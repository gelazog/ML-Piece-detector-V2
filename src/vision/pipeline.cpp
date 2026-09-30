#include "vision/pipeline.h"
#include <algorithm>
#include <cstdio>

#include "vision/gray.h"

#include <chrono>

#include <opencv2/imgproc.hpp>

#include <utility>
#include <vector>

#include "vision/contour_analysis.h"
#include "vision/position_fixture.h"
#include "vision/segmentation.h"

namespace pci::vision {

namespace {

// La zona efectiva: el rectángulo con el que se recorta.
//
// Con polígono, es su envolvente — así el recorte y su ganancia de velocidad se
// conservan tal cual, y el polígono solo añade precisión encima. Sin polígono,
// el rectángulo que puso el operador.
cv::Rect croppingRect(const PipelineConfig& config, const cv::Rect& frameRect) {
    if (config.roiPolygon.size() >= 3) {
        return cv::boundingRect(config.roiPolygon) & frameRect;
    }
    return config.roi & frameRect;
}


// Borra de la máscara todo lo que cae FUERA del polígono.
//
// Se aplica sobre la máscara ya segmentada y no sobre la imagen, y la
// diferencia importa: recortar la imagen antes metería un borde artificial
// —negro contra la pieza— que la segmentación tomaría por un contorno de
// verdad. Sobre la máscara, lo de fuera simplemente deja de existir.
void keepOnlyInsidePolygon(cv::Mat& mask, const PipelineConfig& config,
                           const cv::Rect& crop, bool cropped) {
    if (config.roiPolygon.size() < 3 || mask.empty()) {
        return;
    }
    // El polígono viene en coordenadas de la imagen completa; la máscara está
    // en las del recorte.
    std::vector<cv::Point> local;
    local.reserve(config.roiPolygon.size());
    const cv::Point offset = cropped ? crop.tl() : cv::Point(0, 0);
    for (const auto& point : config.roiPolygon) {
        local.push_back(point - offset);
    }
    cv::Mat inside = cv::Mat::zeros(mask.size(), CV_8UC1);
    cv::fillPoly(inside, std::vector<std::vector<cv::Point>>{local}, cv::Scalar(255));
    cv::bitwise_and(mask, inside, mask);
}

// Convierte un contorno ya aceptado en un PieceAnalysis completo.
//
// La máscara limpia se construye **solo dentro de la envolvente de la pieza**:
// con varias piezas, hacerlo a tamaño de frame para cada una multiplicaría por
// N el coste de la parte que C4b acababa de abaratar.
//
// `gray` es `working` ya en gris, o vacía si no hay afinado subpíxel. Llega
// hecha de fuera por lo que costaba hacerla aquí: se convertía la imagen ENTERA
// una vez por pieza, y con la bandeja de cien tuercas (1920×1080, BGR) eso
// llevaba `analyzeFrames` de 65,8 a 108,4 ms — cien conversiones de la misma
// imagen para quedarse cada vez con los píxeles de alrededor de una tuerca.
//
// `full` y `offset` dicen dónde vive `working` dentro de la imagen completa:
// la máscara se crea UNA vez, ya del tamaño del frame y en su sitio. Antes se
// creaba del tamaño del recorte y, con zona de detección, se volvía a crear del
// tamaño del frame para copiarla encima: dos reservas y una copia por pieza
// donde basta una reserva.
core::Result<PieceAnalysis> analyzePiece(const cv::Mat& working, const cv::Mat& gray,
                                         PieceContour contour, const PipelineConfig& config,
                                         const cv::Size& full, const cv::Point& offset) {
    const cv::Rect box = cv::boundingRect(contour.points) &
                         cv::Rect(0, 0, working.cols, working.rows);
    if (box.empty()) {
        return core::Result<PieceAnalysis>::err("Contorno degenerado");
    }

    cv::Mat pieceMask = cv::Mat::zeros(box.size(), CV_8UC1);
    std::vector<std::vector<cv::Point>> shifted{contour.points};
    for (auto& point : shifted.front()) {
        point -= box.tl();
    }
    cv::drawContours(pieceMask, shifted, 0, cv::Scalar(255), cv::FILLED);

    auto fixture = computeFixture(pieceMask, config.autoOrient);
    if (!fixture.isOk()) {
        return core::Result<PieceAnalysis>::err(fixture.error().message);
    }
    fixture.value().origin += cv::Point2f(box.tl());

    Fixture local = fixture.value();
    local.origin -= cv::Point2f(box.tl());
    auto normalized =
        normalizePiece(working(box), pieceMask, local, config.canonicalSize);
    if (!normalized.isOk()) {
        return core::Result<PieceAnalysis>::err(normalized.error().message);
    }

    PieceAnalysis analysis;
    analysis.contour = std::move(contour);
    analysis.fixture = fixture.value();
    analysis.normalized = std::move(normalized.value());
    // Del tamaño del FRAME y no de la envolvente, aunque sería más barato: la
    // máscara es API pública y quien la consume la cruza con la imagen entera
    // (`pieceMaskWithHoles` exige el mismo tamaño, y las pruebas lo comprueban).
    analysis.mask = cv::Mat::zeros(full, CV_8UC1);
    pieceMask.copyTo(analysis.mask(box + offset));

    // AFINADO SUBPÍXEL, TAMBIÉN POR AQUÍ.
    //
    // Estaba solo en `analyzeFrame`, el camino de UNA pieza. `analyzeFrames`
    // —el de varias— pasa por aquí, así que el ajuste se ignoraba en silencio
    // en cuanto había más de una pieza en el encuadre.
    //
    // Lo grave no es que faltara: es que ese ajuste abre un diálogo avisando de
    // que «las medidas de la pieza cambian a partir de ahora» y pidiendo revisar
    // las tolerancias. El operador revisa sus tolerancias contra un cambio que
    // en su bandeja no se ha producido.
    //
    // Y desde que el modo automático mide TODAS las piezas, este camino es el
    // normal y no la excepción, así que el hueco pasó de raro a habitual.
    //
    // Los puntos ya están en coordenadas de `working`, que es el marco de la
    // imagen que se pasa: se afina contra ella directamente, igual que arriba.
    if (config.subpixelEdges && analysis.contour.points.size() >= 3) {
        const auto refined = refineContourSubpixel(gray, analysis.contour.points);
        if (refined.refined > 0) {
            analysis.contour.area = subpixelArea(refined.points);
            analysis.contour.perimeter = subpixelPerimeter(refined.points);
            analysis.contour.subpixel = refined.points;
        }
    }
    return core::Result<PieceAnalysis>::ok(std::move(analysis));
}

// Lleva un análisis del marco del recorte al de la imagen completa. La máscara
// no: `analyzePiece` ya la deja en su sitio.
void shiftToFullFrame(PieceAnalysis& analysis, const cv::Rect& roi) {
    const cv::Point offset = roi.tl();
    for (auto& point : analysis.contour.points) {
        point += offset;
    }
    analysis.contour.centroid += cv::Point2f(offset);
    analysis.contour.rotatedRect.center += cv::Point2f(offset);
    analysis.fixture.origin += cv::Point2f(offset);
}

// El gris que usa el afinado subpíxel, con la regla de siempre: tres canales se
// convierten y cualquier otra cosa se pasa tal cual. Vacía si no hay afinado,
// para no pagar la conversión cuando nadie la va a leer.
//
// No es `toGray` a propósito: con cuatro canales `toGray` convierte y esto no
// —`refineContourSubpixel` recibe la BGRA, ve que no es CV_8UC1 y devuelve el
// contorno sin afinar—. Cambiarlo es una corrección aparte, no algo que meter
// de paso en un cambio que promete resultados idénticos.
cv::Mat subpixelGray(const cv::Mat& image, const PipelineConfig& config) {
    if (!config.subpixelEdges) {
        return {};
    }
    if (image.channels() == 3) {
        cv::Mat gray;
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
        return gray;
    }
    return image;
}

// La imagen de trabajo y su máscara segmentada: lo que comparten los dos
// caminos, el de una pieza y el de todas.
struct Segmented {
    cv::Rect roi;
    bool useRoi = false;
    cv::Mat working;
    cv::Mat mask;
};

core::Result<Segmented> segmentWorking(const cv::Mat& image, const PipelineConfig& config) {
    Segmented out;
    const cv::Rect frameRect(0, 0, image.cols, image.rows);
    out.roi = croppingRect(config, frameRect);
    out.useRoi = out.roi.area() > 0 && out.roi != frameRect;
    out.working = out.useRoi ? image(out.roi) : image;

    auto mask = segmentPiece(out.working, config.segmentation);
    if (!mask.isOk()) {
        return core::Result<Segmented>::err(mask.error().message);
    }
    out.mask = std::move(mask.value());
    applyMaskCorrection(out.mask, config, out.roi, out.useRoi);
    keepOnlyInsidePolygon(out.mask, config, out.roi, out.useRoi);
    return core::Result<Segmented>::ok(std::move(out));
}

// Todas las piezas a partir de una máscara ya segmentada. `grayWorking` es el
// gris de `seg.working` (vacío sin afinado subpíxel).
core::Result<std::vector<PieceAnalysis>> piecesFromMask(const cv::Mat& image,
                                                        const Segmented& seg,
                                                        const cv::Mat& grayWorking,
                                                        const PipelineConfig& config,
                                                        int* belowMinArea,
                                                        std::vector<double>* blobAreas) {
    auto contours = findPieceContours(config.segmentation.splitTouchingPieces
                                          ? splitTouchingPieces(seg.mask)
                                          : seg.mask,
                                      config.minAreaFraction, config.maxAreaFraction,
                                      kMaxPieces, nullptr, belowMinArea, blobAreas);
    if (contours.empty()) {
        return core::Result<std::vector<PieceAnalysis>>::err(
            "No se encontró ninguna pieza en la imagen");
    }

    const cv::Point offset = seg.useRoi ? seg.roi.tl() : cv::Point(0, 0);
    std::vector<PieceAnalysis> pieces;
    pieces.reserve(contours.size());
    for (auto& contour : contours) {
        auto analysis = analyzePiece(seg.working, grayWorking, std::move(contour), config,
                                     image.size(), offset);
        if (!analysis.isOk()) {
            continue;  // una pieza degenerada no invalida a las demás
        }
        if (seg.useRoi) {
            shiftToFullFrame(analysis.value(), seg.roi);
        }
        pieces.push_back(std::move(analysis.value()));
    }
    if (pieces.empty()) {
        return core::Result<std::vector<PieceAnalysis>>::err(
            "No se encontró ninguna pieza en la imagen");
    }
    return core::Result<std::vector<PieceAnalysis>>::ok(std::move(pieces));
}

// El cronómetro de `analyzeFrame`. Solo existe si alguien lo pidió: `mark`
// apunta los ms transcurridos en la etapa y reinicia, de forma que las etapas
// se reparten el total sin huecos ni solapes — que es lo que permite comprobar
// que la suma cuadra, y un desglose cuya suma no cuadra está mintiendo.
class Stopwatch {
public:
    explicit Stopwatch(StageTimings* timings) : timings_(timings) {
        if (timings_ != nullptr) {
            started_ = Clock::now();
            last_ = started_;
        }
    }
    void mark(double StageTimings::*stage) {
        if (timings_ == nullptr) {
            return;
        }
        const auto now = Clock::now();
        timings_->*stage = std::chrono::duration<double, std::milli>(now - last_).count();
        last_ = now;
    }
    // El total se mide de punta a punta, NO sumando las etapas. Así, si alguna
    // vez el desglose deja de cuadrar con el total, la diferencia aparece en vez
    // de esconderse: es el trozo de trabajo que nadie está atribuyendo a nada.
    void finish() {
        if (timings_ != nullptr) {
            timings_->total =
                std::chrono::duration<double, std::milli>(Clock::now() - started_).count();
        }
    }

private:
    using Clock = std::chrono::steady_clock;
    StageTimings* timings_;
    Clock::time_point started_{};
    Clock::time_point last_{};
};

// La pieza principal —el contorno mayor— a partir de una máscara ya segmentada.
// `grayImage` es el gris de la imagen COMPLETA si quien llama ya lo tiene; si
// llega vacío y hace falta, se saca aquí, al final, para que el cronómetro de
// `analyzeFrame` lo siga atribuyendo donde siempre (a ninguna etapa, al total).
core::Result<PieceAnalysis> mainFromMask(const cv::Mat& image, const Segmented& seg,
                                         cv::Mat grayImage,
                                         const PipelineConfig& config, Stopwatch& clock) {
    auto contour =
        findLargestContour(seg.mask, config.minAreaFraction, config.maxAreaFraction);
    if (!contour.isOk()) {
        return core::Result<PieceAnalysis>::err(contour.error().message);
    }
    clock.mark(&StageTimings::contour);

    // Máscara reconstruida solo con el contorno mayor: los blobs de ruido que
    // sobrevivieron a la morfología no deben sesgar el fixture ni el recorte.
    cv::Mat cleanMask = cv::Mat::zeros(seg.mask.size(), CV_8UC1);
    const std::vector<std::vector<cv::Point>> fill{contour.value().points};
    cv::drawContours(cleanMask, fill, 0, cv::Scalar(255), cv::FILLED);

    const auto fixture = computeFixture(cleanMask, config.autoOrient);
    if (!fixture.isOk()) {
        return core::Result<PieceAnalysis>::err(fixture.error().message);
    }
    clock.mark(&StageTimings::fixture);

    auto normalized =
        normalizePiece(seg.working, cleanMask, fixture.value(), config.canonicalSize);
    if (!normalized.isOk()) {
        return core::Result<PieceAnalysis>::err(normalized.error().message);
    }
    clock.mark(&StageTimings::normalize);

    PieceAnalysis analysis;
    analysis.contour = std::move(contour.value());
    analysis.fixture = fixture.value();
    analysis.normalized = std::move(normalized.value());

    if (seg.useRoi) {
        // Desplazar contorno, fixture y máscara al marco de la imagen completa.
        const cv::Point offset = seg.roi.tl();
        for (auto& point : analysis.contour.points) {
            point += offset;
        }
        analysis.contour.centroid += cv::Point2f(offset);
        analysis.contour.rotatedRect.center += cv::Point2f(offset);
        analysis.fixture.origin += cv::Point2f(offset);

        cv::Mat fullMask = cv::Mat::zeros(image.size(), CV_8UC1);
        cleanMask.copyTo(fullMask(seg.roi));
        analysis.mask = std::move(fullMask);
    } else {
        analysis.mask = std::move(cleanMask);
    }

    // Afinado subpíxel del contorno, si se pidió.
    //
    // AQUÍ y no antes: los puntos ya están en coordenadas de la imagen completa,
    // que es donde vive `image`. Afinar dentro del recorte y desplazar después
    // funcionaría igual, pero obligaría a recordar en qué marco está cada cosa
    // en dos sitios en vez de uno.
    //
    // El área y el perímetro se recalculan desde el contorno afinado y en coma
    // flotante: redondear al entero justo después de haber medido en décimas
    // tiraría la precisión recién ganada.
    if (config.subpixelEdges && analysis.contour.points.size() >= 3) {
        if (grayImage.empty()) {
            grayImage = subpixelGray(image, config);
        }
        const auto refined = refineContourSubpixel(grayImage, analysis.contour.points);
        if (refined.refined > 0) {
            analysis.contour.area = subpixelArea(refined.points);
            analysis.contour.perimeter = subpixelPerimeter(refined.points);
            analysis.contour.subpixel = refined.points;
        }
    }
    return core::Result<PieceAnalysis>::ok(std::move(analysis));
}

}  // namespace

// Aplica la corrección manual del borde sobre la máscara ya segmentada.
//
// Se hace aquí y no antes por lo mismo que el polígono de la zona libre: pintar
// sobre la IMAGEN metería bordes artificiales que la segmentación leería como
// contornos de verdad. Sobre la máscara, lo marcado simplemente cuenta o deja
// de contar.
void applyMaskCorrection(cv::Mat& mask, const PipelineConfig& config, const cv::Rect& crop,
                         bool cropped) {
    if (mask.empty()) {
        return;
    }
    const auto region = [&](const cv::Mat& correction) {
        if (correction.empty() || correction.type() != CV_8UC1) {
            return cv::Mat();
        }
        if (!cropped) {
            return correction.size() == mask.size() ? correction : cv::Mat();
        }
        // La corrección viene en coordenadas de la imagen completa y la máscara
        // está en las del recorte.
        const cv::Rect valid = crop & cv::Rect(0, 0, correction.cols, correction.rows);
        return valid.size() == mask.size() ? correction(valid) : cv::Mat();
    };

    // El orden importa y es el del pincel: primero se añade lo que falta y
    // después se quita lo que sobra, así marcar fondo sobre algo recién marcado
    // como pieza gana lo último que hizo el operador.
    if (const cv::Mat add = region(config.forcePiece); !add.empty()) {
        cv::bitwise_or(mask, add, mask);
    }
    if (const cv::Mat remove = region(config.forceBackground); !remove.empty()) {
        cv::Mat keep;
        cv::bitwise_not(remove, keep);
        cv::bitwise_and(mask, keep, mask);
    }
}

MeasurementStability measureStability(const cv::Mat& image, const PipelineConfig& config,
                                     int levels) {
    MeasurementStability out;
    out.levelsSwept = std::max(1, levels);
    if (image.empty()) {
        out.summary = "No hay imagen que medir.";
        return out;
    }
    // El umbral de partida: el que el operador puso, o el que elegiría Otsu.
    int base = config.segmentation.manualThreshold;
    if (base < 0) {
        cv::Mat gray = toGray(image);
        if (gray.empty()) {
            out.summary = "No se puede leer esta imagen.";
            return out;
        }
        // No es `otsuMask`: aquí se quiere el UMBRAL que elige Otsu, la máscara se tira.
        cv::Mat binary;
        base = static_cast<int>(
            cv::threshold(gray, binary, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU));
    }

    std::vector<double> widths;
    for (int delta = -out.levelsSwept; delta <= out.levelsSwept; delta += 2) {
        PipelineConfig probe = config;
        probe.segmentation.manualThreshold = std::clamp(base + delta, 1, 254);
        auto all = analyzeFrames(image, probe);
        if (!all.isOk() || all.value().empty()) {
            continue;
        }
        const auto& piece = all.value()[largestPieceIndex(all.value())];
        widths.push_back(std::max(piece.contour.rotatedRect.size.width,
                                  piece.contour.rotatedRect.size.height));
    }
    // Con menos de la mitad del barrido no se puede hablar de oscilación: lo que
    // habría es que la pieza se pierde a poco que se toque el umbral, y eso es
    // otra cosa y se dice aparte.
    const std::size_t expected = static_cast<std::size_t>(out.levelsSwept) + 1;
    if (widths.size() < expected / 2 + 1) {
        out.summary =
            "La pieza deja de detectarse en cuanto el corte de gris se mueve un poco: "
            "la medida no es estable y antes de fiarse hay que arreglar la iluminación.";
        return out;
    }

    std::sort(widths.begin(), widths.end());
    out.measured = true;
    out.medianWidthPx = widths[widths.size() / 2];
    out.swingPx = widths.back() - widths.front();
    out.swingFraction = out.medianWidthPx > 0.0 ? out.swingPx / out.medianWidthPx : 0.0;

    char text[320];
    if (out.swingFraction >= kMeasurementMovesWithTheLight) {
        std::snprintf(text, sizeof(text),
                      "Moviendo el corte de gris %d niveles a cada lado, esta pieza mide "
                      "entre %.1f y %.1f px: oscila un %.1f %%. A esta escena le afecta "
                      "la luz tanto como la propia pieza — mira si hay sombra pegada al "
                      "borde o un reflejo de frente.",
                      out.levelsSwept, widths.front(), widths.back(),
                      100.0 * out.swingFraction);
    } else {
        std::snprintf(text, sizeof(text),
                      "Moviendo el corte de gris %d niveles a cada lado, la medida solo "
                      "oscila un %.1f %% (%.1f px de %.1f). El borde manda sobre la luz.",
                      out.levelsSwept, 100.0 * out.swingFraction, out.swingPx,
                      out.medianWidthPx);
    }
    out.summary = text;
    return out;
}

core::Result<std::vector<PieceAnalysis>> analyzeFrames(const cv::Mat& image,
                                                       const PipelineConfig& config,
                                                       int* belowMinArea,
                                                       std::vector<double>* blobAreas) {
    if (image.empty()) {
        return core::Result<std::vector<PieceAnalysis>>::err("Imagen vacía");
    }
    auto seg = segmentWorking(image, config);
    if (!seg.isOk()) {
        return core::Result<std::vector<PieceAnalysis>>::err(seg.error().message);
    }
    // El gris, UNA vez y antes del bucle de piezas. Y del recorte, no de la
    // imagen entera: es lo único que `analyzePiece` va a mirar.
    const cv::Mat gray = subpixelGray(seg.value().working, config);
    return piecesFromMask(image, seg.value(), gray, config, belowMinArea, blobAreas);
}

FrameAndPieces analyzeFrameAndPieces(const cv::Mat& image, const PipelineConfig& config) {
    if (image.empty()) {
        return {core::Result<PieceAnalysis>::err("Imagen vacía"),
                core::Result<std::vector<PieceAnalysis>>::err("Imagen vacía")};
    }
    auto seg = segmentWorking(image, config);
    if (!seg.isOk()) {
        return {core::Result<PieceAnalysis>::err(seg.error().message),
                core::Result<std::vector<PieceAnalysis>>::err(seg.error().message)};
    }
    // Un solo gris para los dos caminos. La conversión es píxel a píxel, así
    // que el gris del recorte es el recorte del gris: idéntico a convertir
    // `working` por separado, como hace `analyzeFrames`.
    const cv::Mat grayImage = subpixelGray(image, config);
    const cv::Mat grayWorking =
        grayImage.empty() || !seg.value().useRoi ? grayImage : grayImage(seg.value().roi);
    Stopwatch unused(nullptr);
    // Ninguno de los dos caminos escribe en la máscara compartida:
    // `findContours` no la modifica desde OpenCV 3.2 y `splitTouchingPieces`
    // trabaja sobre una copia.
    return {mainFromMask(image, seg.value(), grayImage, config, unused),
            piecesFromMask(image, seg.value(), grayWorking, config, nullptr, nullptr)};
}

cv::Mat pieceMaskWithHoles(const cv::Mat& image, const cv::Mat& filledMask,
                           const SegmentationOptions& options) {
    if (image.empty() || filledMask.empty() || image.size() != filledMask.size()) {
        return filledMask;  // sin nada que cruzar, lo que había es lo mejor que hay
    }
    auto segmented = segmentPiece(image, options);
    if (!segmented.isOk()) {
        // Si la segmentación falla ahora, la máscara rellena sigue siendo
        // válida como silueta: se pierden los agujeros y no se pierde la pieza.
        return filledMask;
    }
    cv::Mat withHoles;
    cv::bitwise_and(filledMask, segmented.value(), withHoles);
    // ¿SE HA PERDIDO LA PIEZA, O ES QUE LA PIEZA TIENE UN AGUJERO GRANDE?
    //
    // Un cruce que se queda sin pieza significa que la segunda segmentación no
    // vio lo mismo que la primera (otra polaridad, otro umbral automático). En
    // ese caso manda la máscara original: perder los agujeros es un
    // inconveniente, perder la pieza es no medir nada.
    //
    // Aquí la sospecha se medía por ÁREA —«si el cruce conserva menos de la
    // mitad, desconfía»— y eso descartaba justo las piezas para las que existe
    // esta función. Una arandela de pared fina conserva poco de su disco por
    // definición: en `arandelas-4.png` los anillos son Ø 191 px con un taladro
    // de Ø 150, o sea que el material es el 46 % del disco. Caía por debajo de
    // la mitad, se devolvía la máscara rellena y la arandela se quedaba sin
    // agujeros, sin Ø interior y con un área de 28 678 px² en vez de 13 264.
    //
    // Y era peor que una cota perdida: la MISMA arandela salía con siete
    // agujeros en el informe de la imagen entera y con cero al elegirla como
    // pieza, según por qué camino se llegara.
    //
    // Lo que hay que comprobar no es cuánta área queda, sino si sigue estando la
    // pieza: el cruce tiene que conservar su CONTORNO EXTERIOR. Se rellena el
    // contorno mayor del cruce y se compara con la máscara rellena. Una arandela
    // da el 100 % —su borde de fuera es el mismo— y un cruce con la polaridad
    // cambiada se queda con el agujero, cuyo contorno relleno es mucho menor.
    // No es `largestOuterContour`: CHAIN_APPROX_SIMPLE y hace falta el ÍNDICE para `drawContours`.
    std::vector<std::vector<cv::Point>> kept;
    cv::findContours(withHoles, kept, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    if (kept.empty()) {
        return filledMask;
    }
    const auto biggest = std::max_element(
        kept.begin(), kept.end(),
        [](const auto& a, const auto& b) { return cv::contourArea(a) < cv::contourArea(b); });
    cv::Mat outline = cv::Mat::zeros(withHoles.size(), CV_8UC1);
    cv::drawContours(outline, kept,
                     static_cast<int>(std::distance(kept.begin(), biggest)),
                     cv::Scalar(255), cv::FILLED);
    // El 0,8 no separa dos poblaciones medidas: es holgura para que un umbral
    // algo distinto pueda comerse unos píxeles del borde sin que eso cuente como
    // haber perdido la pieza. Lo que tiene que caer del otro lado —quedarse con
    // el agujero en vez de con la pieza— no se acerca: en los anillos de
    // `arandelas-4.png` sería el 62 %.
    if (cv::countNonZero(outline) < 0.8 * cv::countNonZero(filledMask)) {
        return filledMask;
    }
    return withHoles;
}

core::Result<PieceAnalysis> analyzeFrame(const cv::Mat& image, const PipelineConfig& config,
                                         StageTimings* timings) {
    if (image.empty()) {
        return core::Result<PieceAnalysis>::err("Imagen vacía");
    }
    // El cronómetro solo corre si alguien lo pidió (ver `Stopwatch`).
    Stopwatch clock(timings);

    // Zona de detección: todo el pipeline trabaja sobre el recorte y al final
    // los resultados se llevan a coordenadas de la imagen completa.
    auto seg = segmentWorking(image, config);
    if (!seg.isOk()) {
        return core::Result<PieceAnalysis>::err(seg.error().message);
    }
    clock.mark(&StageTimings::segment);

    auto analysis = mainFromMask(image, seg.value(), cv::Mat(), config, clock);
    clock.finish();
    return analysis;
}

}  // namespace pci::vision
