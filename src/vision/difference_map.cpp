#include "vision/difference_map.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>

namespace pci::vision {

namespace {

// Por debajo de esta intensidad no se pinta nada encima de la pieza. Ver
// `paintDifference`.
constexpr double kNothingToShow = 0.05;

cv::Mat toGray(const cv::Mat& image) {
    if (image.empty()) {
        return {};
    }
    cv::Mat gray;
    switch (image.channels()) {
        case 1:
            gray = image.clone();
            break;
        case 3:
            cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
            break;
        case 4:
            cv::cvtColor(image, gray, cv::COLOR_BGRA2GRAY);
            break;
        default:
            return {};
    }
    if (gray.depth() != CV_8U) {
        gray.convertTo(gray, CV_8U);
    }
    return gray;
}

// Iguala el brillo y el contraste globales de `image` a los de `like`, usando
// solo los píxeles de la pieza.
//
// Hace falta porque el recorte normalizado conserva la exposición de la foto: la
// misma pieza fotografiada dos veces con la luz un poco distinta da una resta
// encendida DE PUNTA A PUNTA, y ese mapa señala la pieza entera, que es lo mismo
// que no señalar nada.
cv::Mat matchLevels(const cv::Mat& image, const cv::Mat& like, const cv::Mat& mask) {
    cv::Scalar meanA;
    cv::Scalar stdA;
    cv::Scalar meanB;
    cv::Scalar stdB;
    cv::meanStdDev(image, meanA, stdA, mask);
    cv::meanStdDev(like, meanB, stdB, mask);
    // Con contraste casi nulo, escalar dispararía el ruido. Se iguala solo el
    // nivel medio y se deja el contraste como está.
    const double scale = stdA[0] > 1.0 ? stdB[0] / stdA[0] : 1.0;
    const double shift = meanB[0] - meanA[0] * scale;
    cv::Mat levelled;
    image.convertTo(levelled, CV_8U, scale, shift);
    return levelled;
}

}  // namespace

DifferenceMap compareToReference(const cv::Mat& current, const cv::Mat& reference,
                                 const DifferenceOptions& options) {
    DifferenceMap result;
    cv::Mat now = toGray(current);
    cv::Mat before = toGray(reference);
    if (now.empty() || before.empty()) {
        result.problem = "No hay recorte con el que comparar.";
        return result;
    }
    if (before.size() != now.size()) {
        cv::resize(before, before, now.size(), 0.0, 0.0, cv::INTER_AREA);
    }

    // La pieza es lo que no es fondo. El recorte canónico trae el fondo a cero,
    // así que esto separa pieza de hueco sin necesitar la máscara original.
    cv::Mat mask;
    cv::threshold(now, mask, 1.0, 255.0, cv::THRESH_BINARY);
    cv::Mat referenceMask;
    cv::threshold(before, referenceMask, 1.0, 255.0, cv::THRESH_BINARY);
    cv::bitwise_or(mask, referenceMask, mask);
    if (cv::countNonZero(mask) < 50) {
        result.problem = "El recorte está prácticamente vacío.";
        return result;
    }

    now = matchLevels(now, before, mask);

    // COMPARACIÓN TOLERANTE. Cada píxel se enfrenta al RANGO que la referencia
    // toma en su vecindad, no a un único valor: si cae dentro, no se enciende.
    // Es lo que distingue «esto está un píxel desplazado» de «esto no estaba».
    // `tolerancePx` es un RADIO, así que el elemento mide el doble más uno. Lo
    // decía la cabecera —«cuánto desalineamiento se perdona»— y aquí estaba
    // usándose como diámetro: con 3 px declarados solo se perdonaba 1, y la
    // misma pieza movida 2 px encendía el mapa al 0,47.
    const int reach = 2 * std::max(1, options.tolerancePx) + 1;
    const cv::Mat element =
        cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(reach, reach));
    cv::Mat high;
    cv::Mat low;
    cv::dilate(before, high, element);
    cv::erode(before, low, element);

    cv::Mat above;
    cv::Mat under;
    cv::subtract(now, high, above);  // saturada: 0 donde no sobresale
    cv::subtract(low, now, under);
    cv::Mat difference = cv::max(above, under);

    // Suelo de ruido: por debajo, dos fotos idénticas ya tendrían relieve.
    cv::subtract(difference, cv::Scalar(options.noiseFloor), difference);
    // Fuera de la pieza no hay nada que comparar; el borde del recorte siempre
    // tiene salto y encenderlo señalaría el contorno en todas las piezas.
    cv::Mat inside;
    cv::erode(mask, inside, element);
    difference.setTo(cv::Scalar(0), inside == 0);

