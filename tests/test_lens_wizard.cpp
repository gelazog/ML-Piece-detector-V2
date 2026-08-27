// EL ASISTENTE DE CALIBRACIÓN DE LA LENTE, de punta a punta y sin cámara.
//
// Lo que este asistente tiene que conseguir no es «recoger unas fotos»: es que
// las tomas lleguen a las ESQUINAS del encuadre. Está medido en
// `tests/test_lens_distortion.cpp` y es tajante — doce tomas apiñadas alrededor
// del centro producen una calibración de aspecto impecable que deja el borde un
// 34,88 % desviado, o sea DOS VECES Y MEDIA peor que no corregir nada.
//
// Así que lo que se comprueba aquí no es que el diálogo se abra: es que se NIEGA
// a calibrar mientras falten esquinas, y que dice cuáles faltan.
//
// El banco de tableros está copiado a propósito del otro fichero. Son bancos
// independientes y no deben poder romperse entre sí — la misma decisión, y por
// el mismo motivo, que ya está tomada en `test_calibration_images.cpp`.

#include <gtest/gtest.h>

#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

#include <cstdio>
#include <vector>

#include "camera/frame_utils.h"
#include "ui/lens_calibration_dialog.h"

namespace {

constexpr int kWidth = 1280;
constexpr int kHeight = 960;

cv::Mat cameraMatrix() {
    cv::Mat k = cv::Mat::eye(3, 3, CV_64F);
    k.at<double>(0, 0) = 900.0;
    k.at<double>(1, 1) = 900.0;
    k.at<double>(0, 2) = kWidth / 2.0;
    k.at<double>(1, 2) = kHeight / 2.0;
    return k;
}

cv::Mat distortion() {
    return (cv::Mat_<double>(5, 1) << -0.25, 0.08, 0.0, 0.0, 0.0);
}

QImage boardShot(const pci::vision::BoardSpec& spec, const cv::Vec3d& rvec,
                 const cv::Vec3d& tvec) {
    cv::Mat image(kHeight, kWidth, CV_8UC1, cv::Scalar(235));
    const auto side = static_cast<float>(spec.squareMm);
    constexpr int kSteps = 6;
    for (int row = 0; row <= spec.innerRows; ++row) {
        for (int col = 0; col <= spec.innerCols; ++col) {
            if ((row + col) % 2 != 0) {
                continue;
            }
            const float x0 = static_cast<float>(col - 1) * side;
            const float y0 = static_cast<float>(row - 1) * side;
            std::vector<cv::Point3f> outline;
            for (int i = 0; i < kSteps; ++i) {
                outline.emplace_back(x0 + side * i / kSteps, y0, 0.0F);
            }
            for (int i = 0; i < kSteps; ++i) {
                outline.emplace_back(x0 + side, y0 + side * i / kSteps, 0.0F);
            }
            for (int i = 0; i < kSteps; ++i) {
                outline.emplace_back(x0 + side - side * i / kSteps, y0 + side, 0.0F);
            }
            for (int i = 0; i < kSteps; ++i) {
                outline.emplace_back(x0, y0 + side - side * i / kSteps, 0.0F);
            }
            std::vector<cv::Point2f> projected;
            cv::projectPoints(outline, rvec, tvec, cameraMatrix(), distortion(), projected);
            std::vector<cv::Point> polygon;
            polygon.reserve(projected.size());
            for (const auto& p : projected) {
                polygon.emplace_back(cvRound(p.x), cvRound(p.y));
            }
            cv::fillConvexPoly(image, polygon, cv::Scalar(25), cv::LINE_8);
        }
    }
    cv::Mat colour;
    cv::cvtColor(image, colour, cv::COLOR_GRAY2BGR);
    return pci::camera::matToQImage(colour).copy();
}

std::vector<cv::Vec3d> rotations() {
    return {{0.05, 0.02, 0.0},   {0.35, -0.20, 0.05}, {-0.30, 0.25, -0.05},
            {0.20, 0.30, 0.10},  {-0.25, -0.30, 0.0}, {0.40, 0.05, -0.15},
            {-0.10, 0.40, 0.08}, {0.15, -0.35, 0.12}, {-0.35, -0.10, -0.10},
            {0.28, 0.18, 0.20},  {-0.18, 0.32, 0.15}, {0.10, -0.10, -0.20}};
}

// Repartidas por los nueve rincones del encuadre.
std::vector<cv::Vec3d> spreadOut() {
    return {{80.0, 70.0, 400.0},     {-240.0, 70.0, 400.0},   {80.0, -170.0, 400.0},
            {-240.0, -170.0, 400.0}, {-80.0, 70.0, 400.0},    {-80.0, -170.0, 400.0},
            {80.0, -50.0, 400.0},    {-240.0, -50.0, 400.0},  {-80.0, -50.0, 400.0},
            {40.0, 30.0, 460.0},     {-200.0, -130.0, 460.0}, {-80.0, -50.0, 340.0}};
}

// Todas alrededor del centro: la calibración que parece buena y no lo es.
std::vector<cv::Vec3d> huddled() {
    return {{-80.0, -50.0, 400.0}, {-100.0, -60.0, 430.0}, {-70.0, -40.0, 420.0},
            {-90.0, -55.0, 380.0}, {-85.0, -45.0, 450.0},  {-75.0, -65.0, 410.0},
            {-95.0, -50.0, 395.0}, {-80.0, -35.0, 440.0},  {-105.0, -55.0, 405.0},
            {-65.0, -60.0, 425.0}, {-88.0, -48.0, 415.0},  {-78.0, -52.0, 435.0}};
}

int feedAndCapture(pci::ui::LensCalibrationDialog& dialog,
                   const std::vector<cv::Vec3d>& places) {
    const auto turns = rotations();
    int captured = 0;
    for (std::size_t i = 0; i < places.size(); ++i) {
        if (dialog.offerFrame(boardShot(dialog.boardSpec(), turns[i], places[i])) &&
            dialog.captureCurrent()) {
            ++captured;
        }
    }
    return captured;
}

}  // namespace

