// QUIÉN SE LLEVA EL ENTER EN CADA DIÁLOGO.
//
// Continuación de `tests/test_default_buttons.cpp`, que fijó la clase de fallo
// en los dos diálogos donde ya había mordido. Este barre los demás.
//
// El fallo no lo escribe nadie a propósito. En un `QDialog` sin botón por
// defecto declarado, **Qt coge el primer botón que se construyó**, así que quién
// responde al Enter lo deciden tres líneas de C++ que nadie vuelve a mirar. En
// la guía de atajos la respuesta era «Restaurar por defecto» —que borra toda la
// tabla de teclas recién editada— y en el calibrador era «Calcular escala» en
// vez de «Aplicar calibración».
//
// No hace falta ningún error de programación para perder el trabajo: basta con
// teclear Enter creyendo que guardas.
//
// SE PREGUNTA A LOS BOTONES, no se deduce del orden de construcción. Lo que
// importa es lo que Qt va a hacer, no lo que uno cree que hará leyendo el
// código — que es precisamente lo que falla aquí.
//
// Y se comprueban DOS cosas, que no son la misma:
//
//   1. Quién se lleva el Enter no puede ser un botón que destruya.
//   2. Ningún botón destructivo conserva `autoDefault`, porque con él basta
//      tabular hasta él para que el Enter siguiente destruya. Al mutar el
//      arreglo del calibrador se vio que quitar el `setDefault(true)` NO movía
//      el botón por defecto —el `setAutoDefault(false)` del primero ya lo
//      desplazaba— y lo cazó la segunda comprobación.

#include <gtest/gtest.h>

#include <QApplication>
#include <QDialog>
#include <QPushButton>
#include <QString>
#include <QStringList>

#include <cstdio>

#include "ui/background_patch_dialog.h"
#include "ui/history_dialog.h"
#include "ui/lens_calibration_dialog.h"
#include "ui/measurement_mode_dialog.h"
#include "ui/piece_manager_dialog.h"
#include "ui/template_manager_dialog.h"

using namespace pci;

namespace {

// Por prefijo y no por texto exacto: «Restaurar», «Restablecer» y «Borrar»
// aparecen conjugados de varias formas y una lista de textos exactos
// envejecería a la primera reescritura de una etiqueta.
bool looksDestructive(const QString& text) {
    static const QStringList words{
        QStringLiteral("Restaurar"), QStringLiteral("Restablecer"),
        QStringLiteral("Borrar"),    QStringLiteral("Eliminar"),
        QStringLiteral("Quitar"),    QStringLiteral("Descartar"),
    };
    for (const auto& word : words) {
        if (text.contains(word, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

QPushButton* defaultButton(const QDialog& dialog) {
    const auto buttons = dialog.findChildren<QPushButton*>();
    for (auto* button : buttons) {
        if (button->isDefault()) {
            return button;
        }
    }
    // Sin ninguno declarado, Qt se queda con el primero que tenga autoDefault.
    for (auto* button : buttons) {
        if (button->autoDefault()) {
            return button;
        }
    }
    return nullptr;
}

// El barrido que hacen todas las pruebas de abajo, para que cada diálogo nuevo
// sea una línea y no una copia.
void enterMustNotDestroy(const QDialog& dialog, const char* name) {
    auto* takesEnter = defaultButton(dialog);
    std::printf("  [enter] %-24s -> %s\n", name,
                takesEnter == nullptr ? "(ninguno)"
                                      : takesEnter->text().toStdString().c_str());
    if (takesEnter != nullptr) {
        EXPECT_FALSE(looksDestructive(takesEnter->text()))
            << name << ": el Enter dispara «" << takesEnter->text().toStdString()
            << "», que destruye. Nadie lo eligió: Qt coge el primer botón "
               "construido, y basta teclear Enter creyendo que guardas.";
    }
    for (auto* button : dialog.findChildren<QPushButton*>()) {
        if (looksDestructive(button->text())) {
            EXPECT_FALSE(button->autoDefault())
                << name << ": «" << button->text().toStdString()
                << "» conserva autoDefault, así que basta tabular hasta él para que "
                   "el Enter siguiente destruya";
        }
    }
}

}  // namespace

TEST(EnterInDialogs, TheMeasurementModeDialogDoesNotFireSomethingDestructive) {
    repositories::PieceMeasurement measurement;
    ui::MeasurementModeDialog dialog(measurement, QStringLiteral("pieza de prueba"));
    enterMustNotDestroy(dialog, "Modo de medición");
}

TEST(EnterInDialogs, TheTemplateManagerDoesNotFireDeleteOnEnter) {
    // Sin base de datos: el diálogo se construye igual y lo que se mira son sus
    // botones. Y aquí el riesgo es literal — este diálogo BORRA plantillas con
    // todas sus herramientas dentro.
    ui::TemplateManagerDialog dialog(nullptr, 0, QStringLiteral("principal"));
    enterMustNotDestroy(dialog, "Plantillas");
}

TEST(EnterInDialogs, ThePieceManagerDoesNotFireDeleteOnEnter) {
    // El más peligroso de todos: borra una pieza con TODAS sus referencias,
    // herramientas e historial.
    ui::PieceManagerDialog dialog(nullptr, nullptr);
    enterMustNotDestroy(dialog, "Piezas");
}

TEST(EnterInDialogs, TheHistoryDialogDoesNotFireSomethingDestructive) {
    ui::HistoryDialog dialog(nullptr, nullptr, 0);
    enterMustNotDestroy(dialog, "Historial");
}

TEST(EnterInDialogs, TheLensCalibrationDialogDoesNotFireSomethingDestructive) {
    ui::LensCalibrationDialog dialog;
    enterMustNotDestroy(dialog, "Calibración de lente");
}

TEST(EnterInDialogs, TheBackgroundPatchDialogDoesNotFireSomethingDestructive) {
    cv::Mat flat(200, 200, CV_8UC3, cv::Scalar(30, 30, 200));
    ui::BackgroundPatchDialog dialog(flat, vision::SegmentationOptions{});
    enterMustNotDestroy(dialog, "Señalar el fondo");
}
