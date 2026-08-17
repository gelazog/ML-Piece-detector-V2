// Banco del INFORME DE PIEZA: medirlo todo de un tirón a partir del contorno.
//
// Lo que se comprueba aquí no es que los cálculos sean correctos —cada uno tiene
// su propio banco— sino que el informe **conteste a la pregunta entera**: qué
// figura es, cuánto mide de contorno y qué cotas tiene, con unidades y sin
// cortar. Y sobre figuras de geometría CONOCIDA, para que el número esperado se
// pueda calcular a mano en vez de creerse el que salga.
#include <gtest/gtest.h>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "inspection_editor/piece_report.h"
#include "vision/pipeline.h"

using namespace pci::inspection;

namespace {

constexpr int kCanvas = 600;

// Todas las figuras se dibujan con LINE_8, sin suavizado. Con `LINE_AA` el
// rasterizado infla la silueta algo más de un píxel por lado —un Ø200 sale de
// contorno 202,87— y ese sesgo constante se lee como error dependiente del
// tamaño. Es un fallo que este proyecto ya pagó una vez.
cv::Mat disc(int radius = 150) {
    cv::Mat mask = cv::Mat::zeros(kCanvas, kCanvas, CV_8UC1);
    cv::circle(mask, {kCanvas / 2, kCanvas / 2}, radius, cv::Scalar(255), cv::FILLED,
               cv::LINE_8);
    return mask;
}

cv::Mat washer(int outer = 150, int inner = 60) {
    cv::Mat mask = disc(outer);
    cv::circle(mask, {kCanvas / 2, kCanvas / 2}, inner, cv::Scalar(0), cv::FILLED,
               cv::LINE_8);
    return mask;
}

cv::Mat regularPolygon(int sides, int radius = 160) {
    cv::Mat mask = cv::Mat::zeros(kCanvas, kCanvas, CV_8UC1);
    std::vector<cv::Point> vertices;
    for (int k = 0; k < sides; ++k) {
        const double angle = 2.0 * CV_PI * k / sides - CV_PI / 2.0;
        vertices.emplace_back(
            static_cast<int>(std::lround(kCanvas / 2 + radius * std::cos(angle))),
            static_cast<int>(std::lround(kCanvas / 2 + radius * std::sin(angle))));
    }
    cv::fillPoly(mask, std::vector<std::vector<cv::Point>>{vertices}, cv::Scalar(255),
                 cv::LINE_8);
    return mask;
}

// Pieza clara sobre fondo oscuro: el montaje a contraluz que estas medidas
// piden.
cv::Mat sceneFor(const cv::Mat& mask) {
    cv::Mat gray(mask.size(), CV_8UC1, cv::Scalar(30));
    gray.setTo(cv::Scalar(220), mask);
    return gray;
}

PieceReport reportOf(const cv::Mat& mask, double mmPerPixel = 0.0) {
    return measureWholePiece(sceneFor(mask), mask, {}, mmPerPixel);
}

const MeasurementRow* rowNamed(const PieceReport& report, const std::string& needle) {
    for (const auto& row : report.rows) {
        if (row.tool.find(needle) != std::string::npos) {
            return &row;
        }
    }
    return nullptr;
}

int countGroup(const PieceReport& report, const std::string& group) {
    return static_cast<int>(std::count_if(
        report.rows.begin(), report.rows.end(),
        [&group](const MeasurementRow& row) { return row.group == group; }));
}

}  // namespace

TEST(PieceReport, TheContourFactsComeOutWhateverTheShapeIs) {
    // Perímetro, área, envolvente, agujeros, circularidad y tramos: no dependen
    // de haber reconocido la figura, son lo que el contorno ES. Y son justo los
    // que hasta ahora no se podían leer en ningún sitio salvo un rótulo en una
    // esquina del editor.
    for (const auto& mask : {disc(), washer(), regularPolygon(6), regularPolygon(3)}) {
        const auto report = reportOf(mask);
        ASSERT_TRUE(report.ok) << report.problem;
        for (const char* name :
             {"Perímetro", "Área", "Largo total", "Ancho total", "Agujeros",
              "Circularidad", "Tramos rectos", "Arcos"}) {
            EXPECT_NE(rowNamed(report, name), nullptr)
                << "falta «" << name << "» en el informe";
        }
    }
}

