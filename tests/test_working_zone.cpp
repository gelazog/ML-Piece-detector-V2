// Banco de pruebas de la ZONA DE TRABAJO: que recortar no cambie ninguna
// medida, que el seguimiento siga a la pieza y que se rinda por el motivo
// correcto.
//
// Existe porque la zona es la parte del programa peor cubierta: se arreglaron
// tres fallos reales y solo la regla pura quedó en tests, porque `MainWindow`
// no tiene banco de pruebas.
#include <gtest/gtest.h>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "vision/auto_roi.h"
#include "vision/pipeline.h"

using namespace pci::vision;

namespace {

constexpr int kSequenceFrames = 20;

const cv::Size kFrameSize(1280, 720);

double frameArea() {
    return static_cast<double>(kFrameSize.width) * kFrameSize.height;
}

// Escena sintética: una pieza en «L» clara y texturada sobre fondo oscuro.
//
// A color y con textura a propósito. El recorte canónico se compara píxel a
// píxel, y sobre una pieza de un solo tono esa comparación pasaría igual aunque
// el recorte estuviera desplazado o escalado: lo que se compararía sería una
// mancha uniforme contra otra. La «L» además da un contorno con esquinas, no un
// rectángulo cuya simetría perdona errores.
cv::Mat sceneWithPiece(const cv::Rect& piece, const cv::Size& frameSize = kFrameSize) {
    cv::Mat scene(frameSize, CV_8UC3, cv::Scalar(26, 22, 20));

    const int armH = piece.height * 35 / 100;  // grosor del brazo horizontal
    const int armW = piece.width * 40 / 100;   // anchura del brazo vertical
    const std::vector<std::vector<cv::Point>> polygon{{
        {piece.x, piece.y},
        {piece.x + piece.width, piece.y},
        {piece.x + piece.width, piece.y + armH},
        {piece.x + armW, piece.y + armH},
        {piece.x + armW, piece.y + piece.height},
        {piece.x, piece.y + piece.height},
    }};
    cv::fillPoly(scene, polygon, cv::Scalar(206, 212, 218));

    // Marcas interiores: bastante más oscuras que la pieza para que se vean en
    // el recorte canónico, y bastante más claras que el fondo para no abrir
    // agujeros en la máscara — la textura tiene que cambiar el CONTENIDO del
    // recorte sin cambiar la segmentación.
    for (int i = 1; i <= 3; ++i) {
        const int x = piece.x + i * piece.width / 5;
        cv::line(scene, {x, piece.y + 2}, {x, piece.y + armH - 2}, cv::Scalar(140, 146, 150),
                 3);
    }
    cv::circle(scene, {piece.x + armW / 2, piece.y + piece.height - armH / 2},
               std::max(3, armW / 4), cv::Scalar(150, 140, 138), cv::FILLED);
    return scene;
}

// Escena del mismo fondo pero sin ninguna pieza: la mano que retira la pieza,
// el hueco entre dos piezas de la línea.
cv::Mat emptyScene(const cv::Size& frameSize = kFrameSize) {
    return cv::Mat(frameSize, CV_8UC3, cv::Scalar(26, 22, 20));
}

// La pieza recorre el campo en diagonal sin llegar a tocar el borde de la
// imagen: tocarlo sería otra prueba (la de rendirse), no esta.
cv::Rect pieceAtStep(int step) {
    return {150 + step * 45, 120 + step * 22, 170, 130};
}

cv::Rect boundsOf(const PieceAnalysis& analysis) {
    return cv::boundingRect(analysis.contour.points);
}

// Píxeles distintos entre dos recortes canónicos.
//
// `countNonZero` no acepta matrices multicanal y lanza; `reshape(1)` las mira
// como un solo canal con tres veces más columnas, que para «cuántas muestras
// difieren» es exactamente lo que se quiere contar.
int differingPixels(const cv::Mat& a, const cv::Mat& b) {
    if (a.size() != b.size() || a.type() != b.type()) {
        return -1;  // tamaños distintos: ni siquiera son comparables
    }
    cv::Mat diff;
    cv::absdiff(a, b, diff);
    return cv::countNonZero(diff.reshape(1));
}

// Todas las medidas que la aplicación saca de un frame, comparadas con
// exactitud. Se acumula el peor desfase del fixture para poder imprimirlo.
void expectSameMeasurements(const PieceAnalysis& reference, const PieceAnalysis& measured,
                            const std::string& where, double* worstFixturePx) {
    SCOPED_TRACE(where);

    ASSERT_EQ(reference.contour.points.size(), measured.contour.points.size())
        << "el contorno ni siquiera tiene el mismo número de puntos";
    int wrongPoints = 0;
    for (std::size_t i = 0; i < reference.contour.points.size(); ++i) {
        if (reference.contour.points[i] != measured.contour.points[i]) {
            ++wrongPoints;
        }
    }
    EXPECT_EQ(wrongPoints, 0) << "puntos del contorno desplazados por el recorte";
    EXPECT_DOUBLE_EQ(reference.contour.area, measured.contour.area);
    EXPECT_DOUBLE_EQ(reference.contour.perimeter, measured.contour.perimeter);

    // El fixture es lo que coloca cada herramienta sobre la pieza: si se moviera
    // medio píxel al recortar, todas las medidas se moverían con él.
    //
    // Cota medida, no inventada: los dos caminos dan el MISMO número salvo por
    // el redondeo de sumarle la esquina del recorte en `float`. Medido 0 px con
    // recortes alineados y 1.5e-5 px sobre la secuencia; se fija en 1e-3, casi
    // dos órdenes por encima de lo peor visto y muy por debajo del píxel, que es
    // lo que haría falta para que una medida cambiara.
    constexpr double kFixtureTolerancePx = 1e-3;
    const double dx = std::abs(static_cast<double>(reference.fixture.origin.x) -
                               measured.fixture.origin.x);
    const double dy = std::abs(static_cast<double>(reference.fixture.origin.y) -
                               measured.fixture.origin.y);
    *worstFixturePx = std::max(*worstFixturePx, std::max(dx, dy));
    EXPECT_LT(dx, kFixtureTolerancePx) << "el origen del fixture se movió en X";
    EXPECT_LT(dy, kFixtureTolerancePx) << "el origen del fixture se movió en Y";
    EXPECT_DOUBLE_EQ(reference.fixture.angleDeg, measured.fixture.angleDeg);
    EXPECT_DOUBLE_EQ(reference.fixture.anisotropy, measured.fixture.anisotropy);

    // El recorte canónico alimenta los embeddings: dos recortes que difieran en
    // un píxel son dos piezas distintas para la fase de aprendizaje.
    EXPECT_EQ(differingPixels(reference.normalized, measured.normalized), 0)
        << "el recorte canónico cambió al recortar";

    // La máscara vuelve en coordenadas de la imagen completa en los dos casos.
    EXPECT_EQ(differingPixels(reference.mask, measured.mask), 0)
        << "la máscara no volvió al marco de la imagen completa";
}

}  // namespace

