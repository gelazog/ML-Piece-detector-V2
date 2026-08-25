#include "vision/segmentation.h"

#include "vision/edge_segmentation.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace pci::vision {

namespace {

// Media de los píxeles del marco exterior de una máscara binaria.
double borderMean(const cv::Mat& binary) {
    const int border = std::max(1, std::min(binary.rows, binary.cols) / 50);
    double sum = 0.0;
    std::int64_t count = 0;

    auto accumulate = [&](const cv::Mat& region) {
        sum += cv::sum(region)[0];
        count += static_cast<std::int64_t>(region.total());
    };

    accumulate(binary.rowRange(0, border));
    accumulate(binary.rowRange(binary.rows - border, binary.rows));
    accumulate(binary.colRange(0, border));
    accumulate(binary.colRange(binary.cols - border, binary.cols));

    return count > 0 ? sum / static_cast<double>(count) : 0.0;
}

// LO QUE EL BRILLO SE LLEVÓ, DEVUELTO SIN DEJAR ENTRAR EL FONDO.
//
// `seeds` es lo que ya se sabe que es pieza. Se corta otra vez con el umbral
// aflojado `by` niveles hacia el lado de la pieza y se conserva SOLO lo que
// toca una semilla. Es la histéresis de Canny llevada al nivel de gris: un
// píxel dudoso cuenta si está conectado a uno seguro.
//
// Y ahí está el porqué de que no meta fondo: la mesa aflojada tampoco toca
// ninguna semilla, porque las semillas son pieza. Sube el brillo de la propia
// cara de la pieza —que está pegado a ella— y no la sombra pegada a la mesa.
cv::Mat recoverWhatTheGlareTook(const cv::Mat& blurred, const cv::Mat& seeds,
                                double threshold, int by, bool pieceIsTheDarkSide,
                                const cv::Mat& kernel) {
    // AFLOJAR ES MOVER EL CORTE HACIA LA PIEZA. Con la pieza oscura eso es SUBIR
    // el corte (más gris pasa a ser pieza); con la pieza clara, bajarlo.
    const double loose = pieceIsTheDarkSide ? std::min(254.0, threshold + by)
                                            : std::max(1.0, threshold - by);
    cv::Mat wide;
    cv::threshold(blurred, wide, loose, 255.0, cv::THRESH_BINARY);
    if (pieceIsTheDarkSide) {
        cv::bitwise_not(wide, wide);
    }

    // Se etiqueta lo aflojado y se marcan las etiquetas que contienen semilla.
    // Con `connectedComponents` en vez de reconstrucción morfológica iterada
    // porque una pieza larga necesitaría decenas de dilataciones para que la
    // semilla llegue a la punta, y esto lo resuelve en una pasada.
    cv::Mat labels;
    const int count = cv::connectedComponents(wide, labels, 8, CV_32S);
    if (count <= 1) {
        return seeds;
    }
    std::vector<unsigned char> alive(static_cast<std::size_t>(count), 0);
    for (int y = 0; y < labels.rows; ++y) {
        const auto* label = labels.ptr<int>(y);
        const auto* seed = seeds.ptr<unsigned char>(y);
        for (int x = 0; x < labels.cols; ++x) {
            if (seed[x] != 0 && label[x] > 0) {
                alive[static_cast<std::size_t>(label[x])] = 1;
            }
        }
    }

    cv::Mat grown = cv::Mat::zeros(seeds.size(), CV_8UC1);
    for (int y = 0; y < labels.rows; ++y) {
        const auto* label = labels.ptr<int>(y);
        auto* out = grown.ptr<unsigned char>(y);
        for (int x = 0; x < labels.cols; ++x) {
            if (label[x] > 0 && alive[static_cast<std::size_t>(label[x])] != 0) {
                out[x] = 255;
            }
        }
    }
    // La misma limpieza que llevaba la máscara estricta: al crecer se recogen
    // dientes de sierra del borde aflojado, y dejarlos haría que el perímetro
    // dependiera de si esta opción está puesta.
    if (!kernel.empty()) {
        cv::morphologyEx(grown, grown, cv::MORPH_OPEN, kernel);
        cv::morphologyEx(grown, grown, cv::MORPH_CLOSE, kernel);
    }
    return grown;
}

}  // namespace