TEST(PieceReport, TheNumbersAreTheOnesTheFigureShouldGive) {
    // Geometría conocida: un disco de radio 150 tiene perímetro 2·pi·150 = 942,5
    // y área pi·150² = 70 686. Se comprueban contra la fórmula, no contra lo que
    // salga.
    constexpr int kRadius = 150;
    const auto report = reportOf(disc(kRadius));
    ASSERT_TRUE(report.ok) << report.problem;

    const auto* perimeter = rowNamed(report, "Perímetro");
    const auto* area = rowNamed(report, "Área");
    const auto* circularity = rowNamed(report, "Circularidad");
    ASSERT_NE(perimeter, nullptr);
    ASSERT_NE(area, nullptr);
    ASSERT_NE(circularity, nullptr);

    const double expectedPerimeter = 2.0 * CV_PI * kRadius;
    const double expectedArea = CV_PI * kRadius * kRadius;
    std::printf("  [informe] disco r=%d: perimetro %.1f (teorico %.1f), area %.0f "
                "(teorico %.0f), circularidad %.3f\n",
                kRadius, perimeter->value, expectedPerimeter, area->value, expectedArea,
                circularity->value);

    // Un contorno rasterizado no es la circunferencia ideal; el 2 % cubre la
    // discretización sin dejar pasar un error de fórmula.
    EXPECT_NEAR(perimeter->value, expectedPerimeter, expectedPerimeter * 0.02);
    EXPECT_NEAR(area->value, expectedArea, expectedArea * 0.02);
    // Un disco es lo más circular que hay: su circularidad tiene que rondar 1.
    EXPECT_GT(circularity->value, 0.97);

    // Y el largo y el ancho de un disco son su diámetro, los dos.
    const auto* longSide = rowNamed(report, "Largo total");
    const auto* shortSide = rowNamed(report, "Ancho total");
    ASSERT_NE(longSide, nullptr);
    ASSERT_NE(shortSide, nullptr);
    EXPECT_NEAR(longSide->value, 2.0 * kRadius, 4.0);
    EXPECT_NEAR(shortSide->value, 2.0 * kRadius, 4.0);
}

TEST(PieceReport, WhatItMeasuresDependsOnWhatTheShapeIs) {
    // El corazón de lo que se pidió: que las cotas salgan DEL CONTORNO. Un
    // disco no tiene lados que medir y un hexágono no tiene diámetro; pedirle
    // las mismas cotas a los dos daría números que no significan nada en uno de
    // ellos.
    const auto round = reportOf(disc());
    ASSERT_TRUE(round.ok) << round.problem;
    EXPECT_EQ(round.shape.kind, pci::vision::ShapeKind::Circle);
    EXPECT_EQ(round.headline, "Pieza redonda");
    EXPECT_NE(rowNamed(round, "Ø"), nullptr) << "a una pieza redonda le falta el diámetro";
    EXPECT_NE(rowNamed(round, "Redondez"), nullptr);

    const auto hex = reportOf(regularPolygon(6));
    ASSERT_TRUE(hex.ok) << hex.problem;
    EXPECT_EQ(hex.shape.kind, pci::vision::ShapeKind::Polygon);
    EXPECT_EQ(hex.headline, "Polígono de 6 lados");
    EXPECT_NE(rowNamed(hex, "Lado 1"), nullptr) << "a un polígono le faltan sus lados";
    EXPECT_NE(rowNamed(hex, "Ángulo 1"), nullptr);

    const auto ring = reportOf(washer());
    ASSERT_TRUE(ring.ok) << ring.problem;
    EXPECT_EQ(ring.shape.kind, pci::vision::ShapeKind::Ring);
    EXPECT_EQ(ring.headline, "Arandela");
    EXPECT_NE(rowNamed(ring, "Ø exterior"), nullptr);
    EXPECT_NE(rowNamed(ring, "Ø interior"), nullptr);
    // Y su agujero se cuenta como agujero.
    const auto* holes = rowNamed(ring, "Agujeros");
    ASSERT_NE(holes, nullptr);
    EXPECT_DOUBLE_EQ(holes->value, 1.0);
}

TEST(PieceReport, TheReportIsNotCutBecauseItIsAReportAndNotAReviewList) {
    // La diferencia con el diálogo de propuestas, y es deliberada: aquel se
    // corta en doce porque es una lista que hay que revisar a mano; esto es un
    // informe, y un informe cortado contesta a medias.
    //
    // Un dodecágono da bastantes más de doce cotas entre lados, ángulos y
    // envolvente: si el informe estuviera cortado, se vería aquí.
    const auto report = reportOf(regularPolygon(12));
    ASSERT_TRUE(report.ok) << report.problem;
    const int dimensions = countGroup(report, kGroupDimension);
    std::printf("  [informe] dodecagono: %d hechos de contorno y %d cotas\n",
                countGroup(report, kGroupContour), dimensions);
    EXPECT_GT(dimensions, 12)
        << "el informe se cortó en el tope del diálogo de propuestas";
}

