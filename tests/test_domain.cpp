#include <gtest/gtest.h>

#include "domain/calibration.h"
#include "domain/capture_quality.h"
#include "domain/verdict.h"

using namespace pci::domain;

// --- Veredicto combinado ---

TEST(Verdict, AllOkGivesOk) {
    EmbeddingCheck embedding;
    embedding.evaluated = true;
    embedding.similarity = 0.99;
    embedding.anomalous = false;

    const auto verdict = combineVerdict(embedding, {{"caliper", true, 40.0, "d=40px"}});
    EXPECT_TRUE(verdict.ok);
    EXPECT_EQ(verdict.summary, "OK");
}

TEST(Verdict, AnomalousAppearanceGivesNg) {
    EmbeddingCheck embedding;
    embedding.evaluated = true;
    embedding.anomalous = true;

    const auto verdict = combineVerdict(embedding, {{"caliper", true, 40.0, ""}});
    EXPECT_FALSE(verdict.ok);
    EXPECT_NE(verdict.summary.find("anomalía"), std::string::npos);
}

TEST(Verdict, FailedToolGivesNgWithCount) {
    EmbeddingCheck embedding;
    embedding.evaluated = true;

    const auto verdict = combineVerdict(
        embedding, {{"a", false, 1.0, ""}, {"b", true, 2.0, ""}, {"c", false, 3.0, ""}});
    EXPECT_FALSE(verdict.ok);
    EXPECT_NE(verdict.summary.find("2 herramienta(s)"), std::string::npos);
}

TEST(Verdict, NoModelStillOkWithNote) {
    EmbeddingCheck embedding;  // evaluated = false
    embedding.note = "modelo no disponible";

    const auto verdict = combineVerdict(embedding, {{"caliper", true, 40.0, ""}});
    EXPECT_TRUE(verdict.ok);
    EXPECT_NE(verdict.summary.find("sin comparación"), std::string::npos);
}

TEST(Verdict, BothFailuresListed) {
    EmbeddingCheck embedding;
    embedding.evaluated = true;
    embedding.anomalous = true;

    const auto verdict = combineVerdict(embedding, {{"a", false, 0.0, ""}});
    EXPECT_FALSE(verdict.ok);
    EXPECT_NE(verdict.summary.find("anomalía"), std::string::npos);
    EXPECT_NE(verdict.summary.find("herramienta"), std::string::npos);
}

// --- Calibración de escala ---

TEST(Calibration, FromKnownLength) {
    // 200 px corresponden a 50 mm -> 0.25 mm/px.
    const auto calibration = calibrationFromKnownLength(200.0, 50.0, 640, 60.0);
    ASSERT_TRUE(calibration.valid());
    EXPECT_DOUBLE_EQ(calibration.mmPerPixel, 0.25);
    EXPECT_DOUBLE_EQ(calibration.toMm(100.0), 25.0);
    EXPECT_GT(calibration.cameraDistanceMm, 0.0);
}

TEST(Calibration, MethodsAreConsistent) {
    // La distancia estimada por el método A debe reproducir la misma escala
    // al usarla como entrada del método B (mismo FOV y ancho de imagen).
    const auto fromLength = calibrationFromKnownLength(200.0, 50.0, 640, 60.0);
    const auto fromDistance = calibrationFromCameraDistance(
        fromLength.cameraDistanceMm, 60.0, 640);
    ASSERT_TRUE(fromDistance.valid());
    EXPECT_NEAR(fromDistance.mmPerPixel, fromLength.mmPerPixel, 1e-9);
}

TEST(Calibration, InvalidInputsGiveUncalibrated) {
    EXPECT_FALSE(calibrationFromKnownLength(0.0, 50.0, 640, 60.0).valid());
    EXPECT_FALSE(calibrationFromKnownLength(200.0, 0.0, 640, 60.0).valid());
    EXPECT_FALSE(calibrationFromCameraDistance(0.0, 60.0, 640).valid());
    EXPECT_DOUBLE_EQ(estimateCameraDistanceMm(0.0, 60.0, 640), 0.0);
}

