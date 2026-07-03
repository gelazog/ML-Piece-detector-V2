#pragma once

#include <opencv2/core.hpp>

#include <memory>
#include <string>
#include <vector>

#include "core/result.h"

namespace pci::ml {

// Especificación de entrada del modelo, expuesta para poder testear el
// preprocesado sin cargar ningún modelo.
struct PreprocessSpec {
    int width = 224;
    int height = 224;
    bool nchw = false;  // los EfficientNet-Lite exportados de TF usan NHWC
};

// BGR (CV_8UC3) o gris -> tensor float32 RGB con la normalización de
// EfficientNet-Lite: (x - 127) / 128. Devuelve vacío si la imagen es inválida.
std::vector<float> preprocessToTensor(const cv::Mat& image, const PreprocessSpec& spec);

// Normalización L2 in situ; el vector cero se deja intacto.
void l2Normalize(std::vector<float>& v);

// Envuelve una Ort::Session. El modelo nunca se reentrena: solo inferencia.
// No es thread-safe: una instancia por hilo de inspección.
class EmbeddingExtractor {
public:
    static core::Result<std::unique_ptr<EmbeddingExtractor>> create(
        const std::string& modelPath);
    ~EmbeddingExtractor();

    EmbeddingExtractor(const EmbeddingExtractor&) = delete;
    EmbeddingExtractor& operator=(const EmbeddingExtractor&) = delete;

    // Embedding L2-normalizado de la imagen (idealmente el recorte canónico
    // que produce vision::normalizePiece).
    core::Result<std::vector<float>> extract(const cv::Mat& image);

    // Dimensión del embedding; 0 si el modelo la declara dinámica (se conoce
    // tras la primera extracción).
    [[nodiscard]] int dimension() const;
    [[nodiscard]] const PreprocessSpec& spec() const;

private:
    struct Impl;
    explicit EmbeddingExtractor(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace pci::ml
