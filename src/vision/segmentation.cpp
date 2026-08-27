#include "vision/segmentation.h"

#include "vision/edge_segmentation.h"
#include "vision/gray.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
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

cv::Vec3b estimateBackgroundColour(const cv::Mat& image) {
    if (image.empty()) {
        return {0, 0, 0};
    }
    cv::Mat bgr;
    if (image.channels() == 1) {
        cv::cvtColor(image, bgr, cv::COLOR_GRAY2BGR);
    } else if (image.channels() == 3) {
        bgr = image;
    } else {
        return {0, 0, 0};
    }
    // Un marco proporcional, no un número de píxeles: una banda de 8 px es todo
    // en una imagen de webcam y nada en una de 4000 px de ancho.
    const int band = std::max(2, std::min(bgr.rows, bgr.cols) / 25);
    std::array<std::vector<unsigned char>, 3> channels;
    for (int y = 0; y < bgr.rows; ++y) {
        const bool horizontalBand = y < band || y >= bgr.rows - band;
        for (int x = 0; x < bgr.cols; ++x) {
            if (!horizontalBand && x >= band && x < bgr.cols - band) {
                continue;
            }
            const cv::Vec3b pixel = bgr.at<cv::Vec3b>(y, x);
            for (int c = 0; c < 3; ++c) {
                channels[static_cast<std::size_t>(c)].push_back(pixel[c]);
            }
        }
    }
    cv::Vec3b median{0, 0, 0};
    for (int c = 0; c < 3; ++c) {
        auto& values = channels[static_cast<std::size_t>(c)];
        if (values.empty()) {
            continue;
        }
        const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
        std::nth_element(values.begin(), middle, values.end());
        median[c] = *middle;
    }
    return median;
}

double backgroundColourfulness(const cv::Vec3b& background) {
    const int high = std::max({background[0], background[1], background[2]});
    const int low = std::min({background[0], background[1], background[2]});
    if (high == 0) {
        return 0.0;  // negro: no hay tono del que hablar
    }
    return static_cast<double>(high - low) / high;
}

cv::Mat distanceToBackground(const cv::Mat& bgr, const cv::Vec3b& background) {
    if (bgr.empty() || bgr.channels() != 3) {
        return {};
    }
    cv::Mat lab;
    cv::cvtColor(bgr, lab, cv::COLOR_BGR2Lab);
    cv::Mat onePixel(1, 1, CV_8UC3, cv::Scalar(background[0], background[1], background[2]));
    cv::Mat backgroundLab;
    cv::cvtColor(onePixel, backgroundLab, cv::COLOR_BGR2Lab);
    const cv::Vec3b reference = backgroundLab.at<cv::Vec3b>(0, 0);

    cv::Mat distance(bgr.size(), CV_8UC1);
    for (int y = 0; y < lab.rows; ++y) {
        const auto* row = lab.ptr<cv::Vec3b>(y);
        auto* out = distance.ptr<unsigned char>(y);
        for (int x = 0; x < lab.cols; ++x) {
            const double dL = static_cast<double>(row[x][0]) - reference[0];
            const double dA = static_cast<double>(row[x][1]) - reference[1];
            const double dB = static_cast<double>(row[x][2]) - reference[2];
            const double d = std::sqrt(dL * dL + dA * dA + dB * dB);
            out[x] = static_cast<unsigned char>(std::min(255.0, d));
        }
    }
    return distance;
}

core::Result<cv::Mat> segmentPiece(const cv::Mat& image, const SegmentationOptions& options) {
    if (image.empty()) {
        return core::Result<cv::Mat>::err("Imagen vacía");
    }

    // La cuarta copia de «pasar a gris» que tenía este módulo. Ahora es la de
    // `vision/gray.h`, que además convierte el BGRA que aquí se rechazaba — y
    // BGRA es lo que entrega más de una fuente de imagen.
    cv::Mat gray = toGray(image);
    if (gray.empty()) {
        return core::Result<cv::Mat>::err("Formato de imagen no soportado: " +
                                          std::to_string(image.channels()) + " canales");
    }

    // AQUÍ SE DECIDE QUÉ CANAL SE SEGMENTA.
    //
    // Hasta esta línea, lo primero que hacía el programa con una foto en color
    // era tirar el color. Con la clave de fondo encendida, en vez de la claridad
    // se segmenta la DISTANCIA AL COLOR DEL FONDO, y a partir de ahí todo sigue
    // exactamente igual: mismo Otsu, misma morfología, misma recuperación por
    // histéresis. Lo único que cambia es qué mide cada píxel.
    //
    // La polaridad deja de ser una pregunta: en una imagen de distancias, la
    // pieza es siempre lo que está LEJOS del fondo. Se fija aquí en vez de
    // dejarla a la detección automática, que se equivocaría la mitad de las
    // veces sin motivo.
    SegmentationOptions active = options;
    if (options.backgroundKey != SegmentationOptions::BackgroundKey::Off &&
        image.channels() == 3) {
        const cv::Vec3b background =
            options.backgroundKey == SegmentationOptions::BackgroundKey::Fixed
                ? options.background
                : estimateBackgroundColour(image);
        cv::Mat distance = distanceToBackground(image, background);
        if (!distance.empty()) {
            gray = std::move(distance);
            active.polarity = SegmentationPolarity::LightPiece;
        }
    }

    // Segmentar por el CANTO es un camino aparte: no hay corte de gris que
    // ajustar, así que ni el umbral ni la polaridad tienen nada que decir.
    if (active.method == SegmentationMethod::Edges) {
        EdgeSegmentationOptions edges;
        auto mask = segmentByEdges(gray, edges);
        if (!mask.isOk()) {
            return mask;
        }
        // La limpieza morfológica sí se comparte: quitar grano suelto y cerrar
        // huecos pequeños vale igual venga la máscara de donde venga.
        const int morphology = active.morphKernel | 1;
        if (morphology >= 3) {
            const cv::Mat kernel = cv::getStructuringElement(
                cv::MORPH_ELLIPSE, cv::Size(morphology, morphology));
            cv::morphologyEx(mask.value(), mask.value(), cv::MORPH_OPEN, kernel);
            cv::morphologyEx(mask.value(), mask.value(), cv::MORPH_CLOSE, kernel);
        }
        return mask;
    }

    cv::Mat blurred;
    const int blur = active.blurKernel | 1;  // los kernels deben ser impares
    if (blur >= 3) {
        cv::GaussianBlur(gray, blurred, cv::Size(blur, blur), 0.0);
    } else {
        blurred = gray;
    }

    // Umbral: automático (Otsu) o fijo elegido por el usuario cuando la
    // iluminación engaña al automático.
    cv::Mat binary;
    double usedThreshold = 0.0;
    if (active.manualThreshold >= 0) {
        usedThreshold = static_cast<double>(active.manualThreshold);
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
    switch (active.polarity) {
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
    const int morph = active.morphKernel | 1;
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
    if (active.recoverHighlightsBy > 0) {
        binary = recoverWhatTheGlareTook(blurred, binary, usedThreshold,
                                         active.recoverHighlightsBy, pieceIsTheDarkSide,
                                         kernel);
    }

    return core::Result<cv::Mat>::ok(std::move(binary));
}

}  // namespace pci::vision
