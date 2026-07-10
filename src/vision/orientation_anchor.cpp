#include "vision/orientation_anchor.h"

#include <opencv2/imgproc.hpp>

#include <cmath>

#include "vision/position_fixture.h"

namespace pci::vision {

namespace {

Fixture flipped(const Fixture& fixture) {
    Fixture result = fixture;
    result.angleDeg += 180.0;
    if (result.angleDeg >= 180.0) {
        result.angleDeg -= 360.0;
    }
    return result;
}

cv::Mat toGray(const cv::Mat& image) {
    if (image.channels() == 3) {
        cv::Mat gray;
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
        return gray;
    }
    return image;
}

}  // namespace

double sampleIntensity(const cv::Mat& image, const cv::Point2f& imagePoint, int radius) {
    if (image.empty()) {
        return 0.0;
    }
    const cv::Mat gray = toGray(image);
    const int x = cvRound(imagePoint.x);
    const int y = cvRound(imagePoint.y);
    const cv::Rect window =
        cv::Rect(x - radius, y - radius, 2 * radius + 1, 2 * radius + 1) &
        cv::Rect(0, 0, gray.cols, gray.rows);
    if (window.empty()) {
        return 0.0;
    }
    return cv::mean(gray(window))[0];
}

Fixture resolveWithAnchor(const cv::Mat& image, const Fixture& fixture,
                          const OrientationAnchor& anchor) {
    const Fixture alternative = flipped(fixture);
    const double direct =
        std::abs(sampleIntensity(image, toImageCoords(fixture, anchor.piecePoint)) -
                 anchor.intensity);
    const double rotated =
        std::abs(sampleIntensity(image, toImageCoords(alternative, anchor.piecePoint)) -
                 anchor.intensity);
    return rotated < direct ? alternative : fixture;
}

core::Result<void> applyAnchor(const cv::Mat& image, const OrientationAnchor& anchor,
                               PieceAnalysis& analysis) {
    const Fixture resolved = resolveWithAnchor(image, analysis.fixture, anchor);
    if (std::abs(resolved.angleDeg - analysis.fixture.angleDeg) < 1e-9) {
        return core::Result<void>::ok();
    }

    // Hubo giro de 180°: el recorte normalizado debe rehacerse con el fixture
    // corregido para que los embeddings comparen manzanas con manzanas.
    analysis.fixture = resolved;
    auto normalized = normalizePiece(image, analysis.mask, resolved);
    if (!normalized.isOk()) {
        return core::Result<void>::err(normalized.error().message);
    }
    analysis.normalized = std::move(normalized.value());
    return core::Result<void>::ok();
}

}  // namespace pci::vision
