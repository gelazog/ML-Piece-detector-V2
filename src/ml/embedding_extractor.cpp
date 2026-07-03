#include "ml/embedding_extractor.h"

#include <onnxruntime_cxx_api.h>
#include <opencv2/imgproc.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <numeric>
#include <utility>

#include "core/logging.h"

namespace pci::ml {

std::vector<float> preprocessToTensor(const cv::Mat& image, const PreprocessSpec& spec) {
    if (image.empty() || spec.width <= 0 || spec.height <= 0) {
        return {};
    }

    cv::Mat bgr;
    if (image.channels() == 1) {
        cv::cvtColor(image, bgr, cv::COLOR_GRAY2BGR);
    } else if (image.channels() == 3) {
        bgr = image;
    } else {
        return {};
    }

    cv::Mat resized;
    if (bgr.size() == cv::Size(spec.width, spec.height)) {
        resized = bgr;
    } else {
        const bool shrinking = bgr.cols > spec.width || bgr.rows > spec.height;
        cv::resize(bgr, resized, cv::Size(spec.width, spec.height), 0.0, 0.0,
                   shrinking ? cv::INTER_AREA : cv::INTER_LINEAR);
    }

    cv::Mat rgb;
    cv::cvtColor(resized, rgb, cv::COLOR_BGR2RGB);

    const int pixels = spec.width * spec.height;
    std::vector<float> tensor(static_cast<std::size_t>(pixels) * 3);

    for (int y = 0; y < spec.height; ++y) {
        const cv::Vec3b* row = rgb.ptr<cv::Vec3b>(y);
        for (int x = 0; x < spec.width; ++x) {
            const int pixel = y * spec.width + x;
            for (int c = 0; c < 3; ++c) {
                const float value = (static_cast<float>(row[x][c]) - 127.0F) / 128.0F;
                if (spec.nchw) {
                    tensor[static_cast<std::size_t>(c) * pixels + pixel] = value;
                } else {
                    tensor[static_cast<std::size_t>(pixel) * 3 + c] = value;
                }
            }
        }
    }
    return tensor;
}

void l2Normalize(std::vector<float>& v) {
    double sum = 0.0;
    for (const float x : v) {
        sum += static_cast<double>(x) * x;
    }
    if (sum <= 0.0) {
        return;
    }
    const float inv = static_cast<float>(1.0 / std::sqrt(sum));
    for (float& x : v) {
        x *= inv;
    }
}

struct EmbeddingExtractor::Impl {
    Ort::Env env{ORT_LOGGING_LEVEL_ERROR, "pc_inspector"};
    Ort::Session session{nullptr};
    Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    std::string inputName;
    std::string outputName;
    std::vector<std::int64_t> inputShape;
    PreprocessSpec spec;
    int dimension = 0;
};

EmbeddingExtractor::EmbeddingExtractor(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
EmbeddingExtractor::~EmbeddingExtractor() = default;

core::Result<std::unique_ptr<EmbeddingExtractor>> EmbeddingExtractor::create(
    const std::string& modelPath) {
    using ResultT = core::Result<std::unique_ptr<EmbeddingExtractor>>;

    if (!std::filesystem::exists(modelPath)) {
        return ResultT::err("No existe el archivo de modelo: " + modelPath);
    }

    try {
        auto impl = std::make_unique<Impl>();

        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(2);
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        const std::filesystem::path path(modelPath);
        impl->session = Ort::Session(impl->env, path.c_str(), options);

        Ort::AllocatorWithDefaultOptions allocator;
        impl->inputName = impl->session.GetInputNameAllocated(0, allocator).get();
        impl->outputName = impl->session.GetOutputNameAllocated(0, allocator).get();

        auto shape =
            impl->session.GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        if (shape.size() != 4) {
            return ResultT::err("El modelo no recibe imágenes (entrada de " +
                                std::to_string(shape.size()) + " dimensiones)");
        }

        // Distingue NCHW ([1,3,H,W]) de NHWC ([1,H,W,3]); dimensiones dinámicas
        // (-1) se resuelven al tamaño por defecto del spec.
        if (shape[1] == 3) {
            impl->spec.nchw = true;
            impl->spec.height = shape[2] > 0 ? static_cast<int>(shape[2]) : impl->spec.height;
            impl->spec.width = shape[3] > 0 ? static_cast<int>(shape[3]) : impl->spec.width;
        } else if (shape[3] == 3) {
            impl->spec.nchw = false;
            impl->spec.height = shape[1] > 0 ? static_cast<int>(shape[1]) : impl->spec.height;
            impl->spec.width = shape[2] > 0 ? static_cast<int>(shape[2]) : impl->spec.width;
        } else {
            return ResultT::err("Disposición de entrada no reconocida (ni NCHW ni NHWC)");
        }

        impl->inputShape = impl->spec.nchw
                               ? std::vector<std::int64_t>{1, 3, impl->spec.height,
                                                           impl->spec.width}
                               : std::vector<std::int64_t>{1, impl->spec.height,
                                                           impl->spec.width, 3};

        const auto outputShape =
            impl->session.GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        std::int64_t dim = 1;
        for (const auto d : outputShape) {
            if (d > 1) {
                dim *= d;
            }
        }
        impl->dimension = dim > 1 ? static_cast<int>(dim) : 0;

        core::logInfo("Modelo cargado: " + modelPath + " (entrada " +
                      std::to_string(impl->spec.width) + "x" +
                      std::to_string(impl->spec.height) +
                      (impl->spec.nchw ? " NCHW" : " NHWC") + ", embedding " +
                      std::to_string(impl->dimension) + ")");

        return ResultT::ok(std::unique_ptr<EmbeddingExtractor>(
            new EmbeddingExtractor(std::move(impl))));
    } catch (const Ort::Exception& e) {
        return ResultT::err(std::string("ONNX Runtime rechazó el modelo: ") + e.what());
    } catch (const std::exception& e) {
        return ResultT::err(std::string("Fallo cargando el modelo: ") + e.what());
    }
}

core::Result<std::vector<float>> EmbeddingExtractor::extract(const cv::Mat& image) {
    using ResultT = core::Result<std::vector<float>>;

    std::vector<float> tensor = preprocessToTensor(image, impl_->spec);
    if (tensor.empty()) {
        return ResultT::err("Imagen inválida para extracción de embedding");
    }

    try {
        Ort::Value input = Ort::Value::CreateTensor<float>(
            impl_->memoryInfo, tensor.data(), tensor.size(), impl_->inputShape.data(),
            impl_->inputShape.size());

        const char* inputNames[] = {impl_->inputName.c_str()};
        const char* outputNames[] = {impl_->outputName.c_str()};
        auto outputs = impl_->session.Run(Ort::RunOptions{nullptr}, inputNames, &input, 1,
                                          outputNames, 1);

        const auto info = outputs[0].GetTensorTypeAndShapeInfo();
        const float* data = outputs[0].GetTensorData<float>();
        const std::size_t count = info.GetElementCount();
        if (count == 0) {
            return ResultT::err("El modelo devolvió un embedding vacío");
        }

        std::vector<float> embedding(data, data + count);
        l2Normalize(embedding);
        impl_->dimension = static_cast<int>(count);
        return ResultT::ok(std::move(embedding));
    } catch (const Ort::Exception& e) {
        return ResultT::err(std::string("Fallo de inferencia ONNX: ") + e.what());
    }
}

int EmbeddingExtractor::dimension() const {
    return impl_->dimension;
}

const PreprocessSpec& EmbeddingExtractor::spec() const {
    return impl_->spec;
}

}  // namespace pci::ml