core::Result<cv::Mat> segmentPiece(const cv::Mat& image, const SegmentationOptions& options) {
    if (image.empty()) {
        return core::Result<cv::Mat>::err("Imagen vacía");
    }

    cv::Mat gray;
    switch (image.channels()) {
        case 1:
            gray = image;
            break;
        case 3:
            cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
            break;
        default:
            return core::Result<cv::Mat>::err("Formato de imagen no soportado: " +
                                              std::to_string(image.channels()) + " canales");
    }

    // Segmentar por el CANTO es un camino aparte: no hay corte de gris que
    // ajustar, así que ni el umbral ni la polaridad tienen nada que decir.
    if (options.method == SegmentationMethod::Edges) {
        EdgeSegmentationOptions edges;
        auto mask = segmentByEdges(gray, edges);
        if (!mask.isOk()) {
            return mask;
        }
        // La limpieza morfológica sí se comparte: quitar grano suelto y cerrar
        // huecos pequeños vale igual venga la máscara de donde venga.
        const int morphology = options.morphKernel | 1;
        if (morphology >= 3) {
            const cv::Mat kernel = cv::getStructuringElement(
                cv::MORPH_ELLIPSE, cv::Size(morphology, morphology));
            cv::morphologyEx(mask.value(), mask.value(), cv::MORPH_OPEN, kernel);
            cv::morphologyEx(mask.value(), mask.value(), cv::MORPH_CLOSE, kernel);
        }
        return mask;
    }

    cv::Mat blurred;
    const int blur = options.blurKernel | 1;  // los kernels deben ser impares
    if (blur >= 3) {
        cv::GaussianBlur(gray, blurred, cv::Size(blur, blur), 0.0);
    } else {
        blurred = gray;
    }

    // Umbral: automático (Otsu) o fijo elegido por el usuario cuando la
    // iluminación engaña al automático.
    cv::Mat binary;
    double usedThreshold = 0.0;
    if (options.manualThreshold >= 0) {
        usedThreshold = static_cast<double>(options.manualThreshold);
        cv::threshold(blurred, binary, usedThreshold, 255.0, cv::THRESH_BINARY);
    } else {
        usedThreshold =
            cv::threshold(blurred, binary, 0.0, 255.0, cv::THRESH_BINARY | cv::THRESH_OTSU);
    }

    // Polaridad: dejar pieza = 255. THRESH_BINARY marca en blanco lo claro.
    //
    // Se GUARDA la decisión además de aplicarla: quien afloje el corte después
    // tiene que invertir igual, y volver a mirar el marco sobre una máscara
    // distinta podría decidir lo contrario y dejar las dos del revés.
    bool pieceIsTheDarkSide = false;
    switch (options.polarity) {
        case SegmentationPolarity::Auto:
            // El fondo domina el marco exterior: si quedó blanco, invertir.
            pieceIsTheDarkSide = borderMean(binary) > 127.0;
            break;
        case SegmentationPolarity::DarkPiece:
            pieceIsTheDarkSide = true;
            break;
        case SegmentationPolarity::LightPiece:
            pieceIsTheDarkSide = false;
            break;
    }
    if (pieceIsTheDarkSide) {
        cv::bitwise_not(binary, binary);
    }

    // Apertura elimina ruido suelto; cierre rellena huecos pequeños de la pieza.
    const int morph = options.morphKernel | 1;
    const cv::Mat kernel =
        morph >= 3 ? cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(morph, morph))
                   : cv::Mat();
    if (!kernel.empty()) {
        cv::morphologyEx(binary, binary, cv::MORPH_OPEN, kernel);
        cv::morphologyEx(binary, binary, cv::MORPH_CLOSE, kernel);
    }

    // RECUPERAR EL MATERIAL QUE EL BRILLO SE LLEVÓ. Ver `recoverHighlightsBy`.
    //
    // Va DESPUÉS de la morfología a propósito: las semillas tienen que estar ya
    // limpias. Con el grano suelto todavía dentro, cualquier mota de ruido
    // pegada al fondo sería una semilla y arrastraría media mesa.
    if (options.recoverHighlightsBy > 0) {
        binary = recoverWhatTheGlareTook(blurred, binary, usedThreshold,
                                         options.recoverHighlightsBy, pieceIsTheDarkSide,
                                         kernel);
    }

    return core::Result<cv::Mat>::ok(std::move(binary));
}

}  // namespace pci::vision
