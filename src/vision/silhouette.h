#pragma once

#include <algorithm>
#include <utility>
#include <vector>

#include <opencv2/imgproc.hpp>

namespace pci::vision {

// LA SILUETA DE UN RECORTE: DOS PASOS QUE ESTABAN COPIADOS A MANO.
//
// Existen porque el editor repetía, herramienta por herramienta, las mismas
// líneas: diez copias de «Otsu sobre el gris con la polaridad de la
// herramienta» en `tool_executor.cpp`, y ocho de «findContours exterior y
// quedarse con el de mayor área» repartidas entre el ejecutor, `auto_measure`,
// `measure_recipe` y `outlined_piece`. Eran idénticas letra a letra; que sigan
// siéndolo es lo que vigila tests/test_silhouette_helpers.cpp.
//
// Lo que NO se ha traído aquí, a propósito, son las variantes que parecen la
// misma y no lo son: RETR_CCOMP quedándose solo con los exteriores, CHAIN_APPROX_SIMPLE,
// un desempate con `>` estricto desde cero, o un filtro de área antes de elegir.
// Cambian qué contorno sale o con cuántos puntos, y cada una lleva al lado un
// comentario que dice en qué se diferencia.

// Otsu sobre un gris de un canal, con la pieza en blanco (255).
//
// `pieceIsDark` elige la polaridad: con pieza oscura sobre fondo claro se
// invierte el corte. Es exactamente
// `threshold(gray, out, 0, 255, (dark ? BINARY_INV : BINARY) | OTSU)`; el
// umbral que elige Otsu se descarta, porque ninguno de los sitios que usan
// esta función lo quería.
[[nodiscard]] inline cv::Mat otsuMask(const cv::Mat& gray, bool pieceIsDark) {
    cv::Mat binary;
    cv::threshold(gray, binary, 0.0, 255.0,
                  (pieceIsDark ? cv::THRESH_BINARY_INV : cv::THRESH_BINARY) | cv::THRESH_OTSU);
    return binary;
}

// El contorno EXTERIOR de mayor área de una máscara, con todos sus píxeles
// (RETR_EXTERNAL + CHAIN_APPROX_NONE). Vacío si la máscara no tiene ninguno:
// `findContours` nunca devuelve un contorno sin puntos, así que vacío quiere
// decir «no hay nada» y nada más.
//
// Con dos contornos de la misma área gana el PRIMERO que devuelve
// `findContours`, que es lo que hacía `std::max_element` en todas las copias.
[[nodiscard]] inline std::vector<cv::Point> largestOuterContour(const cv::Mat& mask) {
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
    if (contours.empty()) {
        return {};
    }
    auto biggest = std::max_element(
        contours.begin(), contours.end(),
        [](const auto& a, const auto& b) { return cv::contourArea(a) < cv::contourArea(b); });
    return std::move(*biggest);
}

}  // namespace pci::vision
