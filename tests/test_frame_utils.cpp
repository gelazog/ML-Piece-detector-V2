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

// El rango ya no se adivina del valor: se MIDE al abrir la camara (probeControls)
// y rangeFor solo decide un paso de ajuste usable para ese recorrido.
//
// Motivo del cambio: sondeando una camara real, get(BRIGHTNESS) devolvia 91
// pero set() lo rechazaba, y la exposicion aceptaba solo [-11, -3] en vez del
// [-15, 5] que se suponia. Adivinar producia deslizadores muertos o con topes
// falsos.
TEST(CameraControls, RangeUsesTheMeasuredSpan) {
    using pci::camera::CameraProperty;
    using pci::camera::rangeFor;

    // Recorrido en unidades (0..255 de DirectShow): paso entero.
    const auto wide = rangeFor(CameraProperty::Brightness, 0.0, 255.0);
    EXPECT_DOUBLE_EQ(wide.min, 0.0);
    EXPECT_DOUBLE_EQ(wide.max, 255.0);
    EXPECT_DOUBLE_EQ(wide.step, 1.0);

    // Rango real medido en la camara de pruebas: exposicion de -11 a -3.
    const auto exposure = rangeFor(CameraProperty::Exposure, -11.0, -3.0);
    EXPECT_DOUBLE_EQ(exposure.min, -11.0);
    EXPECT_DOUBLE_EQ(exposure.max, -3.0);
    EXPECT_DOUBLE_EQ(exposure.step, 1.0);

    // Escala normalizada (0..1 de MSMF): hace falta paso decimal o el
    // deslizador solo tendria dos posiciones.
    const auto normalized = rangeFor(CameraProperty::Contrast, 0.0, 1.0);
    EXPECT_LT(normalized.step, 1.0);
    EXPECT_GT(normalized.step, 0.0);

    // Los extremos pueden llegar invertidos (se empuja primero al alto): se
    // ordenan solos.
    const auto swapped = rangeFor(CameraProperty::Gain, 128.0, 8.0);
    EXPECT_DOUBLE_EQ(swapped.min, 8.0);
    EXPECT_DOUBLE_EQ(swapped.max, 128.0);

    // Rango degenerado (la camara no dijo nada util): nunca un rango vacio que
    // deje el deslizador atascado.
    const auto degenerate = rangeFor(CameraProperty::Gain, 5.0, 5.0);
    EXPECT_GT(degenerate.max, degenerate.min);
    EXPECT_GT(degenerate.step, 0.0);

    // Las casillas son binarias pase lo que pase.
    const auto toggle = rangeFor(CameraProperty::AutoFocus, -50.0, 900.0);
    EXPECT_DOUBLE_EQ(toggle.min, 0.0);
    EXPECT_DOUBLE_EQ(toggle.max, 1.0);
    EXPECT_DOUBLE_EQ(toggle.step, 1.0);
}

// Arrastrar un deslizador encola decenas de valores por segundo; aplicarlos
// todos bloqueaba el hilo de captura (cada set() cuesta milisegundos) y de los
// intermedios no queda nada visible.
TEST(CameraControls, CoalescingKeepsOnlyTheLastValuePerProperty) {
    using pci::camera::CameraProperty;
    using pci::camera::CameraControlValue;

    std::vector<CameraControlValue> pending;
    // Simula un arrastre de 200 pasos sobre el brillo mientras la exposicion se
    // toca dos veces.
    for (int i = 0; i < 200; ++i) {
        pci::camera::coalesceControls(pending,
                                      {{CameraProperty::Brightness, static_cast<double>(i)}});
    }
    pci::camera::coalesceControls(pending, {{CameraProperty::Exposure, -7.0}});
    pci::camera::coalesceControls(pending, {{CameraProperty::Exposure, -5.0}});

    ASSERT_EQ(pending.size(), 2U) << "la cola debe tener una entrada por propiedad";
    EXPECT_EQ(pending[0].property, CameraProperty::Brightness);
    EXPECT_DOUBLE_EQ(pending[0].value, 199.0);  // el ultimo valor del arrastre
    EXPECT_EQ(pending[1].property, CameraProperty::Exposure);
    EXPECT_DOUBLE_EQ(pending[1].value, -5.0);

    // El orden de llegada de propiedades distintas se conserva.
    std::vector<CameraControlValue> other;
    pci::camera::coalesceControls(other, {{CameraProperty::AutoFocus, 1.0},
                                          {CameraProperty::Focus, 30.0}});
    ASSERT_EQ(other.size(), 2U);
    EXPECT_EQ(other[0].property, CameraProperty::AutoFocus);
    EXPECT_EQ(other[1].property, CameraProperty::Focus);
}