// 1. La propiedad que sostiene toda la función.
//
// Si recortar moviera las medidas, la zona de trabajo no sería una
// optimización: sería una optimización que estropea la inspección. El operador
// vería las mismas herramientas dando números distintos según una casilla que
// solo debería afectar a cuánto tarda cada frame. Por eso se compara con
// exactitud —contorno punto a punto y recorte canónico píxel a píxel— y no con
// una tolerancia generosa que dejaría pasar justo el fallo que importa.
TEST(WorkingZone, CroppingChangesNoMeasurementAtAll) {
    const cv::Rect piece(480, 260, 200, 160);
    const cv::Mat scene = sceneWithPiece(piece);

    const auto whole = analyzeFrame(scene, PipelineConfig{});
    ASSERT_TRUE(whole.isOk()) << whole.error().message;

    double worstFixturePx = 0.0;
    // Varios márgenes: uno tan justo que roza a la pieza, uno holgado y uno que
    // deja fuera la mitad del frame. Los tres CONTIENEN a la pieza, que es la
    // única condición que la zona promete respetar.
    for (const int margin : {12, 40, 140}) {
        const cv::Rect roi =
            cv::Rect(piece.x - margin, piece.y - margin, piece.width + 2 * margin,
                     piece.height + 2 * margin) &
            cv::Rect(0, 0, scene.cols, scene.rows);
        ASSERT_LT(roi.area(), scene.cols * scene.rows)
            << "un recorte del tamaño del frame no probaría nada";

        PipelineConfig config;
        config.roi = roi;
        const auto cropped = analyzeFrame(scene, config);
        ASSERT_TRUE(cropped.isOk()) << "margen " << margin << ": " << cropped.error().message;

        expectSameMeasurements(whole.value(), cropped.value(),
                               "margen de " + std::to_string(margin) + " px",
                               &worstFixturePx);
    }
    std::printf("  [zona] peor desfase del fixture al recortar: %.6f px\n", worstFixturePx);
}

// 2. Que el seguimiento siga a la pieza de verdad, alimentado como lo alimenta
// la ventana: con el resultado del análisis del frame anterior.
TEST(WorkingZone, TheCropFollowsThePieceThroughTheWholeSequence) {
    AutoRoiTracker tracker;
    double fractionSum = 0.0;
    int croppedFrames = 0;

    for (int step = 0; step < kSequenceFrames; ++step) {
        const cv::Mat scene = sceneWithPiece(pieceAtStep(step));
        SCOPED_TRACE("frame " + std::to_string(step));

        // La verdad de dónde está la pieza sale de analizar el frame entero. No
        // vale usar los límites del análisis recortado para comprobar que el
        // recorte contiene a la pieza: si el recorte la hubiera cortado, esos
        // límites serían los de lo que quedó dentro y el test se daría la razón
        // a sí mismo.
        const auto truth = analyzeFrame(scene, PipelineConfig{});
        ASSERT_TRUE(truth.isOk()) << truth.error().message;

        PipelineConfig config;
        config.roi = tracker.roi();
        const auto analysis = analyzeFrame(scene, config);
        ASSERT_TRUE(analysis.isOk())
            << "el recorte perdió la pieza: " << analysis.error().message;

        if (config.roi.area() > 0) {
            EXPECT_EQ(config.roi & boundsOf(truth.value()), boundsOf(truth.value()))
                << "el recorte cortaba a la pieza";
            fractionSum += static_cast<double>(config.roi.area()) / frameArea();
            ++croppedFrames;
        }

        tracker.update(true, boundsOf(analysis.value()), scene.size());
        EXPECT_EQ(tracker.lastGiveUp(), AutoRoiGiveUp::None)
            << "se rindió sin motivo: " << giveUpReason(tracker.lastGiveUp());
    }

    // El primer frame se analiza entero por fuerza (aún no hay recorte); del
    // segundo en adelante tiene que haber zona, o no hay seguimiento que probar.
    ASSERT_EQ(croppedFrames, kSequenceFrames - 1)
        << "el seguimiento se quedó sin recorte en algún frame";
    const double meanFraction = fractionSum / croppedFrames;
    std::printf("  [zona] area media del recorte: %.1f %% del frame (%d frames)\n",
                100.0 * meanFraction, croppedFrames);
    // Medido: 9.5 % del frame. La cota se fija en el doble de lo medido —por
    // encima de un quinto de la imagen el recorte ya no ahorraría nada que se
    // note, y el margen deja sitio a que la segmentación cambie un poco.
    EXPECT_LT(meanFraction, 0.20) << "el recorte no está ahorrando trabajo";
}