TEST(PieceReport, EveryRowKnowsWhichGroupItBelongsTo) {
    // Mezclar un hecho del contorno con una cota sin distinguirlos invita a
    // buscarle tolerancia a un área que nadie ha declarado.
    const auto report = reportOf(regularPolygon(6));
    ASSERT_TRUE(report.ok) << report.problem;
    for (const auto& row : report.rows) {
        EXPECT_TRUE(row.group == kGroupContour || row.group == kGroupDimension)
            << "la fila «" << row.tool << "» no dice de qué grupo es";
    }
    EXPECT_GT(countGroup(report, kGroupContour), 0);
    EXPECT_GT(countGroup(report, kGroupDimension), 0);
    EXPECT_EQ(static_cast<int>(report.contourFactCount()),
              countGroup(report, kGroupContour));

    // Los hechos del contorno van PRIMERO: es lo que se lee de un vistazo antes
    // de bajar al detalle de cada cota.
    int lastFact = -1;
    int firstDimension = -1;
    for (int i = 0; i < static_cast<int>(report.rows.size()); ++i) {
        if (report.rows[static_cast<std::size_t>(i)].group == kGroupContour) {
            lastFact = i;
        } else if (firstDimension < 0) {
            firstDimension = i;
        }
    }
    ASSERT_GE(lastFact, 0);
    ASSERT_GE(firstDimension, 0);
    EXPECT_LT(lastFact, firstDimension)
        << "las cotas se colaron entre los hechos del contorno";
}

TEST(PieceReport, EveryRowCarriesItsUnitAndTheAreaIsSquared) {
    // Ninguna fila puede salir sin unidad, y el área tiene que entrar con la
    // escala AL CUADRADO. Es la lección que costó el rotulado de las medidas.
    constexpr double kScale = 0.25;  // mm por píxel
    const auto report = reportOf(disc(150), kScale);
    ASSERT_TRUE(report.ok) << report.problem;
    for (const auto& row : report.rows) {
        EXPECT_FALSE(row.unit.empty()) << "la fila «" << row.tool << "» salió sin unidad";
    }

    // Una sola unidad de longitud en TODO el informe. El primer intento de este
    // test esperaba «mm» en el perímetro y salía «cm»: con 941 px a 0,25 mm/px
    // son 235 mm, y el modo automático pasa a cm por encima de 10 cm. La regla
    // era correcta para una etiqueta suelta y mala para una tabla — una tabla
    // existe para comparar filas, y un perímetro en cm junto a un lado en mm
    // obliga a convertir de cabeza en cada renglón. Ahora la unidad se resuelve
    // una vez, con la medida mayor de la pieza.
    const auto* area = rowNamed(report, "Área");
    const auto* perimeter = rowNamed(report, "Perímetro");
    ASSERT_NE(area, nullptr);
    ASSERT_NE(perimeter, nullptr);
    EXPECT_EQ(perimeter->unit, "cm") << "235 mm pasan de 10 cm: el informe entero va en cm";
    EXPECT_EQ(area->unit, "cm²") << "el área tiene que seguir a la misma unidad";
    std::string lengthUnit;
    for (const auto& row : report.rows) {
        if (row.unit == "mm" || row.unit == "cm" || row.unit == "px") {
            if (lengthUnit.empty()) {
                lengthUnit = row.unit;
            }
            EXPECT_EQ(row.unit, lengthUnit)
                << "la fila «" << row.tool << "» usa otra unidad de longitud que el resto";
        }
    }
    // El área entra con la escala AL CUADRADO. Con la lineal saldría un número
    // plausible y equivocado por un factor igual a la propia escala.
    EXPECT_NEAR(area->value, area->pixels * kScale * kScale / 100.0, area->value * 1e-6);

    const auto* holes = rowNamed(report, "Agujeros");
    const auto* circularity = rowNamed(report, "Circularidad");
    ASSERT_NE(holes, nullptr);
    ASSERT_NE(circularity, nullptr);
    EXPECT_EQ(holes->unit, "n") << "un recuento no lleva unidades de longitud";
    EXPECT_EQ(circularity->unit, "—") << "una fracción no lleva unidad";
}

