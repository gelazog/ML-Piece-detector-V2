#include "camera/frame_utils.h"

#include <gtest/gtest.h>
#include <opencv2/core.hpp>

#include <QColor>
#include <QImage>

using pci::camera::matToQImage;

TEST(FrameUtils, EmptyMatGivesNullImage) {
    const cv::Mat empty;
    EXPECT_TRUE(matToQImage(empty).isNull());
}

TEST(FrameUtils, UnsupportedTypeGivesNullImage) {
    const cv::Mat floats(4, 4, CV_32FC1, cv::Scalar(0.5));
    EXPECT_TRUE(matToQImage(floats).isNull());
}

TEST(FrameUtils, BgrPixelsMapToCorrectColors) {
    cv::Mat mat(2, 2, CV_8UC3, cv::Scalar(0, 0, 0));
    mat.at<cv::Vec3b>(0, 0) = {255, 0, 0};  // azul en BGR
    mat.at<cv::Vec3b>(0, 1) = {0, 255, 0};  // verde
    mat.at<cv::Vec3b>(1, 0) = {0, 0, 255};  // rojo

    const QImage image = matToQImage(mat);
    ASSERT_FALSE(image.isNull());
    EXPECT_EQ(image.size(), QSize(2, 2));
    EXPECT_EQ(image.pixelColor(0, 0), QColor(0, 0, 255));
    EXPECT_EQ(image.pixelColor(1, 0), QColor(0, 255, 0));
    EXPECT_EQ(image.pixelColor(0, 1), QColor(255, 0, 0));
    EXPECT_EQ(image.pixelColor(1, 1), QColor(0, 0, 0));
}

TEST(FrameUtils, ImageOwnsItsBuffer) {
    cv::Mat mat(2, 2, CV_8UC3, cv::Scalar(10, 20, 30));
    QImage image = matToQImage(mat);

    // Al reutilizar el Mat (como hace el hilo de captura), la QImage no cambia.
    mat.setTo(cv::Scalar(200, 200, 200));
    EXPECT_EQ(image.pixelColor(0, 0), QColor(30, 20, 10));
}

TEST(FrameUtils, GrayscaleSupported) {
    const cv::Mat gray(3, 3, CV_8UC1, cv::Scalar(128));
    const QImage image = matToQImage(gray);
    ASSERT_FALSE(image.isNull());
    EXPECT_EQ(image.format(), QImage::Format_Grayscale8);
    EXPECT_EQ(image.pixelColor(1, 1), QColor(128, 128, 128));
}