// 3a. Se rinde cuando la pieza deja de estar, y lo dice.
TEST(WorkingZone, GivesUpAsPieceLostWhenThePieceLeavesTheScene) {
    AutoRoiTracker tracker;
    const cv::Mat withPiece = sceneWithPiece(pieceAtStep(6));
    const auto first = analyzeFrame(withPiece, PipelineConfig{});
    ASSERT_TRUE(first.isOk()) << first.error().message;
    tracker.update(true, boundsOf(first.value()), withPiece.size());
    ASSERT_TRUE(tracker.tracking());

    // Se retira la pieza. El análisis del recorte falla de verdad: no hay nada
    // que segmentar dentro de la zona.
    const cv::Mat gone = emptyScene();
    int lostFrames = 0;
    while (tracker.tracking() && lostFrames < 6) {
        PipelineConfig config;
        config.roi = tracker.roi();
        const auto analysis = analyzeFrame(gone, config);
        EXPECT_FALSE(analysis.isOk())
            << "no hay pieza en la escena y el análisis dice haber encontrado una";
        tracker.update(false, cv::Rect(), gone.size());
        ++lostFrames;
        if (lostFrames <= 2) {
            EXPECT_TRUE(tracker.tracking())
                << "un parpadeo de " << lostFrames << " frame(s) no puede tirar la zona";
        }
    }

    EXPECT_FALSE(tracker.tracking());
    EXPECT_EQ(tracker.lastGiveUp(), AutoRoiGiveUp::PieceLost);
    EXPECT_EQ(lostFrames, 3) << "se toleran dos frames sin pieza, ni uno más";
}

// 3b. Se rinde cuando la pieza se sale del recorte, y el motivo que reporta es
// «se sale», no «saltó el área».
//
// Este es el caso que fija el ORDEN de las dos guardas: una pieza que se sale
// aparece cortada, y al aparecer cortada su área se desploma. Las dos
// condiciones se cumplen a la vez, así que el motivo que se le enseña al
// operador depende de cuál se mire primero — y el bueno es la causa, no el
// síntoma. Aquí la pieza se sale de verdad: no se le inventan los límites al
// seguimiento, se le da lo que el análisis ve dentro del recorte.
TEST(WorkingZone, GivesUpAsEscapingAndNotAsAreaJumpWhenThePieceLeavesTheCrop) {
    AutoRoiTracker tracker;
    const cv::Rect start(500, 300, 160, 120);
    const cv::Mat before = sceneWithPiece(start);
    const auto first = analyzeFrame(before, PipelineConfig{});
    ASSERT_TRUE(first.isOk()) << first.error().message;
    tracker.update(true, boundsOf(first.value()), before.size());
    const cv::Rect roi = tracker.roi();
    ASSERT_GT(roi.area(), 0);

    // Un salto grande hacia la izquierda: la pieza se va y el recorte solo
    // alcanza a ver una esquina de ella.
    const cv::Mat after = sceneWithPiece(cv::Rect(320, 300, 160, 120));
    PipelineConfig config;
    config.roi = roi;
    const auto escaping = analyzeFrame(after, config);
    ASSERT_TRUE(escaping.isOk()) << escaping.error().message;

    const cv::Rect seen = boundsOf(escaping.value());
    ASSERT_LT(seen.area() * 2, boundsOf(first.value()).area())
        << "el trozo visible tiene que ser menos de la mitad, o no habría salto de área "
           "que desempatar";

    tracker.update(true, seen, after.size());
    EXPECT_FALSE(tracker.tracking());
    EXPECT_EQ(tracker.lastGiveUp(), AutoRoiGiveUp::PieceEscaping)
        << "reportó el síntoma en vez de la causa: " << giveUpReason(tracker.lastGiveUp());

    // Y rendirse cuesta exactamente un frame: el siguiente ya se analiza entero
    // y vuelve a ver la pieza completa.
    PipelineConfig recovered;
    recovered.roi = tracker.roi();
    EXPECT_EQ(recovered.roi.area(), 0);
    const auto again = analyzeFrame(after, recovered);
    ASSERT_TRUE(again.isOk()) << again.error().message;
    EXPECT_GT(boundsOf(again.value()).area(), seen.area() * 2)
        << "al volver al frame entero tiene que reaparecer la pieza completa";
}

// 3c. Se rinde cuando le cambian la pieza dentro del recorte, y lo llama por su
// nombre. La pieza nueva cabe entera en el recorte de la anterior: si asomara,
// saltaría antes la guarda de «se está saliendo» y este test estaría probando
// otra cosa.
TEST(WorkingZone, GivesUpAsAreaJumpedWhenThePieceIsSwappedInsideTheCrop) {
    AutoRoiTracker tracker;
    const cv::Mat small = sceneWithPiece(cv::Rect(580, 310, 120, 100));
    const auto first = analyzeFrame(small, PipelineConfig{});
    ASSERT_TRUE(first.isOk()) << first.error().message;
    tracker.update(true, boundsOf(first.value()), small.size());
    const cv::Rect roi = tracker.roi();
    ASSERT_GT(roi.area(), 0);

    const cv::Mat swapped = sceneWithPiece(cv::Rect(545, 281, 190, 158));
    PipelineConfig config;
    config.roi = roi;
    const auto second = analyzeFrame(swapped, config);
    ASSERT_TRUE(second.isOk()) << second.error().message;

    const cv::Rect bigger = boundsOf(second.value());
    ASSERT_EQ(roi & bigger, bigger) << "la pieza nueva tiene que caber entera en el recorte";
    ASSERT_GT(bigger.area(), 2 * boundsOf(first.value()).area())
        << "si no dobla el área, no hay salto que detectar";

    tracker.update(true, bigger, swapped.size());
    EXPECT_FALSE(tracker.tracking());
    EXPECT_EQ(tracker.lastGiveUp(), AutoRoiGiveUp::AreaJumped)
        << "motivo reportado: " << giveUpReason(tracker.lastGiveUp());
}