    const int smooth = std::max(1, options.smoothPx) | 1;
    if (smooth >= 3) {
        cv::GaussianBlur(difference, difference, cv::Size(smooth, smooth), 0.0);
    }

    double peak = 0.0;
    cv::Point where;
    cv::minMaxLoc(difference, nullptr, &peak, nullptr, &where);
    result.heat = difference;
    result.worst = where;
    result.worstValue = peak / 255.0;
    if (peak > 0.0) {
        cv::Mat lit;
        cv::threshold(difference, lit, peak * 0.5, 255.0, cv::THRESH_BINARY);
        const double pieceArea = cv::countNonZero(inside);
        result.litFraction =
            pieceArea > 0.0 ? cv::countNonZero(lit) / pieceArea : 0.0;
    }
    result.ok = true;
    return result;
}

cv::Mat paintDifference(const cv::Mat& current, const DifferenceMap& map, double opacity) {
    if (current.empty() || !map.ok || map.heat.empty()) {
        return current;
    }
    cv::Mat base;
    if (current.channels() == 1) {
        cv::cvtColor(current, base, cv::COLOR_GRAY2BGR);
    } else if (current.channels() == 4) {
        cv::cvtColor(current, base, cv::COLOR_BGRA2BGR);
    } else {
        base = current.clone();
    }
    // NADA QUE ENSEÑAR, NADA QUE PINTAR.
    //
    // Va antes que el realce de abajo y es su condición: el mapa se va a estirar
    // hasta su propio máximo, y sin este corte una pieza limpia enseñaría un
    // faro encendido sobre su píxel más ruidoso. Un mapa que siempre señala algo
    // enseña a ignorarlo.
    if (map.worstValue < kNothingToShow) {
        return base;
    }

    cv::Mat heat = map.heat;
    if (heat.size() != base.size()) {
        cv::resize(heat, heat, base.size(), 0.0, 0.0, cv::INTER_LINEAR);
    }
    // Se estira hasta el máximo del propio mapa. El trabajo de esta imagen es
    // decir DÓNDE, no cuánto: pintar un defecto que puntúa 0,36 al 36 % de
    // intensidad lo deja apenas visible justo cuando hay algo que mirar. La
    // gravedad la lleva el número —`worstValue`—, que va escrito al lado.
    double peak = 0.0;
    cv::minMaxLoc(heat, nullptr, &peak);
    if (peak > 1.0) {
        heat.convertTo(heat, CV_8U, 255.0 / peak);
    }

    // COLORMAP_INFERNO y no JET: el jet tiene un pico de brillo en el amarillo
    // intermedio, así que un aviso de gravedad media se ve MÁS que uno grave.
    // El inferno crece en claridad de forma monótona, y en un mapa que dice
    // «esto es lo peor» eso no es un detalle estético.
    cv::Mat coloured;
    cv::applyColorMap(heat, coloured, cv::COLORMAP_INFERNO);

    // La mezcla se pesa CON EL PROPIO MAPA: donde no hay diferencia se ve la
    // pieza tal cual, y el color solo aparece donde hay algo que enseñar. Una
    // mezcla uniforme teñiría la pieza entera de morado oscuro y taparía
    // precisamente lo que se quiere mirar.
    cv::Mat weight;
    heat.convertTo(weight, CV_32F, opacity / 255.0);
    cv::Mat merged(base.size(), CV_8UC3);
    for (int y = 0; y < base.rows; ++y) {
        const auto* baseRow = base.ptr<cv::Vec3b>(y);
        const auto* colourRow = coloured.ptr<cv::Vec3b>(y);
        const auto* weightRow = weight.ptr<float>(y);
        auto* out = merged.ptr<cv::Vec3b>(y);
        for (int x = 0; x < base.cols; ++x) {
            const float w = std::clamp(weightRow[x], 0.0F, 1.0F);
            for (int c = 0; c < 3; ++c) {
                out[x][c] = cv::saturate_cast<unsigned char>(baseRow[x][c] * (1.0F - w) +
                                                             colourRow[x][c] * w);
            }
        }
    }

    // Y una marca en el punto peor: un mapa de calor dice «por aquí», y el
    // operador necesita «aquí» para poder ir a mirar la pieza con la mano.
    if (map.worst.x >= 0 && map.worstValue > 0.0) {
        cv::circle(merged, map.worst, 12, cv::Scalar(255, 255, 255), 2, cv::LINE_AA);
        cv::circle(merged, map.worst, 13, cv::Scalar(0, 0, 0), 1, cv::LINE_AA);
    }
    return merged;
}

}  // namespace pci::vision
