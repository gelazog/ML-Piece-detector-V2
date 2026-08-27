#include "vision/contour_analysis.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <utility>
#include <vector>

namespace pci::vision {

namespace {

// Rellena los datos derivados de un contorno ya aceptado.
PieceContour describe(const std::vector<cv::Point>& points, double area) {
    PieceContour result;
    result.points = points;
    const cv::Moments m = cv::moments(points);
    if (m.m00 > 0.0) {
        result.centroid = {static_cast<float>(m.m10 / m.m00),
                           static_cast<float>(m.m01 / m.m00)};
    }
    result.area = area;
    result.perimeter = cv::arcLength(points, true);
    result.rotatedRect = cv::minAreaRect(points);
    return result;
}

}  // namespace

cv::Mat splitTouchingPieces(const cv::Mat& mask, double coreRatio) {
    if (mask.empty()) {
        return mask;
    }
    std::vector<std::vector<cv::Point>> blobs;
    cv::findContours(mask, blobs, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    if (blobs.empty()) {
        return mask;
    }

    cv::Mat result = mask.clone();
    for (const auto& blob : blobs) {
        const cv::Rect box = cv::boundingRect(blob) & cv::Rect(0, 0, mask.cols, mask.rows);
        if (box.width < 6 || box.height < 6) {
            continue;
        }
        // La mancha sola, en su propia caja: la distancia se mide DENTRO de
        // ella y no contra las vecinas.
        cv::Mat local = cv::Mat::zeros(box.size(), CV_8UC1);
        std::vector<std::vector<cv::Point>> one{blob};
        for (auto& point : one.front()) {
            point -= box.tl();
        }
        cv::drawContours(local, one, 0, cv::Scalar(255), cv::FILLED);

        cv::Mat distance;
        cv::distanceTransform(local, distance, cv::DIST_L2, 5);
        double peak = 0.0;
        cv::minMaxLoc(distance, nullptr, &peak);
        if (peak <= 2.0) {
            continue;  // demasiado fina para tener corazón
        }
        cv::Mat cores;
        cv::threshold(distance, cores, coreRatio * peak, 255.0, cv::THRESH_BINARY);
        cores.convertTo(cores, CV_8U);

        std::vector<std::vector<cv::Point>> found;
        cv::findContours(cores, found, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        // Un corazón de verdad tiene cuerpo. Sin este filtro, un pico de ruido
        // en la punta de un diente contaría como pieza.
        const double blobArea = static_cast<double>(cv::countNonZero(local));
        std::vector<std::vector<cv::Point>> real;
        for (const auto& core : found) {
            if (cv::contourArea(core) >= kTouchingCoreMinFraction * blobArea) {
                real.push_back(core);
            }
        }
        if (real.size() < 2) {
            continue;  // una sola pieza: no se toca
        }

        // Watershed SOBRE LA DISTANCIA, no sobre la máscara.
        //
        // La primera versión lo corría sobre la máscara binaria, y ahí no hay
        // gradiente que seguir: dentro de la pieza todo vale lo mismo. Medido,
        // dibujaba una línea —quitaba 1 713 píxeles— pero no llegaba a cortar,
        // y los dos engranajes seguían saliendo como un solo contorno.
        //
        // Con la distancia invertida sí hay relieve: el cuello donde las dos
        // piezas se tocan es una CRESTA, porque ahí la distancia al fondo cae.
        // El watershed está hecho justo para partir por las crestas.
        cv::Mat relief;
        cv::normalize(distance, relief, 0.0, 255.0, cv::NORM_MINMAX);
        relief.convertTo(relief, CV_8U);
        cv::bitwise_not(relief, relief);  // el cuello, arriba
        cv::Mat colour;
        cv::cvtColor(relief, colour, cv::COLOR_GRAY2BGR);

        cv::Mat markers = cv::Mat::zeros(box.size(), CV_32S);
        for (std::size_t i = 0; i < real.size(); ++i) {
            cv::drawContours(markers, real, static_cast<int>(i),
                             cv::Scalar(static_cast<double>(i) + 2.0), cv::FILLED);
        }
        // El fondo, marcado como 1: sin una semilla de fondo el watershed no
        // sabe dónde termina la pieza y reparte todo el recorte entre las dos.
        markers.setTo(cv::Scalar(1), local == 0);
        cv::watershed(colour, markers);

        // Las fronteras (-1) se abren en la máscara. Se ENGORDAN a tres píxeles
        // porque una línea de uno deja las dos mitades tocando en diagonal, y
        // `findContours` con conectividad de 8 las vuelve a unir — que es
        // exactamente lo que pasó con la primera versión.
        cv::Mat border = (markers == -1);
        cv::dilate(border, border, cv::Mat(), cv::Point(-1, -1), 1);
        cv::Mat piece = result(box);
        piece.setTo(cv::Scalar(0), border);
    }
    return result;
}

std::vector<PieceContour> findPieceContours(const cv::Mat& mask, double minAreaFraction,
                                            double maxAreaFraction, int maxCount,
                                            int* discarded) {
    if (discarded != nullptr) {
        *discarded = 0;
    }
    std::vector<PieceContour> pieces;
    if (mask.empty() || mask.type() != CV_8UC1 || maxCount <= 0) {
        return pieces;
    }
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    const double imageArea = static_cast<double>(mask.total());
    std::vector<std::pair<double, const std::vector<cv::Point>*>> accepted;
    for (const auto& contour : contours) {
        const double area = cv::contourArea(contour);
        // El mismo filtro que ya se aplicaba al contorno mayor, ahora a cada
        // uno: por debajo es ruido, por encima es una segmentacion degenerada.
        if (area < minAreaFraction * imageArea || area > maxAreaFraction * imageArea) {
            continue;
        }
        accepted.emplace_back(area, &contour);
    }
    // Por area para QUEDARSE con las mayores, con desempate por posicion. El
    // desempate no es cosmetico: sin el, dos piezas del mismo tamaño decidian a
    // suertes cual sobrevivia al recorte de `maxCount`.
    std::sort(accepted.begin(), accepted.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) {
            return a.first > b.first;
        }
        const cv::Rect boxA = cv::boundingRect(*a.second);
        const cv::Rect boxB = cv::boundingRect(*b.second);
        if (boxA.y != boxB.y) {
            return boxA.y < boxB.y;
        }
        return boxA.x < boxB.x;
    });
    if (static_cast<int>(accepted.size()) > maxCount) {
        if (discarded != nullptr) {
            *discarded = static_cast<int>(accepted.size()) - maxCount;
        }
        accepted.resize(static_cast<std::size_t>(maxCount));
    }
    pieces.reserve(accepted.size());
    for (const auto& [area, points] : accepted) {
        PieceContour piece = describe(*points, area);
        if (piece.area > 0.0) {
            pieces.push_back(std::move(piece));
        }
    }
    // Y ya recortadas, en orden de lectura: es el orden con el que se van a
    // numerar y a enseñar.
    orderPiecesForReading(pieces);
    return pieces;
}

void orderPiecesForReading(std::vector<PieceContour>& pieces) {
    if (pieces.size() < 2) {
        return;
    }
    // La altura mediana de las piezas marca cuanto puede desviarse una pieza en
    // vertical y seguir siendo de la misma fila. Mediana y no media: una sola
    // pieza enorme o una mancha alargada arrastrarian la media y fundirian todas
    // las filas en una.
    std::vector<float> heights;
    heights.reserve(pieces.size());
    for (const auto& piece : pieces) {
        heights.push_back(cv::boundingRect(piece.points).height);
    }
    std::nth_element(heights.begin(), heights.begin() + heights.size() / 2, heights.end());
    const float median = heights[heights.size() / 2];
    // 0,6 de la altura mediana: dos piezas de la misma fila pueden estar
    // desalineadas medio cuerpo y seguir leyendose como una fila; mas de eso ya
    // es otra fila. Con piezas diminutas el suelo evita que el ruido del contorno
    // parta una fila en varias.
    const float rowTolerance = std::max(4.0F, median * 0.6F);

    std::sort(pieces.begin(), pieces.end(), [](const PieceContour& a, const PieceContour& b) {
        if (a.centroid.y != b.centroid.y) {
            return a.centroid.y < b.centroid.y;
        }
        return a.centroid.x < b.centroid.x;
    });

    // Bandas: se abre una fila con la primera pieza y se le van sumando las que
    // caen dentro de la tolerancia respecto al centro de la fila EN CURSO (no
    // respecto a la primera): asi una fila ligeramente inclinada no se parte a la
    // mitad, que es lo que pasa comparando siempre con el primer elemento.
    std::size_t rowStart = 0;
    double rowSum = pieces.front().centroid.y;
    for (std::size_t i = 1; i <= pieces.size(); ++i) {
        const bool sameRow =
            i < pieces.size() &&
            std::abs(pieces[i].centroid.y - rowSum / static_cast<double>(i - rowStart)) <=
                rowTolerance;
        if (sameRow) {
            rowSum += pieces[i].centroid.y;
            continue;
        }
        std::sort(pieces.begin() + static_cast<std::ptrdiff_t>(rowStart),
                  pieces.begin() + static_cast<std::ptrdiff_t>(i),
                  [](const PieceContour& a, const PieceContour& b) {
                      return a.centroid.x < b.centroid.x;
                  });
        if (i < pieces.size()) {
            rowStart = i;
            rowSum = pieces[i].centroid.y;
        }
    }
}

std::size_t largestPieceIndex(const std::vector<PieceAnalysis>& pieces) {
    std::size_t biggest = 0;
    for (std::size_t i = 1; i < pieces.size(); ++i) {
        if (pieces[i].contour.area > pieces[biggest].contour.area) {
            biggest = i;
        }
    }
    return biggest;
}

std::size_t measuredPieceIndex(const std::vector<PieceAnalysis>& pieces, int wanted) {
    if (pieces.empty()) {
        return 0;
    }
    if (wanted >= 1 && wanted <= static_cast<int>(pieces.size())) {
        return static_cast<std::size_t>(wanted - 1);
    }
    return largestPieceIndex(pieces);
}

const PieceContour* largestPiece(const std::vector<PieceContour>& pieces) {
    const PieceContour* best = nullptr;
    for (const auto& piece : pieces) {
        if (best == nullptr || piece.area > best->area) {
            best = &piece;
        }
    }
    return best;
}

core::Result<PieceContour> findLargestContour(const cv::Mat& mask, double minAreaFraction,
                                              double maxAreaFraction) {
    if (mask.empty() || mask.type() != CV_8UC1) {
        return core::Result<PieceContour>::err("Máscara inválida (se espera CV_8UC1)");
    }

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    double bestArea = 0.0;
    const std::vector<cv::Point>* best = nullptr;
    for (const auto& contour : contours) {
        const double area = cv::contourArea(contour);
        if (area > bestArea) {
            bestArea = area;
            best = &contour;
        }
    }

    const double imageArea = static_cast<double>(mask.total());
    if (best == nullptr || bestArea < minAreaFraction * imageArea) {
        return core::Result<PieceContour>::err("No se encontró ninguna pieza en la imagen");
    }
    if (bestArea > maxAreaFraction * imageArea) {
        return core::Result<PieceContour>::err(
            "La segmentación cubre casi toda la imagen (revisa fondo/iluminación)");
    }

    const cv::Moments m = cv::moments(*best);
    if (m.m00 <= 0.0) {
        return core::Result<PieceContour>::err("Contorno degenerado");
    }

    PieceContour result;
    result.points = *best;
    result.centroid = {static_cast<float>(m.m10 / m.m00), static_cast<float>(m.m01 / m.m00)};
    result.area = bestArea;
    result.perimeter = cv::arcLength(*best, true);
    result.rotatedRect = cv::minAreaRect(*best);
    return core::Result<PieceContour>::ok(std::move(result));
}

}  // namespace pci::vision