// 4. La pregunta de verdad: ¿medir con la zona activa da lo mismo que medir sin
// ella? Se recorre la secuencia entera en los dos modos y se compara frame a
// frame, pasando por `effectiveWorkingZone` como hace la ventana.
TEST(WorkingZone, TheWholeSequenceMeasuresTheSameWithTheZoneOnAndOff) {
    AutoRoiTracker tracker;
    const cv::Rect noFixedZone;  // en modo automático la zona dibujada no pinta nada
    double worstFixturePx = 0.0;
    int zonedFrames = 0;

    for (int step = 0; step < kSequenceFrames; ++step) {
        const cv::Mat scene = sceneWithPiece(pieceAtStep(step));

        PipelineConfig off;
        off.roi = effectiveWorkingZone(WorkingZoneMode::Off, noFixedZone, tracker.roi());
        ASSERT_EQ(off.roi.area(), 0) << "el modo apagado no puede recortar nada";
        const auto reference = analyzeFrame(scene, off);
        ASSERT_TRUE(reference.isOk()) << "frame " << step << ": " << reference.error().message;

        PipelineConfig on;
        on.roi = effectiveWorkingZone(WorkingZoneMode::Automatic, noFixedZone, tracker.roi());
        const auto measured = analyzeFrame(scene, on);
        ASSERT_TRUE(measured.isOk()) << "frame " << step << ": " << measured.error().message;

        if (on.roi.area() > 0) {
            ++zonedFrames;
        }
        expectSameMeasurements(reference.value(), measured.value(),
                               "frame " + std::to_string(step) + " de la secuencia",
                               &worstFixturePx);

        // El seguimiento se alimenta con el resultado del análisis recortado,
        // que es el que la ventana tiene a mano.
        tracker.update(true, boundsOf(measured.value()), scene.size());
    }

    // Sin esto el test pasaría también si la zona nunca se hubiera activado:
    // estaría comparando el camino sin recorte consigo mismo.
    ASSERT_EQ(zonedFrames, kSequenceFrames - 1)
        << "la zona no estuvo activa: no se ha comparado nada";
    std::printf("  [zona] %d frames medidos con zona activa, peor desfase %.6f px\n",
                zonedFrames, worstFixturePx);
}

// 5. El recuento de piezas. El test anterior demuestra que recortar no cambia la
// MEDIDA; este es el sitio donde sí la cambiaba, y por eso no se ve desde allí:
// lo que el recorte se lleva por delante no es la precisión de la pieza mayor,
// son las OTRAS CINCO.
//
// El fallo era de verdad y llegaba al operador: con la zona en Automático,
// abrir Configurar le enseñaba «Se ven 1 pieza(s) y se esperan 6» con las seis
// en la mesa, y «Usar detectadas» le ofrecía fijar el valor equivocado.
namespace {

// Seis piezas repartidas, una de ellas claramente la mayor: así el recorte
// automático tiene a quién seguir y las otras cinco quedan fuera.
cv::Mat sceneWithSixPieces() {
    cv::Mat scene = emptyScene();
    const std::vector<cv::Rect> pieces{
        {120, 90, 260, 210},  // la mayor: es la que el recorte va a rodear
        {620, 110, 120, 95},  {820, 130, 110, 90}, {1050, 100, 115, 92},
        {640, 430, 118, 96},  {880, 470, 122, 94},
    };
    for (const auto& piece : pieces) {
        const cv::Mat one = sceneWithPiece(piece);
        one(piece).copyTo(scene(piece));
    }
    return scene;
}

int piecesSeen(const cv::Mat& scene, const cv::Rect& roi) {
    PipelineConfig config;
    config.roi = roi;
    const auto all = analyzeFrames(scene, config);
    return all.isOk() ? static_cast<int>(all.value().size()) : 0;
}

}  // namespace

TEST(WorkingZone, CountingPiecesNeverHappensInsideTheAutomaticCrop) {
    const cv::Mat scene = sceneWithSixPieces();
    const cv::Rect noFixedZone;

    // La verdad de la escena, sin recortar nada.
    const int truth = piecesSeen(scene, {});
    ASSERT_EQ(truth, 6) << "la escena de prueba no tiene seis piezas: no prueba nada";

    // El recorte que el seguimiento produce sobre esta escena, alimentado como
    // lo alimenta la ventana: con la pieza MAYOR, que es la que devuelve
    // `analyzeFrame`.
    AutoRoiTracker tracker;
    PipelineConfig whole;
    const auto biggest = analyzeFrame(scene, whole);
    ASSERT_TRUE(biggest.isOk());
    tracker.update(true, boundsOf(biggest.value()), scene.size());
    const cv::Rect crop = tracker.roi();
    ASSERT_GT(crop.area(), 0) << "sin recorte no hay fallo que reproducir";

    // EL FALLO, reproducido: contar dentro del recorte automático da 1 por
    // construcción. No es un error de precisión ni depende de la escena — el
    // recorte rodea a una pieza con su margen, así que las demás no están.
    const int insideTheCrop = piecesSeen(scene, crop);
    std::printf("  [recuento] la escena tiene %d piezas; dentro del recorte automatico "
                "(%.1f %% del frame) se ven %d\n",
                truth, 100.0 * crop.area() / frameArea(), insideTheCrop);
    EXPECT_EQ(insideTheCrop, 1) << "si esto no da 1, el recorte ya no rodea a una sola pieza "
                                   "y este test dejo de reproducir el fallo";

    // Y LA REGLA que lo arregla: cuando alguien va a leer el recuento, el modo
    // automático suelta el recorte. Nadie cuenta dentro de una ventana elegida
    // para seguir a una sola pieza.
    const cv::Rect counting =
        effectiveWorkingZone(WorkingZoneMode::Automatic, noFixedZone, crop, true);
    EXPECT_EQ(counting.area(), 0);
    EXPECT_EQ(piecesSeen(scene, counting), truth);

    // Sin contar, el recorte sigue puesto: la optimización no se pierde, solo
    // cede cuando cambiaría una respuesta.
    EXPECT_EQ(effectiveWorkingZone(WorkingZoneMode::Automatic, noFixedZone, crop, false), crop);
}

