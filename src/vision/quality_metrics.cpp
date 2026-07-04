#include "vision/quality_metrics.h"

#include <opencv2/imgproc.hpp>

namespace pci::vision {

domain::QualityMetrics computeQualityMetrics(const cv::Mat& image,
                                             const PieceAnalysis* analysis) {
    domain::QualityMetrics metrics;
    if (image.empty()) {
        return metrics;
    }

    cv::Mat gray;
    if (image.channels() == 3) {
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = image;
    }

    // Nitidez: varianza del Laplaciano (bajo = borroso).
    cv::Mat laplacian;
    cv::Laplacian(gray, laplacian, CV_64F);
    cv::Scalar mean;
    cv::Scalar stddev;
    cv::meanStdDev(laplacian, mean, stddev);
    metrics.sharpness = stddev[0] * stddev[0];

    metrics.meanBrightness = cv::mean(gray)[0];

    const int nearBlack = cv::countNonZero(gray < 5);
    const int nearWhite = cv::countNonZero(gray > 250);
    metrics.clippedFraction =
        static_cast<double>(nearBlack + nearWhite) / static_cast<double>(gray.total());

    if (analysis != nullptr && !analysis->contour.points.empty()) {
        metrics.pieceFound = true;
        const cv::Rect box = cv::boundingRect(analysis->contour.points);
        constexpr int kMargin = 2;
        metrics.pieceTouchesBorder = box.x <= kMargin || box.y <= kMargin ||
                                     box.x + box.width >= gray.cols - kMargin ||
                                     box.y + box.height >= gray.rows - kMargin;
    }
    return metrics;
}

}  // namespace pci::vision
