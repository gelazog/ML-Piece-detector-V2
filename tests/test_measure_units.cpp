// Banco de UNIDADES: que ninguna medida se publique con una unidad que no es
// la suya.
//
// Existe por un fallo que estuvo repartido en cuatro pantallas a la vez. Las
// cuatro decidían por su cuenta cómo rotular una medida, y las cuatro aplicaban
// la misma regla equivocada —«todo lo que no sea un ángulo va en milímetros»—,
// así que un hexágono se pintaba como «Lados (6): 6,00 mm», un área en píxeles
// CUADRADOS se multiplicaba por la escala lineal (número falso además de unidad
// falsa) y una circularidad, que no tiene unidades, salía en milímetros.
//
// El modelo ya sabía distinguirlo; la capa que pintaba no se lo preguntaba. Un
// número con la unidad equivocada es peor que no dar el número: el primero se
// apunta en una hoja y se usa.
#include <gtest/gtest.h>

#include <opencv2/imgproc.hpp>

#include <string>

#include "inspection_editor/execution/tool_executor.h"
#include "inspection_editor/tools/tool_geometry.h"

using namespace pci::inspection;

namespace {

ToolRunResult reading(double measured, MeasuredKind kind) {
    ToolRunResult result;
    result.measured = measured;
    result.kind = kind;
    result.ok = true;
    return result;
}

// 0,25 mm por píxel: una escala redonda para poder comprobar la conversión de
// cabeza y que el número esperado no dependa de creerse el código.
constexpr double kScale = 0.25;

}  // namespace

TEST(MeasureUnits, ACountIsNeverALength) {
    // EL FALLO, tal cual: un hexágono mide 6 LADOS, y con la calibración puesta
    // se rotulaba «6,00 mm». Seis milímetros de nada.
    const auto sides = reading(6.0, MeasuredKind::Count);
    const std::string text = formatMeasure(sides, kScale, LengthUnit::Auto);
    EXPECT_EQ(text, "n=6");
    EXPECT_EQ(text.find("mm"), std::string::npos) << "un recuento salió en milímetros";
    EXPECT_EQ(text.find("px"), std::string::npos) << "un recuento salió en píxeles";
    // Y sin calibración da lo mismo: un recuento no depende de la escala.
    EXPECT_EQ(formatMeasure(sides, 0.0, LengthUnit::Auto), text);
    EXPECT_EQ(formatMeasure(sides, kScale, LengthUnit::Pixels), text);
}

TEST(MeasureUnits, AFractionHasNoUnitAtAll) {
    // Una circularidad de 0,93 no son 0,93 de nada. La ausencia de unidad es la
    // respuesta correcta, no un olvido.
    const auto circularity = reading(0.934, MeasuredKind::Fraction);
    const std::string text = formatMeasure(circularity, kScale, LengthUnit::Auto);
    EXPECT_EQ(text, "0.934");
    EXPECT_EQ(text.find("mm"), std::string::npos);
    EXPECT_EQ(text.find("px"), std::string::npos);
    EXPECT_EQ(text.find("°"), std::string::npos);
}

TEST(MeasureUnits, AnAreaUsesTheScaleSquaredAndSaysSo) {
    // El más silencioso de los tres: el área vive en px² y se multiplicaba por
    // la escala LINEAL. No solo la unidad era falsa — el número también, y por
    // un factor igual a la propia escala.
    const auto area = reading(1600.0, MeasuredKind::Area);
    const std::string text = formatMeasure(area, kScale, LengthUnit::Millimeters);
    // 1600 px² · (0,25 mm/px)² = 100 mm². Con la escala lineal habrían salido
    // 400, que es cuatro veces el valor real.
    EXPECT_NE(text.find("100.0mm²"), std::string::npos)
        << "el área no se convirtió con la escala al cuadrado: " << text;
    EXPECT_NE(text.find("px²"), std::string::npos)
        << "el área perdió su equivalente en píxeles: " << text;
    // Sin calibrar se dan píxeles cuadrados y se dicen. Inventar milímetros sin
    // escala sería la peor salida de todas.
    EXPECT_EQ(formatMeasure(area, 0.0, LengthUnit::Auto), "1600px²");
}