// La zona FIJA no cede, y la diferencia no es un descuido: el operador la
// dibujó diciendo «mira solo aquí», así que ahí dentro está su respuesta. Si
// también se soltara al contar, el recuento incluiría las piezas que él acaba
// de excluir a mano.
TEST(WorkingZone, TheHandDrawnZoneStillLimitsTheCountBecauseTheOperatorSaidSo) {
    const cv::Mat scene = sceneWithSixPieces();
    const cv::Rect drawn(80, 50, 420, 320);  // abarca solo la pieza mayor

    const cv::Rect zone = effectiveWorkingZone(WorkingZoneMode::Fixed, drawn, cv::Rect(), true);
    EXPECT_EQ(zone, drawn) << "la zona dibujada a mano no puede soltarse al contar";
    EXPECT_EQ(piecesSeen(scene, zone), 1);
    EXPECT_EQ(piecesSeen(scene, {}), 6);
}

// ---------------------------------------------------------------------------
// La zona LIBRE: lo que un rectángulo no puede separar
// ---------------------------------------------------------------------------

namespace {

// Dos piezas en diagonal. Es el caso que motiva la zona libre: NINGÚN rectángulo
// puede contener a una sin tocar a la otra, porque sus envolventes se solapan.
// En una mesa real esto es lo corriente — la pieza de al lado, el borde del
// útil, la sombra pegada a un lado.
cv::Mat twoDiagonalPieces() {
    cv::Mat scene = emptyScene();
    const cv::Rect wanted(360, 120, 200, 160);   // la que se quiere medir
    const cv::Rect neighbour(200, 300, 200, 160);  // la de al lado, que estorba
    for (const auto& piece : {wanted, neighbour}) {
        const cv::Mat one = sceneWithPiece(piece);
        one(piece).copyTo(scene(piece));
    }
    return scene;
}

}  // namespace

TEST(WorkingZone, AFreeShapedZoneSeparatesWhatNoRectangleCan) {
    const cv::Mat scene = twoDiagonalPieces();
    const cv::Rect wanted(360, 120, 200, 160);
    const cv::Rect neighbour(200, 300, 200, 160);

    // Primero, que el caso sea el caso: cualquier rectángulo que contenga
    // entera a la pieza buena toca también a la de al lado. Si esto no fuera
    // cierto, el test no estaría probando nada que un rectángulo no resuelva.
    ASSERT_GT((wanted & neighbour).area(), -1);
    const cv::Rect hull = wanted | neighbour;
    ASSERT_EQ(hull & neighbour, neighbour)
        << "el rectángulo que abarca la pieza buena no llega a la vecina: mal montado";

    // Sin zona: se ven las dos.
    PipelineConfig plain;
    const auto all = analyzeFrames(scene, plain);
    ASSERT_TRUE(all.isOk()) << all.error().message;
    EXPECT_EQ(all.value().size(), 2U) << "la escena tiene que tener dos piezas";

    // Con zona LIBRE ceñida a la pieza buena: solo se ve una, y es la buena.
    PipelineConfig free;
    free.roiPolygon = {{350, 110}, {575, 110}, {575, 290}, {350, 290}};
    const auto onlyWanted = analyzeFrames(scene, free);
    ASSERT_TRUE(onlyWanted.isOk()) << onlyWanted.error().message;
    ASSERT_EQ(onlyWanted.value().size(), 1U) << "la zona libre no dejó fuera a la vecina";
    const cv::Rect got = cv::boundingRect(onlyWanted.value().front().contour.points);
    EXPECT_LT(std::abs(got.x - wanted.x), 6) << "se quedó con la pieza equivocada";
    EXPECT_LT(std::abs(got.y - wanted.y), 6);
    std::printf("  [zona libre] caja medida (%d,%d,%d,%d) frente a la buena (%d,%d,%d,%d)\n",
                got.x, got.y, got.width, got.height, wanted.x, wanted.y, wanted.width,
                wanted.height);
}

TEST(WorkingZone, TheFreeZoneDoesNotMoveTheMeasurement) {
    // La misma exigencia que ya se le hace a la zona rectangular: acotar dónde
    // se mira no puede cambiar lo que se mide. Si el fixture saliera distinto,
    // todas las herramientas se desplazarían.
    const cv::Mat scene = sceneWithPiece(pieceAtStep(4));
    const cv::Rect piece = pieceAtStep(4);

    PipelineConfig plain;
    const auto whole = analyzeFrame(scene, plain);
    ASSERT_TRUE(whole.isOk()) << whole.error().message;

    // Un polígono que contiene a la pieza con holgura, y que NO es un
    // rectángulo: un hexágono alrededor, para que el recorte y la máscara sean
    // de verdad distintos de la envolvente.
    const cv::Point centre(piece.x + piece.width / 2, piece.y + piece.height / 2);
    const int radius = std::max(piece.width, piece.height);
    PipelineConfig free;
    for (int k = 0; k < 6; ++k) {
        const double a = 2.0 * CV_PI * k / 6.0;
        free.roiPolygon.emplace_back(
            static_cast<int>(centre.x + radius * std::cos(a)),
            static_cast<int>(centre.y + radius * std::sin(a)));
    }
    const auto zoned = analyzeFrame(scene, free);
    ASSERT_TRUE(zoned.isOk()) << zoned.error().message;

    double worst = 0.0;
    expectSameMeasurements(whole.value(), zoned.value(), "con zona libre hexagonal", &worst);
    std::printf("  [zona libre] peor desfase del fixture: %.6f px\n", worst);
}