TEST(Calibration, FormatLengthWithAndWithoutScale) {
    ScaleCalibration none;
    EXPECT_NE(none.formatLength(42.3).find("px"), std::string::npos);
    EXPECT_EQ(none.formatLength(42.3).find("mm"), std::string::npos);

    const auto calibrated = calibrationFromKnownLength(100.0, 25.0, 640, 60.0);
    const std::string text = calibrated.formatLength(100.0);
    EXPECT_NE(text.find("25.00 mm"), std::string::npos);
    EXPECT_NE(text.find("px"), std::string::npos);

    // A partir de 100 mm se muestra en cm.
    const std::string big = calibrated.formatLength(600.0);  // 150 mm
    EXPECT_NE(big.find("15.00 cm"), std::string::npos);
}

TEST(Calibration, ResolutionMatchGuardsStaleScale) {
    ScaleCalibration cal;
    cal.mmPerPixel = 0.1;

    // Sin resolución conocida (calibración heredada): no se cuestiona.
    EXPECT_FALSE(cal.resolutionKnown());
    EXPECT_TRUE(cal.matchesResolution(640, 480));
    EXPECT_TRUE(cal.matchesResolution(1920, 1080));

    // Con resolución sellada: solo coincide con esa exacta.
    cal.calibratedWidth = 1280;
    cal.calibratedHeight = 720;
    EXPECT_TRUE(cal.resolutionKnown());
    EXPECT_TRUE(cal.matchesResolution(1280, 720));
    EXPECT_FALSE(cal.matchesResolution(640, 480));
    EXPECT_FALSE(cal.matchesResolution(1280, 721));
}

// --- Criterios de calidad de captura ---

namespace {

QualityMetrics goodMetrics() {
    QualityMetrics metrics;
    metrics.sharpness = 200.0;
    metrics.meanBrightness = 120.0;
    metrics.clippedFraction = 0.01;
    metrics.pieceFound = true;
    metrics.pieceTouchesBorder = false;
    return metrics;
}

}  // namespace

TEST(CaptureQuality, GoodCaptureAccepted) {
    EXPECT_TRUE(validateQuality(goodMetrics()).isOk());
}

TEST(CaptureQuality, RejectionsWithReasons) {
    auto noPiece = goodMetrics();
    noPiece.pieceFound = false;
    auto result = validateQuality(noPiece);
    ASSERT_FALSE(result.isOk());
    EXPECT_NE(result.error().message.find("pieza"), std::string::npos);

    auto cut = goodMetrics();
    cut.pieceTouchesBorder = true;
    result = validateQuality(cut);
    ASSERT_FALSE(result.isOk());
    EXPECT_NE(result.error().message.find("borde"), std::string::npos);

    auto blurry = goodMetrics();
    blurry.sharpness = 5.0;
    result = validateQuality(blurry);
    ASSERT_FALSE(result.isOk());
    EXPECT_NE(result.error().message.find("borrosa"), std::string::npos);

    auto dark = goodMetrics();
    dark.meanBrightness = 15.0;
    result = validateQuality(dark);
    ASSERT_FALSE(result.isOk());
    EXPECT_NE(result.error().message.find("oscura"), std::string::npos);

    auto clipped = goodMetrics();
    clipped.clippedFraction = 0.5;
    result = validateQuality(clipped);
    ASSERT_FALSE(result.isOk());
    EXPECT_NE(result.error().message.find("saturada"), std::string::npos);
}

// --- Reglas de posición del modo Especial (M4) ---

TEST(PositionRules, TolerancesOffMeanNothingIsWatched) {
    const auto check = evaluatePosition(120.0, 0.0, 45.0, 0.0, true);
    EXPECT_FALSE(check.evaluated);
    EXPECT_TRUE(check.ok);  // sin tolerancias no se juzga nada

    // Y no cambia el veredicto: sigue mandando lo de siempre.
    const auto verdict = combineVerdict({}, {}, check);
    EXPECT_TRUE(verdict.ok);
}

TEST(PositionRules, OffCenterPieceFailsWithReason) {
    const auto inside = evaluatePosition(8.0, 10.0, 0.0, 0.0, true);
    EXPECT_TRUE(inside.evaluated);
    EXPECT_TRUE(inside.radiusEvaluated);
    EXPECT_TRUE(inside.ok);

    const auto outside = evaluatePosition(14.5, 10.0, 0.0, 0.0, true);
    EXPECT_FALSE(outside.ok);
    const auto verdict = combineVerdict({}, {}, outside);
    EXPECT_FALSE(verdict.ok);
    EXPECT_NE(verdict.summary.find("descentrada"), std::string::npos) << verdict.summary;
}