TEST(MeasureUnits, AnAngleIsAlwaysDegrees) {
    const auto angle = reading(119.47, MeasuredKind::Angle);
    EXPECT_EQ(formatMeasure(angle, kScale, LengthUnit::Auto), "119.5°");
    EXPECT_EQ(formatMeasure(angle, 0.0, LengthUnit::Pixels), "119.5°");
}

TEST(MeasureUnits, ALengthStillConvertsAsItAlwaysDid) {
    // La clase que sí es una longitud no puede haber cambiado de comportamiento
    // al separar las demás.
    const auto length = reading(200.0, MeasuredKind::Length);
    EXPECT_EQ(formatMeasure(length, 0.0, LengthUnit::Auto), "200.0px");
    EXPECT_EQ(formatMeasure(length, kScale, LengthUnit::Millimeters), "50.00mm (200.0px)");
    EXPECT_EQ(formatMeasure(length, kScale, LengthUnit::Pixels), "200.0px");
    // 200 px · 0,25 = 50 mm, que no llega a 10 cm: en automático manda mm.
    EXPECT_EQ(formatMeasure(length, kScale, LengthUnit::Auto), "50.00mm (200.0px)");
    // 1000 px · 0,25 = 250 mm sí pasa de 10 cm.
    EXPECT_EQ(formatMeasure(reading(1000.0, MeasuredKind::Length), kScale, LengthUnit::Auto),
              "25.00cm (1000.0px)");
}

TEST(MeasureUnits, TheCompactFormDropsThePixelTailAndNothingElse) {
    // La etiqueta que se pinta encima de la pieza no tiene sitio para el sufijo
    // en píxeles, pero sí tiene que llevar la unidad.
    EXPECT_EQ(formatMeasure(reading(200.0, MeasuredKind::Length), kScale, LengthUnit::Auto,
                            true),
              "50.00mm");
    EXPECT_EQ(formatMeasure(reading(1600.0, MeasuredKind::Area), kScale,
                            LengthUnit::Millimeters, true),
              "100.0mm²");
    // Sin escala no hay sufijo que quitar: el compacto es idéntico al largo.
    EXPECT_EQ(formatMeasure(reading(200.0, MeasuredKind::Length), 0.0, LengthUnit::Auto, true),
              "200.0px");
}

TEST(MeasureUnits, EveryKindHasItsOwnKeyForTheDatabase) {
    // La columna `unit` de Measurements guardaba «px» para todo, ángulos y
    // recuentos incluidos. Una columna que siempre dice lo mismo no es un dato,
    // y esta además mentía: el histórico existe para poder releerse, y un valor
    // sin su unidad correcta no se puede releer.
    EXPECT_STREQ(measuredUnitKey(MeasuredKind::Length), "px");
    EXPECT_STREQ(measuredUnitKey(MeasuredKind::Angle), "°");
    EXPECT_STREQ(measuredUnitKey(MeasuredKind::Count), "n");
    EXPECT_STREQ(measuredUnitKey(MeasuredKind::Fraction), "—");
    EXPECT_STREQ(measuredUnitKey(MeasuredKind::Area), "px²");

    // Y ninguna clase comparte clave con otra: si dos coincidieran, releer el
    // histórico volvería a ser adivinar.
    const std::vector<std::string> keys{
        measuredUnitKey(MeasuredKind::Length), measuredUnitKey(MeasuredKind::Angle),
        measuredUnitKey(MeasuredKind::Count), measuredUnitKey(MeasuredKind::Fraction),
        measuredUnitKey(MeasuredKind::Area)};
    for (std::size_t i = 0; i < keys.size(); ++i) {
        for (std::size_t j = i + 1; j < keys.size(); ++j) {
            EXPECT_NE(keys[i], keys[j]) << "dos clases de medida comparten unidad";
        }
    }
}

// ---------------------------------------------------------------------------
// Lo que de verdad importa: que quien MIDE marque bien la clase
// ---------------------------------------------------------------------------