TEST(WorkingZone, APolygonWithFewerThanThreeCornersIsNotAZone) {
    // Dos puntos no encierran nada. Lo correcto es comportarse como si no
    // hubiera zona, no recortar a una línea y quedarse sin pieza.
    const cv::Mat scene = sceneWithPiece(pieceAtStep(4));
    PipelineConfig degenerate;
    degenerate.roiPolygon = {{10, 10}, {200, 200}};
    const auto result = analyzeFrame(scene, degenerate);
    ASSERT_TRUE(result.isOk()) << result.error().message;

    const auto plain = analyzeFrame(scene, PipelineConfig{});
    ASSERT_TRUE(plain.isOk());
    EXPECT_EQ(cv::boundingRect(result.value().contour.points),
              cv::boundingRect(plain.value().contour.points))
        << "un polígono imposible cambió lo que se mide";
}

// ---------------------------------------------------------------------------
// Del trazo a la zona: qué se conserva al simplificar
// ---------------------------------------------------------------------------

namespace {

// Qué parte del área discrepa entre dos polígonos, en tanto por uno del
// original. Es la medida honesta: comparar solo las áreas totales dejaría pasar
// una zona del mismo tamaño desplazada a otro sitio.
double areaMismatch(const std::vector<cv::Point>& a, const std::vector<cv::Point>& b) {
    cv::Rect box = cv::boundingRect(a) | cv::boundingRect(b);
    box += cv::Size(4, 4);
    const auto draw = [&box](const std::vector<cv::Point>& poly) {
        cv::Mat mask = cv::Mat::zeros(box.size(), CV_8UC1);
        std::vector<cv::Point> local;
        local.reserve(poly.size());
        for (const auto& point : poly) {
            local.push_back(point - box.tl());
        }
        cv::fillPoly(mask, std::vector<std::vector<cv::Point>>{local}, cv::Scalar(255));
        return mask;
    };
    const cv::Mat first = draw(a);
    const cv::Mat second = draw(b);
    cv::Mat different;
    cv::bitwise_xor(first, second, different);
    const double reference = cv::countNonZero(first);
    return reference > 0.0 ? cv::countNonZero(different) / reference : 1.0;
}

// Un círculo trazado A PULSO: el punto exacto más el temblor de la mano. El
// temblor lleva semilla fija para que el número que salga hoy sea el mismo
// mañana.
std::vector<cv::Point> handDrawnCircle(cv::Point centre, int radius, double jitterPx,
                                       int samples = 400) {
    std::mt19937 noise(20260816U);
    std::uniform_real_distribution<double> wobble(-jitterPx, jitterPx);
    std::vector<cv::Point> trace;
    trace.reserve(static_cast<std::size_t>(samples));
    for (int k = 0; k < samples; ++k) {
        const double angle = 2.0 * CV_PI * k / samples;
        const double r = radius + (jitterPx > 0.0 ? wobble(noise) : 0.0);
        trace.emplace_back(static_cast<int>(std::lround(centre.x + r * std::cos(angle))),
                           static_cast<int>(std::lround(centre.y + r * std::sin(angle))));
    }
    return trace;
}

}  // namespace

TEST(FreeZoneTrace, TheFrontierNeverMovesFurtherThanTheStatedTolerance) {
    // La garantía de la zona es una DISTANCIA, no un área, y este test costó
    // aprenderlo. El primer intento exigía que el área se conservara dentro del
    // 1 % y salía un 1,45 %: la explicación que escribí —«es el temblor de la
    // mano»— era falsa. Medido aparte, el temblor cuesta un 0,6 % y la
    // simplificación un 1,5 %.
    //
    // El motivo es geométrico y no se arregla con umbrales: los vértices que
    // sobreviven están SOBRE el trazo, así que cada cuerda corta por dentro y
    // un polígono inscrito siempre encierra menos que la curva. Es un sesgo en
    // una sola dirección, y lo que sí está acotado es cuánto se mueve el borde.
    const auto trace = handDrawnCircle({300, 300}, 150, 0.0);
    const auto zone = zonePolygonFromTrace(trace);
    ASSERT_GE(zone.size(), 3U);

    const double tolerance = zoneSimplifyTolerancePx(cv::arcLength(trace, true));
    double worst = 0.0;
    for (const auto& point : trace) {
        // Distancia con signo al polígono: negativa fuera, positiva dentro.
        const double inside = cv::pointPolygonTest(zone, cv::Point2f(point), true);
        worst = std::max(worst, -inside);
    }
    std::printf("  [zona libre] %zu puntos -> %zu vertices; el borde se mueve %.2f px como "
                "mucho (tolerancia %.2f px); pierde el %.2f %% del area\n",
                trace.size(), zone.size(), worst, tolerance,
                100.0 * areaMismatch(trace, zone));
    EXPECT_LE(worst, tolerance + 1.0)
        << "el borde guardado se alejó del trazo más de lo prometido";
    EXPECT_LT(zone.size(), trace.size() / 4)
        << "no simplificó nada: se guardarían cientos de vértices que no informan";
}