TEST(LensWizard, ItRefusesToCalibrateUntilTheCornersAreCovered) {
    pci::ui::LensCalibrationDialog dialog;

    // Doce tomas, todas por el centro. Son de sobra en NÚMERO.
    const int captured = feedAndCapture(dialog, huddled());
    std::printf("  [asistente] apiñadas: %d tomas guardadas, esquinas %d/4\n", captured,
                dialog.coverage().cornersTouched);
    ASSERT_GE(captured, pci::vision::kMinimumViews)
        << "no se guardaron bastantes tomas: la prueba no llega a comprobar nada";

    EXPECT_FALSE(dialog.coverage().goodEnough());
    const QString problem = dialog.tryCalibrate();
    std::printf("  [asistente] dice: %s\n", problem.toStdString().c_str());
    EXPECT_FALSE(problem.isEmpty())
        << "calibra con doce tomas por el centro: ese modelo deja el borde un 35 % "
           "desviado, peor que no corregir nada";
    EXPECT_TRUE(problem.contains(QStringLiteral("esquinas")))
        << "no dice lo único que hay que hacer para arreglarlo";
    EXPECT_FALSE(dialog.result().has_value());

    // Y el botón de calibrar está apagado, que es lo que el operador ve antes de
    // leer ningún mensaje.
    QPushButton* calibrate = nullptr;
    for (auto* button : dialog.findChildren<QPushButton*>()) {
        if (button->objectName() == QStringLiteral("calibrate")) {
            calibrate = button;
        }
    }
    ASSERT_NE(calibrate, nullptr);
    EXPECT_FALSE(calibrate->isEnabled())
        << "el botón invita a calibrar cuando el resultado sería dañino";
}

TEST(LensWizard, WithTheCornersCoveredItCalibratesAndRecoversTheLens) {
    pci::ui::LensCalibrationDialog dialog;

    const int captured = feedAndCapture(dialog, spreadOut());
    const auto coverage = dialog.coverage();
    std::printf("  [asistente] repartidas: %d tomas, esquinas %d/4, zonas %d/9\n", captured,
                coverage.cornersTouched, coverage.cellsTouched);
    ASSERT_GE(captured, pci::vision::kMinimumViews);
    EXPECT_TRUE(coverage.goodEnough());

    QPushButton* calibrate = nullptr;
    for (auto* button : dialog.findChildren<QPushButton*>()) {
        if (button->objectName() == QStringLiteral("calibrate")) {
            calibrate = button;
        }
    }
    ASSERT_NE(calibrate, nullptr);
    EXPECT_TRUE(calibrate->isEnabled()) << "con todo cubierto sigue sin dejar calibrar";

    const QString problem = dialog.tryCalibrate();
    EXPECT_TRUE(problem.isEmpty()) << problem.toStdString();
    ASSERT_TRUE(dialog.result().has_value());

    const auto& model = *dialog.result();
    const double k1 = model.distortion.at<double>(0);
    std::printf("  [asistente] recuperado k1 %.4f (verdad -0,2500), reproyeccion %.3f px, "
                "desplaza %.1f px\n",
                k1, model.reprojectionError, pci::vision::worstDisplacementPx(model));
    EXPECT_NEAR(k1, -0.25, 0.03);
    EXPECT_TRUE(model.isValid());
    EXPECT_FALSE(pci::vision::distortionIsNegligible(model));
}

// Un fotograma sin tablero no deja guardar nada, y lo dice.
TEST(LensWizard, WithNoBoardInSightThereIsNothingToSave) {
    pci::ui::LensCalibrationDialog dialog;
    QImage empty(kWidth, kHeight, QImage::Format_RGB888);
    empty.fill(QColor(120, 120, 120));

    EXPECT_FALSE(dialog.offerFrame(empty));
    EXPECT_FALSE(dialog.captureCurrent());
    EXPECT_EQ(dialog.viewCount(), 0);

    QPushButton* capture = nullptr;
    for (auto* button : dialog.findChildren<QPushButton*>()) {
        if (button->objectName() == QStringLiteral("saveShot")) {
            capture = button;
        }
    }
    ASSERT_NE(capture, nullptr);
    EXPECT_FALSE(capture->isEnabled());
    EXPECT_TRUE(capture->toolTip().contains(QStringLiteral("ENTERO")))
        << "el botón está apagado y no explica qué falta";
}

// Cambiar la descripción del tablero descarta lo recogido: las esquinas
// guardadas son de una rejilla de otro tamaño, y mezclarlas daría un modelo sin
// sentido sin que nada lo dijera.
TEST(LensWizard, ChangingTheBoardThrowsAwayWhatWasCollected) {
    pci::ui::LensCalibrationDialog dialog;
    ASSERT_GE(feedAndCapture(dialog, spreadOut()), pci::vision::kMinimumViews);
    ASSERT_GT(dialog.viewCount(), 0);

    QSpinBox* cols = nullptr;
    for (auto* box : dialog.findChildren<QSpinBox*>()) {
        if (box->value() == 9) {
            cols = box;
        }
    }
    ASSERT_NE(cols, nullptr) << "no se encuentra el campo de esquinas en horizontal";
    cols->setValue(7);

    EXPECT_EQ(dialog.viewCount(), 0)
        << "se quedaron tomas de un tablero distinto: mezclarlas daría un modelo sin "
           "sentido y nada lo diría";
    EXPECT_FALSE(dialog.result().has_value());
}
