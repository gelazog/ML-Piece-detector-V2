#pragma once

#include <opencv2/core.hpp>

#include "core/result.h"

namespace pci::vision {

// Ángulo del eje principal de la máscara en grados [-180, 180), en coordenadas
// de imagen (x a la derecha, y hacia abajo). Se calcula con momentos centrales
// de segundo orden; la ambigüedad de 180° se resuelve con el signo del momento
// de tercer orden a lo largo del eje (asimetría de la pieza).
// Limitación conocida: inestable para piezas casi circulares o con simetría
// de rotación perfecta — irrelevante para comparación por embeddings.
core::Result<double> principalAngleDeg(const cv::Mat& mask);

// Igual, pero sobre unos momentos ya calculados. Existe porque recorrer la
// máscara cuesta: `computeFixture` necesitaba el centroide, la anisotropía y el
// ángulo, y cada uno pedía su propia pasada de `cv::moments` sobre millones de
// píxeles. Con los momentos compartidos, una pasada basta.
core::Result<double> principalAngleDeg(const cv::Moments& moments);

// Anisotropía de la máscara en [0,1] a partir de los momentos centrales de
// segundo orden: 0 = distribución circular (el eje principal no está
// definido), 1 = alargada como una línea. Sirve para saber cuándo confiar en
// el ángulo del eje principal.
double principalAnisotropy(const cv::Mat& mask);
double principalAnisotropy(const cv::Moments& moments);

}  // namespace pci::vision