TEST(FreeZoneTrace, TheTremorOfTheHandCostsLessThanTheTolerance) {
    // Y la otra mitad de la pregunta: con la mano temblando, ¿la zona guardada
    // se parece a la que el operador quería dibujar? Se compara contra el
    // círculo pretendido, no contra el trazo, que es lo único que responde.
    const auto ideal = handDrawnCircle({300, 300}, 150, 0.0, 2000);
    const auto shaky = handDrawnCircle({300, 300}, 150, 1.0);
    const auto zone = zonePolygonFromTrace(shaky);
    ASSERT_GE(zone.size(), 3U);

    const double handError = areaMismatch(ideal, shaky);
    const double zoneError = areaMismatch(ideal, zone);
    std::printf("  [zona libre] frente al circulo pretendido: el pulso discrepa %.3f %%, "
                "la zona guardada %.3f %% (%zu vertices)\n",
                100.0 * handError, 100.0 * zoneError, zone.size());
    EXPECT_LT(zoneError, 0.01)
        << "la zona guardada no se parece a la que se quiso dibujar";
}

TEST(FreeZoneTrace, TheCornersOfADrawnShapeSurvive) {
    // Lo contrario del test anterior: en una forma con esquinas, simplificar no
    // puede redondearlas. Una zona en «L» es justo la que se dibuja para rodear
    // una pieza dejando fuera lo de al lado.
    const std::vector<cv::Point> corners{{100, 100}, {400, 100}, {400, 220},
                                         {220, 220}, {220, 400}, {100, 400}};
    std::vector<cv::Point> trace;
    for (std::size_t i = 0; i < corners.size(); ++i) {
        const cv::Point from = corners[i];
        const cv::Point to = corners[(i + 1) % corners.size()];
        constexpr int kSteps = 60;
        for (int s = 0; s < kSteps; ++s) {
            const double t = static_cast<double>(s) / kSteps;
            trace.emplace_back(
                static_cast<int>(std::lround(from.x + (to.x - from.x) * t)),
                static_cast<int>(std::lround(from.y + (to.y - from.y) * t)));
        }
    }

    const auto zone = zonePolygonFromTrace(trace);
    ASSERT_EQ(zone.size(), corners.size())
        << "la «L» perdió o inventó esquinas al simplificarse";
    for (const auto& corner : corners) {
        double best = 1e9;
        for (const auto& vertex : zone) {
            best = std::min(best, cv::norm(corner - vertex));
        }
        EXPECT_LT(best, 2.0) << "la esquina (" << corner.x << "," << corner.y
                             << ") se movió al simplificar";
    }
}

TEST(FreeZoneTrace, TheAnswerDoesNotDependOnHowBigTheZoneIs) {
    // La tolerancia es una fracción del perímetro, no un número de píxeles, y
    // esto es lo que compra: la misma forma dibujada diez veces más grande se
    // guarda con los mismos vértices. Con una tolerancia absoluta, una zona
    // pequeña perdería sus esquinas y una grande guardaría cientos de puntos.
    //
    // Los dos radios están elegidos POR ENCIMA del suelo de un píxel a
    // propósito: ahí abajo manda el grano de la imagen y no la regla relativa,
    // y eso se comprueba en el test siguiente en vez de mezclarlo con este.
    const auto medium = zonePolygonFromTrace(handDrawnCircle({300, 300}, 200, 0.0, 2000));
    const auto huge = zonePolygonFromTrace(handDrawnCircle({3000, 3000}, 2000, 0.0, 2000));
    std::printf("  [zona libre] r=200 -> %zu vertices; r=2000 -> %zu vertices\n",
                medium.size(), huge.size());
    ASSERT_GE(medium.size(), 3U);
    ASSERT_GE(huge.size(), 3U);
    const std::size_t difference =
        std::max(medium.size(), huge.size()) - std::min(medium.size(), huge.size());
    EXPECT_LE(difference, 2U)
        << "el número de vértices depende del tamaño: la tolerancia no es relativa";
}

TEST(FreeZoneTrace, BelowOnePixelTheImageGrainTakesOver) {
    // El suelo de un píxel rompe la invariancia de escala en las zonas
    // pequeñas, y hace bien: por debajo del píxel no hay información que
    // conservar, así que una zona diminuta se simplifica RELATIVAMENTE más.
    // Lo apunta este test para que no parezca un descuido cuando alguien vea
    // que un círculo de radio 40 sale con menos vértices que uno de radio 400.
    const auto tiny = handDrawnCircle({100, 100}, 40, 0.0, 400);
    const double perimeter = cv::arcLength(tiny, true);
    EXPECT_DOUBLE_EQ(zoneSimplifyTolerancePx(perimeter), 1.0)
        << "con perímetro " << perimeter << " la fracción no llega al píxel: manda el suelo";
    EXPECT_GT(zoneSimplifyTolerancePx(4000.0), 1.0)
        << "en una zona grande el suelo no puede seguir mandando";
}

TEST(FreeZoneTrace, AScribbleThatEnclosesNothingIsNotAZone) {
    // Un trazo de ida y vuelta por la misma línea: tiene cientos de puntos y no
    // encierra nada. Devolverlo como zona dejaría la detección sin nada dentro,
    // y el operador vería morir la detección sin motivo visible.
    std::vector<cv::Point> line;
    for (int x = 100; x < 400; ++x) {
        line.emplace_back(x, 200);
    }
    for (int x = 399; x >= 100; --x) {
        line.emplace_back(x, 200);
    }
    EXPECT_TRUE(zonePolygonFromTrace(line).empty());

    // Y un garabato diminuto tampoco: 6x6 px de zona es un clic tembloroso, no
    // una intención.
    const std::vector<cv::Point> speck{{50, 50}, {56, 50}, {56, 56}, {50, 56}};
    EXPECT_TRUE(zonePolygonFromTrace(speck).empty());

    // Dos puntos no son un área por definición.
    EXPECT_TRUE(zonePolygonFromTrace({{0, 0}, {10, 10}}).empty());
}

