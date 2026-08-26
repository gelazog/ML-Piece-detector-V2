#include "vision/edge_segmentation.h"
#include "vision/gray.h"
#include <cstdio>
#include "vision/pipeline.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace pci::vision {

namespace {


// El grosor del marco exterior del que se toma la muestra del fondo.
int borderWidth(const cv::Size& size) {
    return std::max(4, std::min(size.height, size.width) / 50);
}

// Los valores del marco exterior de una imagen, que es fondo casi seguro.
std::vector<float> borderSample(const cv::Mat& single) {
    const int b = borderWidth(single.size());
    std::vector<float> values;
    values.reserve(static_cast<std::size_t>(2 * b * (single.cols + single.rows)));
    const auto take = [&values](const cv::Mat& region) {
        cv::Mat as32;
        region.convertTo(as32, CV_32F);
        values.insert(values.end(), as32.begin<float>(), as32.end<float>());
    };
    take(single.rowRange(0, b));
    take(single.rowRange(single.rows - b, single.rows));
    take(single.colRange(0, b));
    take(single.colRange(single.cols - b, single.cols));
    return values;
}

// Mediana y desviación robusta (MAD escalada). Robustas y no media/sigma porque
// si una pieza toca el borde del encuadre, la media se va con ella y el ruido se
// dispara — y entonces el umbral que sale de ellas no vale para nada.
std::pair<double, double> medianAndSpread(std::vector<float> values) {
    if (values.empty()) {
        return {0.0, 0.0};
    }
    const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
    std::nth_element(values.begin(), middle, values.end());
    const double median = *middle;
    std::vector<float> deviations;
    deviations.reserve(values.size());
    for (const float value : values) {
        deviations.push_back(static_cast<float>(std::abs(value - median)));
    }
    const auto mid = deviations.begin() + static_cast<std::ptrdiff_t>(deviations.size() / 2);
    std::nth_element(deviations.begin(), mid, deviations.end());
    return {median, 1.4826 * *mid};
}

}  // namespace

core::Result<cv::Mat> segmentByEdges(const cv::Mat& image,
                                     const EdgeSegmentationOptions& options) {
    const cv::Mat gray = toGray(image);
    if (gray.empty()) {
        return core::Result<cv::Mat>::err("Imagen vacía o con un formato no soportado");
    }
    if (gray.rows < 32 || gray.cols < 32) {
        return core::Result<cv::Mat>::err(
            "La imagen es demasiado pequeña para segmentar por el borde");
    }

    cv::Mat blurred;
    cv::GaussianBlur(gray, blurred, cv::Size(5, 5), 0.0);
    cv::Mat dx;
    cv::Mat dy;
    cv::Sobel(blurred, dx, CV_32F, 1, 0, 3);
    cv::Sobel(blurred, dy, CV_32F, 0, 1, 3);
    cv::Mat magnitude;
    cv::magnitude(dx, dy, magnitude);

    // EL UMBRAL NO SE ADIVINA: se mide cuánto gradiente tiene el fondo en el
    // marco exterior. El canto de una pieza está órdenes de magnitud por encima
    // —medido, 4 contra 469—, así que la separación es holgada y no hay que
    // elegir un número bonito.
    const auto [noiseMedian, noiseSpread] = medianAndSpread(borderSample(magnitude));
    const double noise = std::max(noiseMedian + noiseSpread, 1.0);
    const double threshold = std::max(options.edgeSigmas * noise, 20.0);

    cv::Mat edges = magnitude > threshold;

    // Se engorda el trazo para tapar los huecos por donde se escaparía el
    // relleno. Un hueco no cuesta un trozo de pieza: cuesta la pieza entera.
    const int grow = std::max(0, options.closeRadiusPx);
    cv::Mat element;
    if (grow > 0) {
        element = cv::getStructuringElement(cv::MORPH_ELLIPSE,
                                            cv::Size(2 * grow + 3, 2 * grow + 3));
        cv::dilate(edges, edges, element, cv::Point(-1, -1), grow);
        cv::morphologyEx(edges, edges, cv::MORPH_CLOSE, element);
    }

    // INUNDAR DESDE FUERA. Lo que se alcanza desde el marco sin cruzar un borde
    // es fondo; lo que no, es pieza — con su interior entero, brille lo que
    // brille. Es justo lo que el nivel no puede contestar en estas escenas.
    cv::Mat outside;
    cv::bitwise_not(edges, outside);
    cv::Mat floodMask = cv::Mat::zeros(outside.rows + 2, outside.cols + 2, CV_8UC1);
    const int step = std::max(1, std::min(outside.cols, outside.rows) / 64);
    const auto seed = [&](int x, int y) {
        if (outside.at<unsigned char>(y, x) == 255) {
            cv::floodFill(outside, floodMask, cv::Point(x, y), cv::Scalar(128));
        }
    };
    for (int x = 0; x < outside.cols; x += step) {
        seed(x, 0);
        seed(x, outside.rows - 1);
    }
    for (int y = 0; y < outside.rows; y += step) {
        seed(0, y);
        seed(outside.cols - 1, y);
    }

    cv::Mat piece = outside != 128;
    if (grow > 0) {
        // Se le devuelve al fondo el grosor que le comió engordar el borde, o
        // toda pieza saldría más gorda de lo que es y las medidas con ella.
        cv::erode(piece, piece, element, cv::Point(-1, -1), grow);
    }
    cv::morphologyEx(piece, piece, cv::MORPH_OPEN,
                     cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5)));

    // Si no queda nada, se dice. Devolver una máscara vacía en silencio dejaría
    // al operador con «no se detecta pieza» y sin saber que el método no era el
    // adecuado para su escena.
    if (cv::countNonZero(piece) <
        static_cast<int>(options.minAreaFraction * static_cast<double>(gray.total()))) {
        return core::Result<cv::Mat>::err(
            "Segmentando por el borde no queda ninguna pieza: o los cantos no destacan "
            "sobre el fondo, o el borde se corta y el relleno se escapa. Prueba con el "
            "umbral por nivel.");
    }
    return core::Result<cv::Mat>::ok(std::move(piece));
}

