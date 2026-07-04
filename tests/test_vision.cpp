#include <gtest/gtest.h>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <cmath>
#include <vector>

#include "vision/contour_analysis.h"
#include "vision/orientation.h"
#include "vision/pipeline.h"
#include "vision/position_fixture.h"
#include "vision/segmentation.h"

#include "test_helpers.h"

using namespace pci::vision;
using pci::testhelpers::drawLPiece;
using pci::testhelpers::kPi;
using pci::testhelpers::lPieceArea;

namespace {

double angleDiff(double a, double b) {
    double d = a - b;
    while (d >= 180.0) d -= 360.0;
    while (d < -180.0) d += 360.0;
    return d;
}

double maskIoU(const cv::Mat& a, const cv::Mat& b) {
    cv::Mat interMask;
    cv::Mat unionMask;
    cv::bitwise_and(a, b, interMask);
    cv::bitwise_or(a, b, unionMask);
    const int unionCount = cv::countNonZero(unionMask);
    return unionCount > 0 ? static_cast<double>(cv::countNonZero(interMask)) / unionCount : 0.0;
}

}  // namespace

// --- Segmentación ---

TEST(Segmentation, DarkPieceOnLightBackground) {
    const auto image = drawLPiece({640, 480}, {320.0F, 240.0F}, 20.0, 40.0F, 40, 220);
    const auto mask = segmentPiece(image);
    ASSERT_TRUE(mask.isOk());

    const double area = cv::countNonZero(mask.value());
    EXPECT_NEAR(area, lPieceArea(40.0F), lPieceArea(40.0F) * 0.1);
    EXPECT_EQ(mask.value().at<uchar>(10, 10), 0);
}

TEST(Segmentation, LightPieceOnDarkBackground) {
    const auto image = drawLPiece({640, 480}, {320.0F, 240.0F}, -35.0, 40.0F, 230, 30);
    const auto mask = segmentPiece(image);
    ASSERT_TRUE(mask.isOk());

    const double area = cv::countNonZero(mask.value());
    EXPECT_NEAR(area, lPieceArea(40.0F), lPieceArea(40.0F) * 0.1);
}

TEST(Segmentation, EmptyImageFails) {
    EXPECT_FALSE(segmentPiece(cv::Mat()).isOk());
}

// --- Contorno ---

TEST(ContourAnalysis, FindsCentroidAndArea) {
    const cv::Point2f center(300.0F, 220.0F);
    const cv::Mat mask = drawLPiece({640, 480}, center, 15.0, 40.0F, 255, 0);

    const auto contour = findLargestContour(mask);
    ASSERT_TRUE(contour.isOk());
    EXPECT_NEAR(contour.value().centroid.x, center.x, 2.0F);
    EXPECT_NEAR(contour.value().centroid.y, center.y, 2.0F);
    EXPECT_NEAR(contour.value().area, lPieceArea(40.0F), lPieceArea(40.0F) * 0.05);
}

TEST(ContourAnalysis, EmptyMaskFails) {
    const cv::Mat empty = cv::Mat::zeros(480, 640, CV_8UC1);
    EXPECT_FALSE(findLargestContour(empty).isOk());
}

TEST(ContourAnalysis, FullMaskFails) {
    const cv::Mat full(480, 640, CV_8UC1, cv::Scalar(255));
    EXPECT_FALSE(findLargestContour(full).isOk());
}

// --- Orientación ---

TEST(Orientation, TracksRotationFullCircle) {
    const auto baseMask = drawLPiece({640, 480}, {320.0F, 240.0F}, 0.0, 40.0F, 255, 0);
    const auto baseAngle = principalAngleDeg(baseMask);
    ASSERT_TRUE(baseAngle.isOk());

    for (const double theta : {30.0, 90.0, 145.0, 200.0, 300.0}) {
        const auto mask = drawLPiece({640, 480}, {320.0F, 240.0F}, theta, 40.0F, 255, 0);
        const auto angle = principalAngleDeg(mask);
        ASSERT_TRUE(angle.isOk());
        EXPECT_NEAR(angleDiff(angle.value(), baseAngle.value() + theta), 0.0, 2.0)
            << "theta = " << theta;
    }
}

