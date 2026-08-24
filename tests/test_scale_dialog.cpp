// CALIBRAR NO PUEDE OBLIGAR A CONVERTIR A MANO.
//
// El campo de longitud de referencia solo aceptaba milímetros. Quien tiene
// delante una regla en pulgadas o una marca de 5 cm tenía que convertir de
// cabeza, y un error ahí no sale como un error: sale como cotas perfectamente
// formateadas y todas mal. Es de los fallos más caros que puede tener una
// aplicación de medida, porque el resultado parece bueno.
//
// Y la longitud es lo ÚNICO que hay que teclear cada vez que se calibra —era
// también lo único que no se recordaba, mientras la distancia de cámara y el FOV
// sí volvían puestos. Dos quejas del usuario en la misma pantalla: «solo
// menciona los px a mm» y «no se guarda la anterior configuración».

#include <gtest/gtest.h>

#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QImage>

#include "ui/calibration_dialog.h"

namespace {

QImage plainScene() {
    QImage scene(640, 480, QImage::Format_RGB888);
    scene.fill(Qt::darkGray);
    return scene;
}

}  // namespace

TEST(ScaleDialog, TheReferenceCanBeGivenInSomethingOtherThanMillimetres) {
    // Las cuatro unidades tienen que convertir bien. Una pulgada mal puesta
    // —25,0 en vez de 25,4— es un 1,6 % de error en TODAS las cotas.
    ASSERT_EQ(pci::ui::CalibrationDialog::unitCount(), 4);
    EXPECT_DOUBLE_EQ(pci::ui::CalibrationDialog::millimetresPerUnit(0), 1.0);     // mm
    EXPECT_DOUBLE_EQ(pci::ui::CalibrationDialog::millimetresPerUnit(1), 10.0);    // cm
    EXPECT_DOUBLE_EQ(pci::ui::CalibrationDialog::millimetresPerUnit(2), 1000.0);  // m
    EXPECT_DOUBLE_EQ(pci::ui::CalibrationDialog::millimetresPerUnit(3), 25.4);    // pulgada

    // Un índice fuera de la lista no puede dar un factor inventado: se cae a
    // milímetros, que es lo que había antes de que hubiera lista.
    EXPECT_DOUBLE_EQ(pci::ui::CalibrationDialog::millimetresPerUnit(-1), 1.0);
    EXPECT_DOUBLE_EQ(pci::ui::CalibrationDialog::millimetresPerUnit(99), 1.0);
}

TEST(ScaleDialog, TheDialogOffersEveryUnitAndNotOnlyMillimetres) {
    pci::ui::CalibrationDialog dialog(plainScene(), {}, {}, nullptr);
    QComboBox* units = nullptr;
    for (auto* candidate : dialog.findChildren<QComboBox*>()) {
        if (candidate->count() == pci::ui::CalibrationDialog::unitCount()) {
            units = candidate;
        }
    }
    ASSERT_NE(units, nullptr) << "no hay selector de unidad: la referencia sigue "
                                 "obligando a convertir a mano";
    EXPECT_EQ(units->itemText(0), QStringLiteral("mm"));
    EXPECT_EQ(units->itemText(3), QStringLiteral("pulgadas"));
}

TEST(ScaleDialog, TheLengthTypedLastTimeComesBackAndSoDoesItsUnit) {
    // Se abre con lo que se escribió la vez anterior: 6 pulgadas.
    pci::ui::ScaleEntry previous;
    previous.knownLength = 6.0;
    previous.unitIndex = 3;

    pci::ui::CalibrationDialog dialog(plainScene(), {}, previous, nullptr);
    const auto back = dialog.lastEntry();
    EXPECT_DOUBLE_EQ(back.knownLength, 6.0)
        << "la longitud vuelve a 100: hay que reescribirla en cada calibración";
    EXPECT_EQ(back.unitIndex, 3) << "la unidad no se recuerda: vuelve a milímetros";

    // Y el campo tiene que aceptar decimales finos: media pulgada es 12,7 mm, y
    // un campo de un decimal la redondearía sin avisar.
    QDoubleSpinBox* length = nullptr;
    for (auto* candidate : dialog.findChildren<QDoubleSpinBox*>()) {
        if (std::abs(candidate->value() - 6.0) < 1e-9) {
            length = candidate;
        }
    }
    ASSERT_NE(length, nullptr);
    EXPECT_GE(length->decimals(), 2) << "el campo redondea la referencia";
}

TEST(ScaleDialog, AnEmptyPreviousEntryStillOpensWithSomethingUsable) {
    // La primera vez no hay nada guardado. El campo no puede quedarse en cero:
    // una referencia de cero da una escala infinita, y el diálogo abriría
    // enseñando un valor que nunca puede estar bien.
    pci::ui::ScaleEntry nothing;
    nothing.knownLength = 0.0;
    pci::ui::CalibrationDialog dialog(plainScene(), {}, nothing, nullptr);
    EXPECT_GT(dialog.lastEntry().knownLength, 0.0);
}

TEST(ScaleDialog, TheUnitActuallyReachesTheNumberThatIsCalculated) {
    // Que exista una tabla de conversión no demuestra que el cálculo la use, y
    // ese es el único fallo que de verdad importa aquí: una pulgada tomada como
    // un milímetro no da un error visible, da cotas bien formateadas y
    // veinticinco veces equivocadas.
    struct Case {
        int unitIndex;
        double typed;
        double expectedMm;
        const char* what;
    };
    const Case cases[] = {
        {0, 100.0, 100.0, "100 mm"},
        {1, 5.0, 50.0, "5 cm"},
        {2, 0.25, 250.0, "un cuarto de metro"},
        {3, 6.0, 152.4, "6 pulgadas"},
    };
    for (const auto& one : cases) {
        pci::ui::ScaleEntry entry;
        entry.knownLength = one.typed;
        entry.unitIndex = one.unitIndex;
        pci::ui::CalibrationDialog dialog(plainScene(), {}, entry, nullptr);
        EXPECT_NEAR(dialog.knownLengthMm(), one.expectedMm, 1e-9) << one.what;
    }
}