SceneReading readScene(const cv::Mat& image) {
    SceneReading reading;
    const cv::Mat gray = toGray(image);
    if (gray.empty()) {
        reading.summary = "No se pudo leer la imagen.";
        return reading;
    }
    cv::Mat blurred;
    cv::GaussianBlur(gray, blurred, cv::Size(5, 5), 0.0);

    const auto [level, spread] = medianAndSpread(borderSample(blurred));
    reading.backgroundLevel = level;
    reading.backgroundNoise = spread;

    // «Apreciable» = más de lo que se mueve el propio fondo, con un suelo para
    // que un fondo perfectamente liso no cuente cada grano como pieza.
    const double band = std::max(4.0 * std::max(spread, 1.0), 12.0);
    // EL TECHO PUEDE CAER FUERA DEL RANGO. Con el fondo cerca de 255 no existe
    // ningún píxel que pueda pasarlo, así que la cuenta de «más claro» no da
    // cero: no da nada. Decirlo es lo que permite no apoyarse en ella.
    constexpr double kTopOfTheRange = 255.0;
    reading.brightSideIsUnmeasurable = level + band > kTopOfTheRange;
    cv::Mat brighter = blurred > (level + band);
    cv::Mat darker = blurred < (level - band);
    const double total = static_cast<double>(gray.total());
    reading.brighterThanBackground = cv::countNonZero(brighter) / total;
    reading.darkerThanBackground = cv::countNonZero(darker) / total;

    // Las dos partes tienen que pesar algo. Con una sola, Otsu tiene dos
    // poblaciones y hace su trabajo; el problema aparece cuando hay tres —fondo,
    // reflejos y sombras— y un solo corte no puede con ellas.
    constexpr double kMeaningfulSide = 0.01;
    reading.piecesStraddleTheBackground =
        reading.brighterThanBackground > kMeaningfulSide &&
        reading.darkerThanBackground > kMeaningfulSide;

    // EL SEGUNDO MOTIVO POR EL QUE UN CORTE ÚNICO FALLA, y el único que se podía
    // medir en las ocho imágenes reales: que el corte pase por dentro de la
    // pieza. `checkThresholdClipping` ya lo medía y nadie lo consultaba al
    // elegir método, así que el consejo se daba con la mitad de los datos.
    const ClippingCheck clipping = checkThresholdClipping(gray);
    reading.thresholdSwing = clipping.swing;
    reading.thresholdCutsThePiece = clipping.thresholdCutsThePiece;

    // OJO AL LISTÓN: para RECOMENDAR otro método hace falta más recorte que para
    // avisar de él. Ver `kSwingWorthChangingMethod`.
    reading.aSingleCutCannotDoIt = reading.piecesStraddleTheBackground ||
                                   reading.thresholdSwing > kSwingWorthChangingMethod;

    if (reading.piecesStraddleTheBackground) {
        reading.summary =
            "Hay partes más claras y más oscuras que la mesa a la vez. Un umbral único "
            "no puede separarlas: el corte que recoge unas deja fuera a otras. Aquí "
            "conviene segmentar por el borde.";
    } else if (reading.thresholdCutsThePiece) {
        // LA FRASE LA ESCRIBE EL PROPIO COMPROBADOR, con sus cifras. Redactar
        // aquí una segunda versión de lo mismo dejaría dos textos que se pueden
        // desincronizar, y el operador vería uno u otro según por dónde entre.
        reading.summary = clipping.summary +
                          " El borde no depende del nivel de gris, así que aquí conviene "
                          "más: una pieza brillante sale entera en vez de partida en "
                          "trozos o medida corta.";
    } else if (reading.brighterThanBackground + reading.darkerThanBackground < 0.005) {
        reading.summary = "Apenas hay nada que se aparte del fondo.";
    } else {
        reading.summary =
            "Las piezas caen todas del mismo lado del fondo, que es donde el umbral por "
            "nivel funciona bien.";
    }

    // Y SI EL LADO CLARO NO SE PUDO MIRAR, se dice — pero sin cambiar el
    // veredicto: el recorte sí se midió, y es lo que decide.
    if (reading.brightSideIsUnmeasurable) {
        reading.summary +=
            " (El fondo está a " + std::to_string(static_cast<int>(level)) +
            ", tan claro que no se puede saber si algo de la pieza es aún más claro: "
            "ese lado no se ha mirado.)";
    }
    return reading;
}