TEST(Orientation, EmptyMaskFails) {
    EXPECT_FALSE(principalAngleDeg(cv::Mat::zeros(100, 100, CV_8UC1)).isOk());
}

// --- Fixture ---

TEST(Fixture, CoordinateRoundTrip) {
    const Fixture fixture{{100.0F, 50.0F}, 30.0};

    for (const auto& p : {cv::Point2f(0.0F, 0.0F), cv::Point2f(37.5F, -12.25F),
                          cv::Point2f(-80.0F, 140.0F)}) {
        const cv::Point2f back = toImageCoords(fixture, toPieceCoords(fixture, p));
        EXPECT_NEAR(back.x, p.x, 1e-3F);
        EXPECT_NEAR(back.y, p.y, 1e-3F);
    }
}

TEST(Fixture, OriginMapsToZeroAndAxisToPositiveX) {
    const Fixture fixture{{100.0F, 50.0F}, 30.0};

    const cv::Point2f origin = toPieceCoords(fixture, fixture.origin);
    EXPECT_NEAR(origin.x, 0.0F, 1e-4F);
    EXPECT_NEAR(origin.y, 0.0F, 1e-4F);

    const double rad = 30.0 * kPi / 180.0;
    const cv::Point2f onAxis(100.0F + 50.0F * static_cast<float>(std::cos(rad)),
                             50.0F + 50.0F * static_cast<float>(std::sin(rad)));
    const cv::Point2f piece = toPieceCoords(fixture, onAxis);
    EXPECT_NEAR(piece.x, 50.0F, 1e-2F);
    EXPECT_NEAR(piece.y, 0.0F, 1e-2F);
}

TEST(Fixture, NormalizeEmptyMaskFails) {
    const cv::Mat image(100, 100, CV_8UC1, cv::Scalar(128));
    const cv::Mat mask = cv::Mat::zeros(100, 100, CV_8UC1);
    EXPECT_FALSE(normalizePiece(image, mask, Fixture{{50.0F, 50.0F}, 0.0}).isOk());
}

// --- Pipeline end-to-end ---

TEST(Pipeline, AnalyzesSyntheticPiece) {
    const cv::Point2f center(320.0F, 240.0F);
    cv::Mat bgr;
    cv::cvtColor(drawLPiece({640, 480}, center, 25.0, 40.0F, 40, 220), bgr,
                 cv::COLOR_GRAY2BGR);

    const auto analysis = analyzeFrame(bgr);
    ASSERT_TRUE(analysis.isOk());
    EXPECT_NEAR(analysis.value().fixture.origin.x, center.x, 2.0F);
    EXPECT_NEAR(analysis.value().fixture.origin.y, center.y, 2.0F);
    EXPECT_EQ(analysis.value().mask.type(), CV_8UC1);
    EXPECT_EQ(analysis.value().normalized.size(), cv::Size(256, 256));
    EXPECT_EQ(analysis.value().normalized.type(), CV_8UC3);
}

// El test de oro: la misma pieza a dos rotaciones y posiciones distintas debe
// producir recortes normalizados prácticamente idénticos.
TEST(Pipeline, NormalizationIsRotationInvariant) {
    const auto imageA = drawLPiece({640, 480}, {300.0F, 240.0F}, 20.0, 40.0F, 40, 220);
    const auto imageB = drawLPiece({640, 480}, {340.0F, 200.0F}, 125.0, 40.0F, 40, 220);

    const auto a = analyzeFrame(imageA);
    const auto b = analyzeFrame(imageB);
    ASSERT_TRUE(a.isOk());
    ASSERT_TRUE(b.isOk());

    cv::Mat maskA;
    cv::Mat maskB;
    cv::threshold(a.value().normalized, maskA, 0.0, 255.0, cv::THRESH_BINARY);
    cv::threshold(b.value().normalized, maskB, 0.0, 255.0, cv::THRESH_BINARY);

    EXPECT_GT(maskIoU(maskA, maskB), 0.90);
}

TEST(Pipeline, FailsOnEmptyImage) {
    EXPECT_FALSE(analyzeFrame(cv::Mat()).isOk());
}

TEST(Pipeline, FailsOnUniformImage) {
    const cv::Mat uniform(480, 640, CV_8UC1, cv::Scalar(128));
    EXPECT_FALSE(analyzeFrame(uniform).isOk());
}
