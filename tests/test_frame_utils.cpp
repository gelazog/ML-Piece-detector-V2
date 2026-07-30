#include "camera/camera_controls.h"
#include "camera/frame_utils.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>
#include <opencv2/core.hpp>

#include <QColor>
#include <QImage>

using pci::camera::matToQImage;
using pci::camera::qImageToMat;

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

TEST(FrameUtils, QImageToMatNullGivesEmpty) {
    EXPECT_TRUE(qImageToMat(QImage()).empty());
}

TEST(FrameUtils, QImageToMatRoundTrip) {
    cv::Mat mat(2, 2, CV_8UC3, cv::Scalar(0, 0, 0));
    mat.at<cv::Vec3b>(0, 0) = {255, 0, 0};
    mat.at<cv::Vec3b>(1, 1) = {10, 20, 30};

    const cv::Mat back = qImageToMat(matToQImage(mat));
    ASSERT_EQ(back.type(), CV_8UC3);
    ASSERT_EQ(back.size(), mat.size());
    EXPECT_EQ(back.at<cv::Vec3b>(0, 0), cv::Vec3b(255, 0, 0));
    EXPECT_EQ(back.at<cv::Vec3b>(1, 1), cv::Vec3b(10, 20, 30));
}

TEST(FrameUtils, QImageToMatConvertsForeignFormats) {
    QImage rgb(2, 2, QImage::Format_RGB32);
    rgb.fill(QColor(10, 20, 30));  // R=10, G=20, B=30

    const cv::Mat mat = qImageToMat(rgb);
    ASSERT_EQ(mat.type(), CV_8UC3);
    EXPECT_EQ(mat.at<cv::Vec3b>(0, 0), cv::Vec3b(30, 20, 10));  // BGR
}

// --- Controles de la fuente (O2) ---

TEST(CameraControls, KeysAndLabelsAreUniqueAndStable) {
    std::vector<std::string> keys;
    for (const auto property : pci::camera::allCameraProperties()) {
        const std::string key(pci::camera::propertyKey(property));
        EXPECT_FALSE(key.empty());
        EXPECT_EQ(std::count(keys.begin(), keys.end(), key), 0) << "clave repetida: " << key;
        keys.push_back(key);
        EXPECT_NE(std::string(pci::camera::propertyLabel(property)), std::string());
        EXPECT_NE(std::string(pci::camera::propertyHelp(property)), std::string());
    }
    // Las claves persisten en Settings: fijarlas evita perder ajustes al
    // renombrar el enum.
    EXPECT_EQ(std::string(pci::camera::propertyKey(pci::camera::CameraProperty::Exposure)),
              "cam_exposure");
}

TEST(CameraControls, TogglesAreOnlyTheAutomaticOnes) {
    EXPECT_TRUE(pci::camera::isToggle(pci::camera::CameraProperty::AutoExposure));
    EXPECT_TRUE(pci::camera::isToggle(pci::camera::CameraProperty::AutoFocus));
    EXPECT_FALSE(pci::camera::isToggle(pci::camera::CameraProperty::Brightness));
    EXPECT_FALSE(pci::camera::isToggle(pci::camera::CameraProperty::Exposure));
}

// El rango se deduce del valor que devuelve la cámara porque OpenCV no expone
// mínimo ni máximo y cada backend usa su propia escala.
TEST(CameraControls, RangeAdaptsToTheBackendScale) {
    using pci::camera::CameraProperty;
    using pci::camera::suggestedRange;

    // Escala normalizada (MSMF suele devolver 0..1).
    const auto normalized = suggestedRange(CameraProperty::Brightness, 0.5);
    EXPECT_DOUBLE_EQ(normalized.min, 0.0);
    EXPECT_DOUBLE_EQ(normalized.max, 1.0);
    EXPECT_LT(normalized.step, 1.0);  // hace falta paso decimal

    // Escala 0..255 (DirectShow).
    const auto bytes = suggestedRange(CameraProperty::Brightness, 128.0);
    EXPECT_DOUBLE_EQ(bytes.max, 255.0);
    EXPECT_DOUBLE_EQ(bytes.step, 1.0);

    // Un valor mayor que 255 no debe quedar fuera del deslizador.
    EXPECT_GE(suggestedRange(CameraProperty::Brightness, 900.0).max, 900.0);

    // Exposición en log2 segundos (negativa) frente a microsegundos.
    const auto logExposure = suggestedRange(CameraProperty::Exposure, -6.0);
    EXPECT_LT(logExposure.min, 0.0);
    EXPECT_GE(logExposure.max, 0.0);
    const auto microseconds = suggestedRange(CameraProperty::Exposure, 5000.0);
    EXPECT_GE(microseconds.max, 5000.0);

    // Las casillas son binarias.
    const auto toggle = suggestedRange(CameraProperty::AutoFocus, 1.0);
    EXPECT_DOUBLE_EQ(toggle.min, 0.0);
    EXPECT_DOUBLE_EQ(toggle.max, 1.0);
    EXPECT_DOUBLE_EQ(toggle.step, 1.0);
}
