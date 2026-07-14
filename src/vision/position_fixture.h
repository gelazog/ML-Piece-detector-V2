#pragma once

#include <opencv2/core.hpp>

#include "core/result.h"
#include "vision/types.h"

namespace pci::vision {

// Fixture = centroide de la máscara + eje principal. Con autoOrient=false el
// ángulo se deja en 0 (pieza vertical): más estable y sin inclinación espuria.
core::Result<Fixture> computeFixture(const cv::Mat& mask, bool autoOrient = true);

// Cambio de coordenadas imagen <-> pieza (rotación + traslación, sin escala).
cv::Point2f toPieceCoords(const Fixture& fixture, const cv::Point2f& imagePoint);
cv::Point2f toImageCoords(const Fixture& fixture, const cv::Point2f& piecePoint);

// Rota la imagen para alinear el eje principal a 0°, elimina el fondo con la
// máscara, recorta la pieza y la centra escalada (aspecto preservado) en un
// lienzo canónico cuadrado. Esta salida alimenta los embeddings de la fase 3.
core::Result<cv::Mat> normalizePiece(const cv::Mat& image, const cv::Mat& mask,
                                     const Fixture& fixture, int canonicalSize = 256);

}  // namespace pci::vision