// ---------------------------------------------------------------------------
// La regla del modo: qué zona manda
// ---------------------------------------------------------------------------

TEST(FreeZoneMode, DrawingTheFreeZoneUsesItAndErasingItLetsGo) {
    EXPECT_EQ(modeAfterFreeZoneChanged(WorkingZoneMode::Off, true), WorkingZoneMode::Free);
    EXPECT_EQ(modeAfterFreeZoneChanged(WorkingZoneMode::Automatic, true),
              WorkingZoneMode::Free);
    EXPECT_EQ(modeAfterFreeZoneChanged(WorkingZoneMode::Fixed, true), WorkingZoneMode::Free);
    // Se cae a AUTOMÁTICA: quitar una zona es dejar de restringir, no
    // renunciar a la optimización que no puede cambiar ninguna respuesta.
    EXPECT_EQ(modeAfterFreeZoneChanged(WorkingZoneMode::Free, false),
              WorkingZoneMode::Automatic);
    // Borrar la libre no puede tocar a los modos que no la usaban.
    EXPECT_EQ(modeAfterFreeZoneChanged(WorkingZoneMode::Automatic, false),
              WorkingZoneMode::Automatic);
    EXPECT_EQ(modeAfterFreeZoneChanged(WorkingZoneMode::Fixed, false),
              WorkingZoneMode::Fixed);
    // Y dibujar la rectangular no puede dejar la libre a medias.
    EXPECT_EQ(modeAfterFixedZoneChanged(WorkingZoneMode::Free, true), WorkingZoneMode::Fixed);
    EXPECT_EQ(modeAfterFixedZoneChanged(WorkingZoneMode::Free, false), WorkingZoneMode::Free);
}

TEST(FreeZoneMode, ASavedFreeZoneDoesNotCropWhileItsModeIsOff) {
    // El fallo que esta función existe para impedir: la zona sigue guardada de
    // otro día, el operador la ve apagada en el panel, y estaría recortando.
    const std::vector<cv::Point> drawn{{10, 10}, {200, 10}, {200, 200}, {10, 200}};
    EXPECT_TRUE(effectiveWorkingPolygon(WorkingZoneMode::Off, drawn).empty());
    EXPECT_TRUE(effectiveWorkingPolygon(WorkingZoneMode::Automatic, drawn).empty());
    EXPECT_TRUE(effectiveWorkingPolygon(WorkingZoneMode::Fixed, drawn).empty());
    EXPECT_EQ(effectiveWorkingPolygon(WorkingZoneMode::Free, drawn), drawn);
    // Con el modo puesto pero sin dibujo, tampoco hay polígono que aplicar.
    EXPECT_TRUE(effectiveWorkingPolygon(WorkingZoneMode::Free, {{0, 0}, {5, 5}}).empty());

    // Y lo mismo comprobado de punta a punta: la escena de dos piezas en
    // diagonal, con la zona guardada y el modo apagado, tiene que seguir viendo
    // las dos.
    const cv::Mat scene = twoDiagonalPieces();
    PipelineConfig config;
    config.roiPolygon = effectiveWorkingPolygon(
        WorkingZoneMode::Off, {{350, 110}, {575, 110}, {575, 290}, {350, 290}});
    const auto seen = analyzeFrames(scene, config);
    ASSERT_TRUE(seen.isOk()) << seen.error().message;
    EXPECT_EQ(seen.value().size(), 2U) << "una zona guardada recortó con su modo apagado";
}

TEST(FreeZoneMode, TheCropIsTheHullOfTheFreeZoneAndTheCountStillRespectsIt) {
    const std::vector<cv::Point> drawn{{40, 60}, {300, 30}, {320, 250}, {60, 280}};
    const cv::Rect hull = cv::boundingRect(drawn);
    EXPECT_EQ(effectiveWorkingZone(WorkingZoneMode::Free, cv::Rect(), cv::Rect(), false, drawn),
              hull);
    // Como la fija: contar no la suelta. El operador dijo «mira solo aquí» y esa
    // es su respuesta, no una optimización que pueda cederse.
    EXPECT_EQ(effectiveWorkingZone(WorkingZoneMode::Free, cv::Rect(), cv::Rect(), true, drawn),
              hull);
    // Sin polígono utilizable, no hay recorte que valga.
    EXPECT_EQ(effectiveWorkingZone(WorkingZoneMode::Free, cv::Rect(10, 10, 50, 50),
                                   cv::Rect(), false)
                  .area(),
              0);
}

TEST(FreeZoneMode, TheModeSurvivesBeingSavedAndRead) {
    // Se persiste por nombre justo para que añadir un modo no rompa lo guardado.
    for (const auto mode : {WorkingZoneMode::Off, WorkingZoneMode::Automatic,
                            WorkingZoneMode::Fixed, WorkingZoneMode::Free}) {
        EXPECT_EQ(workingZoneModeFromKey(workingZoneModeKey(mode)), mode);
    }
    EXPECT_EQ(workingZoneModeFromKey("free"), WorkingZoneMode::Free);
    // Lo que no se reconoce cae al modo más conservador, no al nuevo.
    EXPECT_EQ(workingZoneModeFromKey("libre"), WorkingZoneMode::Off);
    EXPECT_EQ(workingZoneModeFromKey(nullptr), WorkingZoneMode::Off);
}
