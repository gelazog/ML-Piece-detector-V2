#include "vision/outlined_piece.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <string>

namespace pci::vision {
namespace {

// El recuadro del trazo, con un margen y recortado a la imagen.
//
// El margen no es decoración: la segmentación de dentro necesita ver algo de
// FONDO con el que contrastar. Ceñida al trazo, una pieza que lo llene entero
// deja al umbral sin dos poblaciones que separar y sale todo pieza o todo
// fondo. Es el mismo motivo por el que la Región se propone con holgura.
cv::Rect boxAround(const std::vector<cv::Point>& polygon, const cv::Size& frame) {
    cv::Rect box = cv::boundingRect(polygon);
    const int margin = std::max(8, std::max(box.width, box.height) / 6);
    box.x -= margin;
    box.y -= margin;
    box.width += 2 * margin;
    box.height += 2 * margin;
    return box & cv::Rect(0, 0, frame.width, frame.height);
}

}  // namespace

OutlinedPiece pieceInsideOutline(const cv::Mat& frame, const std::vector<cv::Point>& polygon,
                                 const SegmentationOptions& options) {
    OutlinedPiece result;
    if (frame.empty() || polygon.size() < 3) {
        result.why = "Ese trazo no encierra ninguna zona.";
        return result;
    }

    // El trazo, como máscara. Es el suelo del resultado: pase lo que pase con la
    // detección, la pieza marcada a mano es al menos esto.
    cv::Mat outline(frame.size(), CV_8UC1, cv::Scalar(0));
    cv::fillPoly(outline, std::vector<std::vector<cv::Point>>{polygon}, cv::Scalar(255));
    const double outlineArea = cv::countNonZero(outline);
    if (outlineArea <= 0.0) {
        result.why = "Ese trazo no encierra ninguna zona.";
        return result;
    }
    result.mask = outline;

    const cv::Rect box = boxAround(polygon, frame.size());
    if (box.width < 8 || box.height < 8) {
        result.why = "La zona marcada es demasiado pequeña para buscar un borde dentro.";
        return result;
    }

    // Se segmenta SOLO ese recorte, con los mismos ajustes que la aplicación.
    // Mirar de cerca es todo el truco: una pieza que el umbral global se deja
    // fuera —porque la bandeja tiene otro nivel, porque la sombra se la come—
    // suele aparecer cuando el umbral se calcula con lo que hay alrededor de
    // ella y no con la foto entera.
    auto segmented = segmentPiece(frame(box).clone(), options);
    if (!segmented.isOk()) {
        result.why = "Dentro de la zona marcada no se pudo separar nada del fondo: " +
                     segmented.error().message + " Se usa el trazo tal cual.";
        return result;
    }

    // Lo de dentro del trazo, y nada más: el recuadro lleva margen a propósito y
    // ahí puede caer la pieza de al lado.
    cv::Mat local(frame.size(), CV_8UC1, cv::Scalar(0));
    segmented.value().copyTo(local(box));
    cv::bitwise_and(local, outline, local);

    std::vector<std::vector<cv::Point>> blobs;
    cv::findContours(local, blobs, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
    if (blobs.empty()) {
        result.why =
            "Dentro de la zona marcada no se ve ningún borde que separar del fondo. Se "
            "usa el trazo tal cual: vale para contar la pieza, pero sus cotas serían las "
            "del pulso de quien lo dibujó.";
        return result;
    }
    const auto& biggest = *std::max_element(
        blobs.begin(), blobs.end(),
        [](const auto& a, const auto& b) { return cv::contourArea(a) < cv::contourArea(b); });

    cv::Mat piece(frame.size(), CV_8UC1, cv::Scalar(0));
    // Con los agujeros rellenos: lo que se fuerza es la SILUETA. Los huecos
    // interiores los vuelve a encontrar el análisis, como en cualquier pieza.
    cv::fillPoly(piece, std::vector<std::vector<cv::Point>>{biggest}, cv::Scalar(255));

    result.fillFraction = static_cast<double>(cv::countNonZero(piece)) / outlineArea;
    if (result.fillFraction < kOutlineMinFill) {
        result.why = "Lo que se ve dentro de la zona marcada ocupa solo el " +
                     std::to_string(static_cast<int>(std::lround(100.0 * result.fillFraction))) +
                     " % de ella: eso es una mota o un reflejo, no la pieza que rodeaste. "
                     "Se usa el trazo tal cual.";
        return result;
    }
    if (result.fillFraction > kOutlineMaxFill) {
        result.why = "Dentro de la zona marcada no hay dos niveles que separar —lo "
                     "detectado ocupa el " +
                     std::to_string(static_cast<int>(std::lround(100.0 * result.fillFraction))) +
                     " % del trazo—, así que el borde saldría del trazo y no de la "
                     "imagen. Se usa el trazo tal cual.";
        return result;
    }

    result.mask = piece;
    result.detected = true;
    result.why = "Borde encontrado dentro de la zona marcada: ocupa el " +
                 std::to_string(static_cast<int>(std::lround(100.0 * result.fillFraction))) +
                 " % del trazo, así que sus cotas salen de la imagen y no del trazo.";
    return result;
}

}  // namespace pci::vision