TEST(PositionRules, RotatedPieceFailsAndSignIsIrrelevant) {
    const auto positive = evaluatePosition(0.0, 0.0, 7.5, 5.0, true);
    EXPECT_FALSE(positive.ok);
    // Girar en el otro sentido es igual de malo: se juzga el valor absoluto.
    const auto negative = evaluatePosition(0.0, 0.0, -7.5, 5.0, true);
    EXPECT_FALSE(negative.ok);
    EXPECT_TRUE(evaluatePosition(0.0, 0.0, -4.9, 5.0, true).ok);

    const auto verdict = combineVerdict({}, {}, negative);
    EXPECT_NE(verdict.summary.find("girada"), std::string::npos) << verdict.summary;
}

// Hallazgo de la revisión de diseño: en piezas casi simétricas el eje principal
// salta y daría NG falsos, así que la regla de giro se salta con nota.
TEST(PositionRules, UnreliableAxisSkipsAngleRuleInsteadOfFailing) {
    const auto check = evaluatePosition(0.0, 0.0, 179.0, 5.0, false);
    EXPECT_FALSE(check.angleEvaluated);
    EXPECT_FALSE(check.evaluated);  // no había otra regla activa
    EXPECT_TRUE(check.ok);
    EXPECT_FALSE(check.note.empty());

    // Con tolerancia de centrado activa, esa sí se sigue juzgando.
    const auto mixed = evaluatePosition(20.0, 10.0, 179.0, 5.0, false);
    EXPECT_TRUE(mixed.radiusEvaluated);
    EXPECT_FALSE(mixed.angleEvaluated);
    EXPECT_FALSE(mixed.ok);
    const auto verdict = combineVerdict({}, {}, mixed);
    EXPECT_NE(verdict.summary.find("descentrada"), std::string::npos) << verdict.summary;
    EXPECT_EQ(verdict.summary.find("girada"), std::string::npos) << verdict.summary;
}

TEST(PositionRules, PositionFailureCombinesWithToolFailures) {
    const std::vector<ToolCheck> tools{{"ancho", false, 12.0, "fuera"}};
    const auto position = evaluatePosition(30.0, 10.0, 0.0, 0.0, true);
    const auto verdict = combineVerdict({}, tools, position);
    EXPECT_FALSE(verdict.ok);
    EXPECT_NE(verdict.summary.find("herramienta"), std::string::npos) << verdict.summary;
    EXPECT_NE(verdict.summary.find("descentrada"), std::string::npos) << verdict.summary;
}

// ---------------------------------------------------------------------------
// Recuento de piezas (C5)
// ---------------------------------------------------------------------------

TEST(PieceCount, MissingAPieceIsNgAllByItself) {
    // El caso que motiva el ítem: una bandeja de seis con cinco tornillos daba
    // exactamente el mismo resultado que una llena.
    const auto check = evaluatePieceCount(6, 5);
    EXPECT_TRUE(check.evaluated);
    EXPECT_FALSE(check.ok);

    const auto verdict = combineVerdict({}, {}, {}, check);
    EXPECT_FALSE(verdict.ok) << "faltando una pieza no puede salir OK";
    // El motivo lleva los dos números: "faltan piezas" obligaría a ir a
    // contarlas a mano, que es el trabajo que esto ahorra.
    EXPECT_NE(verdict.summary.find("6"), std::string::npos) << verdict.summary;
    EXPECT_NE(verdict.summary.find("5"), std::string::npos) << verdict.summary;
}

TEST(PieceCount, TooManyPiecesIsAlsoNg) {
    const auto check = evaluatePieceCount(2, 3);
    EXPECT_FALSE(check.ok);
    EXPECT_FALSE(combineVerdict({}, {}, {}, check).ok);
}

TEST(PieceCount, TheRightNumberDoesNotComplain) {
    const auto check = evaluatePieceCount(6, 6);
    EXPECT_TRUE(check.evaluated);
    EXPECT_TRUE(check.ok);
    EXPECT_TRUE(combineVerdict({}, {}, {}, check).ok);
}

