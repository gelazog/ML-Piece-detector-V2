#include "vision/fixture_stabilizer.h"

#include <opencv2/core.hpp>

#include <cmath>

namespace pci::vision {

namespace {

double wrapDeg(double angle) {
    while (angle >= 180.0) {
        angle -= 360.0;
    }
    while (angle < -180.0) {
        angle += 360.0;
    }
    return angle;
}

}  // namespace

Fixture stabilizeFixture(const Fixture& previous, const Fixture& measured,
                         const StabilizerOptions& options, bool& flipped180) {
    flipped180 = false;

    Fixture candidate = measured;
    double angleDiff = wrapDeg(candidate.angleDeg - previous.angleDeg);
    if (options.resolveFlips && std::abs(angleDiff) > 90.0) {
        candidate.angleDeg = wrapDeg(candidate.angleDeg + 180.0);
        flipped180 = true;
        angleDiff = wrapDeg(candidate.angleDeg - previous.angleDeg);
    }

    const double positionDelta = cv::norm(candidate.origin - previous.origin);

    // Quieto: el ruido de medición no mueve nada.
    if (positionDelta < options.positionDeadbandPx &&
        std::abs(angleDiff) < options.angleDeadbandDeg) {
        return previous;
    }

    // Movimiento grande: seguir al instante para no arrastrarse detrás.
    if (positionDelta > options.positionSnapPx ||
        std::abs(angleDiff) > options.angleSnapDeg) {
        return candidate;
    }

    // Movimiento suave: acercamiento exponencial, sin vibración.
    Fixture blended;
    const float alpha = static_cast<float>(options.smoothing);
    blended.origin = previous.origin + (candidate.origin - previous.origin) * alpha;
    blended.angleDeg = wrapDeg(previous.angleDeg + options.smoothing * angleDiff);
    return blended;
}

}  // namespace pci::vision