namespace {

// Una pieza cuadrada clara sobre fondo oscuro, con un agujero, para poder
// ejecutar herramientas de verdad y no solo formatear números inventados.
cv::Mat squareWithAHole() {
    cv::Mat image(400, 400, CV_8UC1, cv::Scalar(20));
    cv::rectangle(image, cv::Rect(100, 100, 200, 200), cv::Scalar(220), cv::FILLED,
                  cv::LINE_8);
    cv::circle(image, cv::Point(200, 200), 30, cv::Scalar(20), cv::FILLED, cv::LINE_8);
    return image;
}

pci::vision::Fixture centredFixture() {
    pci::vision::Fixture fixture;
    fixture.origin = {200.0F, 200.0F};
    fixture.angleDeg = 0.0;
    return fixture;
}

ToolRunResult runRegion(RegionMeasure measure) {
    RegionGeometry geometry;
    geometry.center = {0.0F, 0.0F};
    geometry.width = 220.0F;
    geometry.height = 220.0F;
    geometry.darkPiece = false;  // la pieza es la mancha CLARA sobre fondo oscuro
    geometry.measure = measure;
    ToolConfig config;
    config.id = 1;
    config.name = "region";
    config.type = ToolType::Region;
    config.geometryJson = toJson(ToolGeometry(geometry));
    config.toleranceMin = 0.0;
    config.toleranceMax = 1e12;
    const auto result = runTool(squareWithAHole(), centredFixture(), config);
    EXPECT_TRUE(result.isOk()) << (result.isOk() ? "" : result.error().message);
    return result.isOk() ? result.value() : ToolRunResult{};
}

}  // namespace

TEST(MeasureUnits, TheRegionToolLabelsEachOfItsSixMeasuresCorrectly) {
    // La Región es el caso que demuestra por qué la clase no puede deducirse del
    // TIPO de herramienta: mide seis cosas distintas —tres clases distintas—
    // con el mismo tipo. Preguntarle al tipo habría dado la misma respuesta para
    // un área y para una circularidad.
    EXPECT_EQ(runRegion(RegionMeasure::Area).kind, MeasuredKind::Area);
    EXPECT_EQ(runRegion(RegionMeasure::Perimeter).kind, MeasuredKind::Length);
    EXPECT_EQ(runRegion(RegionMeasure::Solidity).kind, MeasuredKind::Fraction);
    EXPECT_EQ(runRegion(RegionMeasure::Circularity).kind, MeasuredKind::Fraction);
    EXPECT_EQ(runRegion(RegionMeasure::AspectRatio).kind, MeasuredKind::Fraction);
    EXPECT_EQ(runRegion(RegionMeasure::HoleCount).kind, MeasuredKind::Count);

    // Y que el rotulado que sale de ahí sea el bueno de punta a punta: el área
    // del cuadrado menos el agujero, con escala, no puede salir en milímetros
    // lineales.
    const auto area = runRegion(RegionMeasure::Area);
    ASSERT_TRUE(area.ok) << area.detail;
    const std::string text = formatMeasure(area, kScale, LengthUnit::Millimeters);
    std::printf("  [unidades] area de la region: %s\n", text.c_str());
    EXPECT_NE(text.find("mm²"), std::string::npos) << text;

    const auto holes = runRegion(RegionMeasure::HoleCount);
    ASSERT_TRUE(holes.ok) << holes.detail;
    EXPECT_EQ(formatMeasure(holes, kScale, LengthUnit::Millimeters), "n=1")
        << "el recuento de agujeros salió con unidades de longitud";
}

TEST(MeasureUnits, ThePolygonToolCountsSidesAndSaysThatEsUnRecuento) {
    // El caso que se veía en pantalla: «Lados (6): 6,00 mm».
    PolygonGeometry geometry;
    geometry.center = {0.0F, 0.0F};
    geometry.width = 240.0F;
    geometry.height = 240.0F;
    geometry.darkPiece = false;
    ToolConfig config;
    config.id = 1;
    config.name = "Lados";
    config.type = ToolType::Polygon;
    config.geometryJson = toJson(ToolGeometry(geometry));
    config.toleranceMin = 0.0;
    config.toleranceMax = 100.0;

    const auto result = runTool(squareWithAHole(), centredFixture(), config);
    ASSERT_TRUE(result.isOk()) << result.error().message;
    EXPECT_EQ(result.value().kind, MeasuredKind::Count);
    const std::string text = formatMeasure(result.value(), kScale, LengthUnit::Millimeters);
    std::printf("  [unidades] lados del cuadrado: %s (%.0f)\n", text.c_str(),
                result.value().measured);
    EXPECT_EQ(text.find("mm"), std::string::npos)
        << "el recuento de lados volvió a salir en milímetros: " << text;
}