TEST(PieceCount, NotDeclaringANumberNeverWarns) {
    // La regla de siempre: un aviso que salta siempre es un aviso que se
    // aprende a ignorar. Sin número declarado, el recuento no se juzga.
    for (const int found : {0, 1, 7}) {
        const auto check = evaluatePieceCount(0, found);
        EXPECT_FALSE(check.evaluated) << "encontradas " << found;
        EXPECT_TRUE(check.ok);
        EXPECT_TRUE(combineVerdict({}, {}, {}, check).ok);
    }
    // Y el veredicto sin el parámetro se comporta igual que siempre.
    EXPECT_TRUE(combineVerdict({}, {}).ok);
}

TEST(PieceCount, TheCountTravelsInTheVerdict) {
    // Quien enseñe el resultado tiene que poder decir los dos números sin
    // volver a calcular nada.
    const auto verdict = combineVerdict({}, {}, {}, evaluatePieceCount(4, 2));
    EXPECT_EQ(verdict.count.expected, 4);
    EXPECT_EQ(verdict.count.found, 2);
    EXPECT_TRUE(verdict.count.evaluated);
}

// ---------------------------------------------------------------------------
// El brillo no es lo que decide si una imagen sirve para medir
// ---------------------------------------------------------------------------

TEST(CaptureQuality, TwoOppositeButLegitimateSetupsAreBothAccepted) {
    // El criterio de brillo medio rechazaba DOS montajes estándar y opuestos:
    //
    // - Contraluz (pieza clara sobre fondo negro), que es como se miden las
    //   siluetas: brillo medio 37 sobre un mínimo de 40.
    // - Pieza oscura sobre mesa blanca: si el brillo se midiera sobre la pieza,
    //   30 sobre el mismo mínimo.
    //
    // Los dos se miden perfectamente, así que ningún nivel medio puede aprobar a
    // los dos. Lo que hace inservible una imagen no es que sea oscura ni clara:
    // es que la pieza no se separe del fondo.
    using pci::domain::QualityMetrics;
    using pci::domain::validateQuality;

    QualityMetrics backlit;          // contraluz: pieza 220 sobre fondo 30
    backlit.sharpness = 180.0;
    backlit.meanBrightness = 37.0;   // medido sobre la escena del banco
    backlit.pieceFound = true;
    backlit.pieceContrast = 190.0;
    EXPECT_TRUE(validateQuality(backlit).isOk())
        << validateQuality(backlit).error().message;

    QualityMetrics darkOnWhite;      // pieza 30 sobre mesa 213
    darkOnWhite.sharpness = 180.0;
    darkOnWhite.meanBrightness = 213.0;
    darkOnWhite.pieceFound = true;
    darkOnWhite.pieceContrast = 153.0;
    EXPECT_TRUE(validateQuality(darkOnWhite).isOk())
        << validateQuality(darkOnWhite).error().message;
}

TEST(CaptureQuality, AnImageWhereThePieceDoesNotStandOutIsStillRejected) {
    // La otra mitad: soltar el criterio de brillo del todo dejaría pasar
    // exactamente lo que existe para parar. Cuando la pieza NO se separa, el
    // nivel medio vuelve a mandar.
    using pci::domain::QualityMetrics;
    using pci::domain::validateQuality;

    QualityMetrics underexposed;
    underexposed.sharpness = 180.0;
    underexposed.meanBrightness = 22.0;
    underexposed.pieceFound = true;
    underexposed.pieceContrast = 9.0;  // apenas se distingue del fondo
    const auto verdict = validateQuality(underexposed);
    ASSERT_FALSE(verdict.isOk());
    EXPECT_NE(verdict.error().message.find("oscura"), std::string::npos)
        << verdict.error().message;
    // Y el motivo dice las DOS cosas, porque con una sola el operador subiría la
    // luz sin entender que el problema es el contraste.
    EXPECT_NE(verdict.error().message.find("no se separa"), std::string::npos)
        << verdict.error().message;

    // Sin pieza detectada no hay contraste que valga: el nivel medio es lo único
    // que hay, y una imagen negra se rechaza igual.
    QualityMetrics blank;
    blank.sharpness = 180.0;
    blank.meanBrightness = 5.0;
    blank.pieceFound = false;
    EXPECT_FALSE(validateQuality(blank).isOk());
}