TEST(PieceReport, NothingIsMarkedOkBecauseNothingHasBeenJudgedYet) {
    // Una cota recién medida está dentro de su propia tolerancia por
    // construcción: la banda se sugirió A PARTIR de ella. Marcarla «OK» sería
    // dar por comprobado lo que nadie ha comprobado.
    const auto report = reportOf(regularPolygon(6));
    ASSERT_TRUE(report.ok) << report.problem;
    for (const auto& row : report.rows) {
        EXPECT_EQ(row.state, "—")
            << "la fila «" << row.tool << "» se presenta como comprobada";
    }
}

TEST(PieceReport, OnlyWhatCanBeWatchedIsOfferedToBeWatched) {
    // No hay herramienta que mida «el área de la pieza», así que ofrecer
    // vigilarla sería prometer algo que luego no se puede cumplir. Las
    // vigilables son exactamente las cotas.
    const auto report = reportOf(regularPolygon(6));
    ASSERT_TRUE(report.ok) << report.problem;
    EXPECT_EQ(static_cast<int>(report.watchable.size()),
              countGroup(report, kGroupDimension));
    for (const auto& proposal : report.watchable) {
        EXPECT_FALSE(proposal.config.name.empty());
        EXPECT_FALSE(proposal.reason.empty()) << "una propuesta sin porqué no se revisa";
    }
}

TEST(PieceReport, WithoutAPieceItSaysWhyInsteadOfComingBackEmpty) {
    // Un informe vacío sin motivo se confunde con una pieza sin cotas, que es
    // lo contrario de lo que pasa.
    const cv::Mat empty = cv::Mat::zeros(kCanvas, kCanvas, CV_8UC1);
    const auto report = measureWholePiece(sceneFor(empty), empty, {});
    EXPECT_FALSE(report.ok);
    EXPECT_FALSE(report.problem.empty()) << "no se pudo medir y no se dijo por qué";
    EXPECT_TRUE(report.rows.empty());

    const auto nothing = measureWholePiece({}, {}, {});
    EXPECT_FALSE(nothing.ok);
    EXPECT_FALSE(nothing.problem.empty());
}

TEST(PieceReport, WithoutCalibrationItGivesPixelsAndSaysSo) {
    // Inventar milímetros sin escala sería la peor salida de todas.
    const auto report = reportOf(disc(150));
    ASSERT_TRUE(report.ok) << report.problem;
    const auto* perimeter = rowNamed(report, "Perímetro");
    const auto* area = rowNamed(report, "Área");
    ASSERT_NE(perimeter, nullptr);
    ASSERT_NE(area, nullptr);
    EXPECT_EQ(perimeter->unit, "px");
    EXPECT_EQ(area->unit, "px²");
}

TEST(PieceReport, TheWholeReportSurvivesGoingOutAsCsv) {
    // El informe existe para poder sacarlo. Si el CSV perdiera filas o
    // columnas, el informe no serviría para lo que se hizo.
    const auto report = reportOf(regularPolygon(6), 0.25);
    ASSERT_TRUE(report.ok) << report.problem;
    const std::string csv = measurementsToCsv(report.rows);

    int newlines = 0;
    for (const char c : csv) {
        if (c == '\n') {
            ++newlines;
        }
    }
    EXPECT_EQ(newlines, static_cast<int>(report.rows.size()) + 1)
        << "el CSV no tiene una línea por fila más la cabecera";
    EXPECT_NE(csv.find("grupo"), std::string::npos) << "falta la columna de grupo";
    EXPECT_NE(csv.find(kGroupContour), std::string::npos);
    EXPECT_NE(csv.find(kGroupDimension), std::string::npos);
}

// ---------------------------------------------------------------------------
// Los agujeros, que la cadena real se comía
// ---------------------------------------------------------------------------

namespace {

// Una arandela como IMAGEN, no como máscara: pieza oscura sobre fondo claro,
// con su agujero del color del fondo. Es lo que llega de una cámara, y es la
// diferencia que este banco no estaba probando.
cv::Mat washerImage(int outer = 190, int inner = 80) {
    cv::Mat image(kCanvas, kCanvas, CV_8UC1, cv::Scalar(205));
    cv::circle(image, {kCanvas / 2, kCanvas / 2}, outer, cv::Scalar(55), cv::FILLED,
               cv::LINE_8);
    cv::circle(image, {kCanvas / 2, kCanvas / 2}, inner, cv::Scalar(205), cv::FILLED,
               cv::LINE_8);
    return image;
}

}  // namespace

