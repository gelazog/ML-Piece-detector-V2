#include "vision/brush_snap.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace pci::vision {

double seedIntensity(const cv::Mat& gray, const cv::Point& centre, int radius) {
    if (gray.empty() || gray.type() != CV_8UC1) {
        return 0.0;
    }
    const int reach = std::max(1, radius);
    cv::Rect around(centre.x - reach, centre.y - reach, 2 * reach + 1, 2 * reach + 1);
    around &= cv::Rect(0, 0, gray.cols, gray.rows);
    if (around.empty()) {
        return 0.0;
    }
    // Un disco y no el cuadrado: en una esquina del trazo, las esquinas del
    // cuadro caen del otro lado del borde y contaminan la semilla justo cuando
    // más falta hace acertar.
    cv::Mat disc = cv::Mat::zeros(around.size(), CV_8UC1);
    cv::circle(disc, cv::Point(centre.x - around.x, centre.y - around.y), reach,
               cv::Scalar(255), cv::FILLED);
    return cv::mean(gray(around), disc)[0];
}

BrushSnapResult snapBrushBand(const cv::Mat& gray, const cv::Mat& band, const cv::Rect& area,
                              double seedGray, double minContrast) {
    BrushSnapResult result;
    if (gray.empty() || band.empty() || gray.type() != CV_8UC1 || band.type() != CV_8UC1 ||
        band.size() != gray.size()) {
        return result;  // `kept` vacío: el llamador deja la banda como estaba
    }
    const cv::Rect safe = area & cv::Rect(0, 0, gray.cols, gray.rows);
    if (safe.empty()) {
        return result;
    }

    const cv::Mat bandRoi = band(safe);
    const cv::Mat grayRoi = gray(safe);
    // Desde aquí, cualquier salida devuelve la banda ENTERA: si no se puede
    // ceñir, la corrección tiene que hacer lo de toda la vida.
    result.kept = bandRoi.clone();
    result.bandPixels = cv::countNonZero(bandRoi);
    result.keptPixels = result.bandPixels;
    if (result.bandPixels < kMinBandPixels) {
        return result;
    }

    // El umbral se calcula SOLO con los píxeles de la banda, no con el
    // rectángulo que la contiene. La diferencia no es cosmética: la envolvente
    // de un trazo diagonal es casi toda imagen que el operador no ha tocado, y
    // un Otsu sobre ella parte por donde manda esa mayoría ajena en vez de por
    // donde está el borde que se quiere seguir.
    std::vector<unsigned char> values;
    values.reserve(static_cast<std::size_t>(result.bandPixels));
    for (int y = 0; y < bandRoi.rows; ++y) {
        const auto* bandRow = bandRoi.ptr<unsigned char>(y);
        const auto* grayRow = grayRoi.ptr<unsigned char>(y);
        for (int x = 0; x < bandRoi.cols; ++x) {
            if (bandRow[x] != 0) {
                values.push_back(grayRow[x]);
            }
        }
    }
    if (values.empty()) {
        return result;
    }

    const cv::Mat column(static_cast<int>(values.size()), 1, CV_8UC1, values.data());
    cv::Mat ignored;
    const double threshold =
        cv::threshold(column, ignored, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);

    double sumLow = 0.0;
    double sumHigh = 0.0;
    int countLow = 0;
    int countHigh = 0;
    for (const unsigned char value : values) {
        if (static_cast<double>(value) > threshold) {
            sumHigh += value;
            ++countHigh;
        } else {
            sumLow += value;
            ++countLow;
        }
    }
    if (countLow == 0 || countHigh == 0) {
        return result;
    }
    const double meanLow = sumLow / countLow;
    const double meanHigh = sumHigh / countHigh;
    result.contrast = meanHigh - meanLow;
    if (result.contrast < minContrast) {
        // Una sola población con ruido. Partirla inventaría un borde donde no lo
        // hay, y un borde inventado se mide igual de bien que uno real.
        return result;
    }

    const bool keepHigh =
        std::abs(seedGray - meanHigh) < std::abs(seedGray - meanLow);
    cv::Mat side;
    cv::threshold(grayRoi, side, threshold, 255,
                  keepHigh ? cv::THRESH_BINARY : cv::THRESH_BINARY_INV);
    cv::Mat kept;
    cv::bitwise_and(bandRoi, side, kept);
    const int keptPixels = cv::countNonZero(kept);

    // Si ceñir se lo lleva casi todo, la semilla estaba en el lado equivocado o
    // el trazo cayó entero en una sola población. Devolver una pincelada que no
    // marca nada haría creer al operador que el pincel dejó de funcionar, así
    // que en ese caso se pinta la banda como siempre.
    if (keptPixels < kMinBandPixels) {
        return result;
    }

    result.kept = kept;
    result.keptPixels = keptPixels;
    result.snapped = true;
    return result;
}

}  // namespace pci::vision