bool edgeSegmentationLooksBetter(const cv::Mat& image) {
    return readScene(image).aSingleCutCannotDoIt;
}


ClippingCheck checkThresholdClipping(const cv::Mat& image) {
    ClippingCheck check;
    if (image.empty()) {
        check.summary = "No hay imagen que mirar.";
        return check;
    }
    check.loosenedBy = kLoosenThresholdBy;

    cv::Mat gray;
    if (image.channels() == 3) {
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = image;
    }

    // El umbral que elige Otsu es el que usa el modo automático, que es el que
    // hay que juzgar. Con umbral manual el operador ya ha decidido y esto sigue
    // sirviendo igual: le dice si SU elección corta.
    cv::Mat blurred;
    cv::GaussianBlur(gray, blurred, cv::Size(5, 5), 0.0);
    cv::Mat scratch;
    const int otsu = static_cast<int>(
        cv::threshold(blurred, scratch, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU));

    // Hacia dónde se afloja depende de qué lado es la pieza. El marco exterior
    // es fondo, así que si el fondo está por encima del corte, la pieza es la
    // parte oscura y aflojar es SUBIR el corte.
    // EL NIVEL DEL FONDO, SIN PEDIR LA LECTURA ENTERA. Antes esto llamaba a
    // `readScene` para leer un solo campo; desde que `readScene` consulta el
    // recorte para aconsejar método, esa llamada sería recursión infinita. Y
    // aparte era trabajo de más: de toda la lectura aquí solo hace falta saber
    // de qué lado del corte cae la mesa.
    const double backgroundLevel = medianAndSpread(borderSample(blurred)).first;
    const bool pieceIsDark = backgroundLevel > otsu;
    const int loose = pieceIsDark ? std::min(254, otsu + kLoosenThresholdBy)
                                  : std::max(1, otsu - kLoosenThresholdBy);

    const auto totalArea = [&image](int threshold) {
        PipelineConfig config;
        config.segmentation.manualThreshold = threshold;
        auto all = analyzeFrames(image, config);
        if (!all.isOk()) {
            return 0.0;
        }
        double total = 0.0;
        for (const auto& piece : all.value()) {
            total += piece.contour.area;
        }
        return total;
    };

    const double tight = totalArea(otsu);
    const double relaxed = totalArea(loose);
    if (tight <= 0.0) {
        check.summary = "Con este umbral no se detecta ninguna pieza, así que no hay "
                        "nada de lo que decir si está cortado.";
        return check;
    }
    check.swing = (relaxed - tight) / tight;
    check.thresholdCutsThePiece = check.swing > kThresholdCutsTheSwing;

    char buffer[420];
    if (check.thresholdCutsThePiece) {
        std::snprintf(buffer, sizeof(buffer),
                      "EL UMBRAL ESTÁ CORTANDO LA PIEZA. Aflojándolo %d niveles aparece "
                      "un %.0f %% más de área, y eso significa que el corte cae DENTRO "
                      "de la pieza y no en su borde: hay partes suyas casi tan claras "
                      "como la mesa (una cabeza cromada, un canto pulido) y el umbral "
                      "las está dejando fuera.\n\n"
                      "Las medidas salen CORTAS y nada avisa, porque el contorno "
                      "recortado es perfectamente limpio.\n\n"
                      "Prueba a separar «por el canto de la pieza», o fija el umbral a "
                      "mano cerca de %d.",
                      check.loosenedBy, 100.0 * check.swing, loose);
    } else {
        std::snprintf(buffer, sizeof(buffer),
                      "El umbral cae en el borde de la pieza: aflojándolo %d niveles "
                      "solo aparece un %.0f %% más de área. No se está dejando pieza "
                      "fuera.",
                      check.loosenedBy, 100.0 * check.swing);
    }
    check.summary = buffer;
    return check;
}

}  // namespace pci::vision
