// UNA SOLA DECISIÓN DE UNIDAD, Y QUE LLEGUE A TODAS PARTES.
//
// Antes, la conversión se resolvía en nueve sitios con la misma línea copiada:
//
//     useCm = unit == Centimeters || (unit == Auto && mm >= 100.0)
//
// Y ya habían derivado: unas copias escribían un decimal y otras dos, así que la
// misma medida salía «12,3 mm» en el lienzo y «12,34 mm» en el informe. Nadie lo
// decide mal a propósito; se decide nueve veces y basta con que una se quede
// atrás. Es literalmente la incoherencia de la que se queja el usuario, y es la
// razón por la que añadir pulgadas requería centralizar primero: nueve sitios
// son nueve oportunidades de olvidarse de uno.

#include <gtest/gtest.h>

#include <opencv2/imgproc.hpp>

#include <cmath>
#include <string>

#include "inspection_editor/execution/measurement_report.h"
#include "inspection_editor/execution/tool_executor.h"

using pci::inspection::LengthUnit;
using pci::inspection::pickArea;
using pci::inspection::pickLength;

TEST(Units, EachUnitConvertsAndLabelsItself) {
    // 25,4 mm es exactamente una pulgada, por definición desde 1959.
    const double mm = 25.4;

    EXPECT_NEAR(pickLength(mm, LengthUnit::Millimeters).value, 25.4, 1e-9);
    EXPECT_STREQ(pickLength(mm, LengthUnit::Millimeters).suffix, "mm");

    EXPECT_NEAR(pickLength(mm, LengthUnit::Centimeters).value, 2.54, 1e-9);
    EXPECT_STREQ(pickLength(mm, LengthUnit::Centimeters).suffix, "cm");

    EXPECT_NEAR(pickLength(mm, LengthUnit::Inches).value, 1.0, 1e-9);
    EXPECT_STREQ(pickLength(mm, LengthUnit::Inches).suffix, "in");

    // Píxeles no convierte: el número ya viene en píxeles por otro camino y
    // esta función no debe tocarlo.
    EXPECT_NEAR(pickLength(mm, LengthUnit::Pixels).value, 25.4, 1e-9);
}

TEST(Units, AutomaticSwitchesToCentimetresAtTenCentimetres) {
    // El corte está en 100 mm: por debajo se lee mejor en milímetros y por
    // encima los números se hacen largos. Se comprueban los dos lados del
    // borde, que es donde un `>` en vez de un `>=` pasa desapercibido.
    EXPECT_STREQ(pickLength(99.9, LengthUnit::Auto).suffix, "mm");
    EXPECT_STREQ(pickLength(100.0, LengthUnit::Auto).suffix, "cm");
    EXPECT_NEAR(pickLength(100.0, LengthUnit::Auto).value, 10.0, 1e-9);

    // Y automática NUNCA da pulgadas: es una unidad que hay que pedir.
    for (double value : {1.0, 25.4, 99.9, 100.0, 5000.0}) {
        EXPECT_STRNE(pickLength(value, LengthUnit::Auto).suffix, "in")
            << "automática eligió pulgadas por su cuenta con " << value << " mm";
    }
}

TEST(Units, AreaScalesWithTheSquareAndNotLinearly) {
    // Una pulgada cuadrada son 25,4² = 645,16 mm². Si la escala entrara lineal
    // el número saldría 25,4 veces mayor, y sería un error creíble: un área con
    // aspecto correcto y unidad correcta.
    EXPECT_NEAR(pickArea(645.16, LengthUnit::Inches).value, 1.0, 1e-9);
    EXPECT_STREQ(pickArea(645.16, LengthUnit::Inches).suffix, "in²");

    EXPECT_NEAR(pickArea(100.0, LengthUnit::Centimeters).value, 1.0, 1e-9);
    EXPECT_STREQ(pickArea(100.0, LengthUnit::Centimeters).suffix, "cm²");
}

TEST(Units, InchesGetEnoughDecimalsToBeUseful) {
    // Una pulgada son 25,4 mm. Con dos decimales, el último dígito vale un
    // cuarto de milímetro: demasiado grueso para una pieza mecanizada, y el
    // operador no tiene forma de saber que le están escondiendo resolución.
    const int inchDecimals = pickLength(25.4, LengthUnit::Inches).decimals;
    EXPECT_GE(inchDecimals, 3) << "en pulgadas se pierde resolución al escribir";

    // La resolución del último dígito, en milímetros, no puede ser peor que la
    // que se da en milímetros.
    const double inchStepMm = 25.4 * std::pow(10.0, -inchDecimals);
    const double mmStepMm =
        std::pow(10.0, -pickLength(25.4, LengthUnit::Millimeters).decimals);
    EXPECT_LE(inchStepMm, mmStepMm * 3.0)
        << "escribir en pulgadas pierde " << (inchStepMm / mmStepMm)
        << " veces más resolución que en milímetros";
}

TEST(Units, TheReportUsesTheSameDecisionAsTheRestOfTheApplication) {
    // El informe tenía su propia copia de la decisión. Si vuelve a tenerla, esto
    // lo caza: se compara lo que dice el informe con lo que dice la función
    // única, para la misma medida y la misma unidad.
    pci::inspection::ToolRunResult result;
    result.kind = pci::inspection::MeasuredKind::Length;
    result.measured = 200.0;  // px

    for (const auto unit : {LengthUnit::Millimeters, LengthUnit::Centimeters,
                            LengthUnit::Inches, LengthUnit::Auto}) {
        const auto rows = pci::inspection::measurementRows({result}, 0.5, unit);
        ASSERT_EQ(rows.size(), 1U);
        const auto expected = pickLength(200.0 * 0.5, unit);
        EXPECT_EQ(rows.front().unit, std::string(expected.suffix))
            << "el informe rotula distinto que el resto para la unidad "
            << static_cast<int>(unit);
        EXPECT_NEAR(rows.front().value, expected.value, 1e-9)
            << "el informe convierte distinto que el resto";
    }
}
