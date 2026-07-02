#include "vision/pipeline.h"

#include <opencv2/imgproc.hpp>

#include <utility>
#include <vector>

#include "vision/contour_analysis.h"
#include "vision/position_fixture.h"
#include "vision/segmentation.h"

namespace pci::vision {

core::Result<PieceAnalysis> analyzeFrame(const cv::Mat& image, const PipelineConfig& config) {
    auto mask = segmentPiece(image);
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

    auto normalized = normalizePiece(image, cleanMask, fixture.value(), config.canonicalSize);
    if (!normalized.isOk()) {
        return core::Result<PieceAnalysis>::err(normalized.error().message);
    }

    PieceAnalysis analysis;
    analysis.mask = std::move(cleanMask);
    analysis.contour = std::move(contour.value());
    analysis.fixture = fixture.value();
    analysis.normalized = std::move(normalized.value());
    return core::Result<PieceAnalysis>::ok(std::move(analysis));
}

}  // namespace pci::vision
