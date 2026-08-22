#include "vision/detection_tuning.h"

#include <algorithm>
#include <vector>

#include <opencv2/imgproc.hpp>

namespace pci::vision {
namespace {

// Umbral por debajo del cual la mejora no compensa proponer nada. Es un juicio,
// no una medida: por debajo de un punto porcentual el operador no vería la
// diferencia en pantalla y sí vería una pregunta más.
constexpr double kMinimumGain = 0.01;

// Y un suelo de calidad: si ni el mejor ajuste se acerca a la corrección, el
// problema no es el umbral. Proponer un cambio ahí manda a alguien a perseguir
// una solución que no existe.
constexpr double kMinimumAgreement = 0.50;

constexpr int kCoarseStep = 8;
constexpr int kFineSpan = 8;

}  // namespace

bool SegmentationSuggestion::worthApplying() const {
    return found && agreementSuggested >= kMinimumAgreement &&
           agreementSuggested - agreementNow >= kMinimumGain;
}

double maskAgreement(const cv::Mat& a, const cv::Mat& b) {
    if (a.empty() || b.empty() || a.size() != b.size() || a.type() != CV_8UC1 ||
        b.type() != CV_8UC1) {
        return 0.0;
    }
    cv::Mat intersection;
    cv::Mat unionMask;
    cv::bitwise_and(a, b, intersection);
    cv::bitwise_or(a, b, unionMask);
    const double unionCount = cv::countNonZero(unionMask);
    if (unionCount <= 0.0) {
        // Las dos vacías es acuerdo perfecto y no una división por cero: las
        // dos dicen exactamente lo mismo, que no hay nada.
        return 1.0;
    }
    return cv::countNonZero(intersection) / unionCount;
}

SegmentationSuggestion suggestSegmentation(const cv::Mat& image, const cv::Mat& truthMask,
                                           const SegmentationOptions& current) {
    SegmentationSuggestion suggestion;
    suggestion.options = current;
    if (image.empty() || truthMask.empty() || truthMask.type() != CV_8UC1 ||
        truthMask.size() != image.size()) {
        return suggestion;
    }

    const auto agreementWith = [&](const SegmentationOptions& options) -> double {
        auto mask = segmentPiece(image, options);
        if (!mask.isOk()) {
            return -1.0;
        }
        return maskAgreement(mask.value(), truthMask);
    };

    const double now = agreementWith(current);
    if (now < 0.0) {
        return suggestion;  // ni con los ajustes de ahora se puede segmentar
    }
    suggestion.found = true;
    suggestion.agreementNow = now;
    suggestion.agreementSuggested = now;

    // Se prueban las dos polaridades explícitas además de la actual: «pieza
    // clara sobre fondo oscuro» y al revés es la equivocación que más lejos
    // deja el resultado, y la automática puede acertarla o no según la imagen.
    const std::vector<SegmentationPolarity> polarities = {
        current.polarity, SegmentationPolarity::LightPiece, SegmentationPolarity::DarkPiece};

    const auto consider = [&](int threshold, SegmentationPolarity polarity) {
        SegmentationOptions candidate = current;
        candidate.manualThreshold = threshold;
        candidate.polarity = polarity;
        const double score = agreementWith(candidate);
        if (score > suggestion.agreementSuggested) {
            suggestion.agreementSuggested = score;
            suggestion.options = candidate;
        }
    };

    for (const auto polarity : polarities) {
        for (int threshold = kCoarseStep; threshold < 256; threshold += kCoarseStep) {
            consider(threshold, polarity);
        }
    }

    // Afinado alrededor del mejor grueso. Sin esto la propuesta se quedaría
    // siempre en un múltiplo de ocho, que es visible en el borde.
    if (suggestion.options.manualThreshold >= 0) {
        const int centre = suggestion.options.manualThreshold;
        const auto polarity = suggestion.options.polarity;
        const int from = std::max(0, centre - kFineSpan);
        const int to = std::min(255, centre + kFineSpan);
        for (int threshold = from; threshold <= to; ++threshold) {
            consider(threshold, polarity);
        }
    }
    return suggestion;
}

}  // namespace pci::vision
