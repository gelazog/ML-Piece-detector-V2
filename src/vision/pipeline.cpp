#include "vision/pipeline.h"

#include <opencv2/imgproc.hpp>

#include <utility>
#include <vector>

#include "vision/contour_analysis.h"
#include "vision/position_fixture.h"
#include "vision/segmentation.h"

namespace pci::vision {

core::Result<PieceAnalysis> analyzeFrame(const cv::Mat& image, const PipelineConfig& config) {
    if (image.empty()) {
        return core::Result<PieceAnalysis>::err("Imagen vacía");
    }

    // Zona de detección: todo el pipeline trabaja sobre el recorte y al final
    // los resultados se llevan a coordenadas de la imagen completa.
    const cv::Rect frameRect(0, 0, image.cols, image.rows);
    cv::Rect roi = config.roi & frameRect;
    const bool useRoi = roi.area() > 0 && roi != frameRect;
    const cv::Mat working = useRoi ? image(roi) : image;

    auto mask = segmentPiece(working, config.segmentation);
    if (!mask.isOk()) {
        return core::Result<PieceAnalysis>::err(mask.error().message);
    }

    auto contour =
        findLargestContour(mask.value(), config.minAreaFraction, config.maxAreaFraction);
    if (!contour.isOk()) {
        return core::Result<PieceAnalysis>::err(contour.error().message);
    }

    // Máscara reconstruida solo con el contorno mayor: los blobs de ruido que
    // sobrevivieron a la morfología no deben sesgar el fixture ni el recorte.
    cv::Mat cleanMask = cv::Mat::zeros(mask.value().size(), CV_8UC1);
    const std::vector<std::vector<cv::Point>> fill{contour.value().points};
    cv::drawContours(cleanMask, fill, 0, cv::Scalar(255), cv::FILLED);

    const auto fixture = computeFixture(cleanMask);
    if (!fixture.isOk()) {
        return core::Result<PieceAnalysis>::err(fixture.error().message);
    }

    auto normalized =
        normalizePiece(working, cleanMask, fixture.value(), config.canonicalSize);
    if (!normalized.isOk()) {
        return core::Result<PieceAnalysis>::err(normalized.error().message);
    }

    PieceAnalysis analysis;
    analysis.contour = std::move(contour.value());
    analysis.fixture = fixture.value();
    analysis.normalized = std::move(normalized.value());

    if (useRoi) {
        // Desplazar contorno, fixture y máscara al marco de la imagen completa.
        const cv::Point offset = roi.tl();
        for (auto& point : analysis.contour.points) {
            point += offset;
        }
        analysis.contour.centroid += cv::Point2f(offset);
        analysis.contour.rotatedRect.center += cv::Point2f(offset);
        analysis.fixture.origin += cv::Point2f(offset);

        cv::Mat fullMask = cv::Mat::zeros(image.size(), CV_8UC1);
        cleanMask.copyTo(fullMask(roi));
        analysis.mask = std::move(fullMask);
    } else {
        analysis.mask = std::move(cleanMask);
    }

    return core::Result<PieceAnalysis>::ok(std::move(analysis));
}

}  // namespace pci::vision