TEST(PieceReport, AWasherKeepsItsHoleThroughTheRealPipeline) {
    // EL FALLO, y solo se veía sondeando una imagen de verdad: `analyzeFrame`
    // devuelve la máscara con el contorno exterior RELLENO —a propósito, para
    // que los blobs de ruido no sesguen el fixture— y los tres sitios que miden
    // le pasaban esa máscara. Resultado: una arandela salía clasificada como
    // «círculo», sin diámetro interior y con cero agujeros.
    //
    // El banco no lo veía porque aquí las máscaras se dibujan a mano y
    // conservan su agujero. Este test entra por donde entra la aplicación.
    const cv::Mat image = washerImage();
    const auto analysis = pci::vision::analyzeFrame(image, {});
    ASSERT_TRUE(analysis.isOk()) << analysis.error().message;

    // Así estaba: la máscara del análisis no tiene agujero.
    const auto filled = measureWholePiece(image, analysis.value().mask, {});
    ASSERT_TRUE(filled.ok) << filled.problem;
    const auto* holesWhenFilled = rowNamed(filled, "Agujeros");
    ASSERT_NE(holesWhenFilled, nullptr);
    EXPECT_DOUBLE_EQ(holesWhenFilled->value, 0.0)
        << "la máscara rellena ya trae agujeros: este test dejó de reproducir el fallo";
    EXPECT_EQ(filled.shape.kind, pci::vision::ShapeKind::Circle)
        << "con la máscara rellena la arandela tenía que salir como disco";

    // Y así queda: con los agujeros devueltos, es una arandela y tiene sus dos
    // diámetros.
    const cv::Mat withHoles = pci::vision::pieceMaskWithHoles(image, analysis.value().mask);
    const auto report = measureWholePiece(image, withHoles, {});
    ASSERT_TRUE(report.ok) << report.problem;
    EXPECT_EQ(report.shape.kind, pci::vision::ShapeKind::Ring);
    EXPECT_EQ(report.headline, "Arandela");

    const auto* holes = rowNamed(report, "Agujeros");
    ASSERT_NE(holes, nullptr);
    EXPECT_DOUBLE_EQ(holes->value, 1.0);

    const auto* outerDiameter = rowNamed(report, "Ø exterior");
    const auto* innerDiameter = rowNamed(report, "Ø interior");
    ASSERT_NE(outerDiameter, nullptr) << "sin Ø exterior no es una arandela medida";
    ASSERT_NE(innerDiameter, nullptr) << "el Ø interior es la cota que el fallo se comía";
    std::printf("  [agujeros] dibujada Ø380/Ø160 -> medida Ø%.1f/Ø%.1f px\n",
                outerDiameter->value, innerDiameter->value);
    // Se dibujó con radios 190 y 80: Ø 380 y Ø 160.
    EXPECT_NEAR(outerDiameter->value, 380.0, 4.0);
    EXPECT_NEAR(innerDiameter->value, 160.0, 6.0);
}

TEST(PieceReport, RestoringTheHolesNeverLosesThePiece) {
    // La regla de seguridad del cruce: si la segunda segmentación no viera lo
    // mismo que la primera, quedarse sin pieza sería mucho peor que quedarse
    // sin agujeros. Se comprueba con una configuración de segmentación que NO
    // corresponde a la imagen.
    const cv::Mat image = washerImage();
    const auto analysis = pci::vision::analyzeFrame(image, {});
    ASSERT_TRUE(analysis.isOk()) << analysis.error().message;

    pci::vision::SegmentationOptions wrong;
    wrong.manualThreshold = 250;  // casi todo cae del mismo lado
    wrong.polarity = pci::vision::SegmentationPolarity::LightPiece;
    const cv::Mat mask = pci::vision::pieceMaskWithHoles(image, analysis.value().mask, wrong);
    EXPECT_GT(cv::countNonZero(mask), 0) << "el cruce se quedó sin pieza";
    EXPECT_GE(cv::countNonZero(mask), cv::countNonZero(analysis.value().mask) / 2);

    // Y con una imagen vacía o de otro tamaño, devuelve lo que había.
    EXPECT_EQ(cv::countNonZero(pci::vision::pieceMaskWithHoles({}, analysis.value().mask)),
              cv::countNonZero(analysis.value().mask));
}
