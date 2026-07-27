#include "vision/board_frame.h"

#include <algorithm>
#include <cmath>

namespace pci::vision {
namespace {

constexpr double kPi = 3.14159265358979323846;

double toRadians(double degrees) {
    return degrees * kPi / 180.0;
}

double toDegrees(double radians) {
    return radians * 180.0 / kPi;
}

}  // namespace

double normalizeAngle(double degrees) {
    double angle = std::fmod(degrees, 360.0);
    if (angle <= -180.0) {
        angle += 360.0;
    } else if (angle > 180.0) {
        angle -= 360.0;
    }
    return angle;
}

BoardFrame resolveBoardFrame(const BoardConfig& config, const Fixture& fixture, bool pieceFound,
                             const cv::Size& imageSize) {
    const cv::Point2f imageCenter{static_cast<float>(imageSize.width) / 2.0F,
                                  static_cast<float>(imageSize.height) / 2.0F};
    BoardFrame frame;
    switch (config.origin) {
        case BoardOrigin::PieceCenter:
            // Sin pieza no hay centro de pieza: se cae al centro de la imagen
            // para que el tablero siga siendo utilizable.
            frame.origin = pieceFound ? fixture.origin : imageCenter;
            break;
        case BoardOrigin::ImageCenter:
            frame.origin = imageCenter;
            break;
        case BoardOrigin::FixedPoint:
            frame.origin = config.fixedPoint;
            break;
    }
    // El giro solo puede seguir a la pieza si hay pieza detectada.
    frame.angleDeg =
        (config.followPieceAngle && pieceFound) ? normalizeAngle(fixture.angleDeg) : 0.0;
    return frame;
}

BoardReading readPoint(const BoardFrame& frame, const cv::Point2f& imagePoint) {
    const double vx = static_cast<double>(imagePoint.x) - static_cast<double>(frame.origin.x);
    const double vy = static_cast<double>(imagePoint.y) - static_cast<double>(frame.origin.y);
    const double angle = toRadians(frame.angleDeg);
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    // Giro inverso al de los ejes (seguimos en coords con y hacia abajo).
    const double localX = vx * c + vy * s;
    const double localYDown = -vx * s + vy * c;

    BoardReading reading;
    reading.dx = localX;
    reading.dy = -localYDown;  // +Y del tablero apunta hacia arriba
    reading.radius = std::hypot(reading.dx, reading.dy);
    reading.angleDeg = (reading.radius > 0.0) ? toDegrees(std::atan2(reading.dy, reading.dx)) : 0.0;
    return reading;
}

cv::Point2f toImagePoint(const BoardFrame& frame, double dx, double dy) {
    const double angle = toRadians(frame.angleDeg);
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    const double localYDown = -dy;
    const double vx = dx * c - localYDown * s;
    const double vy = dx * s + localYDown * c;
    return {static_cast<float>(static_cast<double>(frame.origin.x) + vx),
            static_cast<float>(static_cast<double>(frame.origin.y) + vy)};
}

BoardReading readPiece(const BoardFrame& frame, const Fixture& fixture) {
    return readPoint(frame, fixture.origin);
}

double pieceAngleOffset(const BoardFrame& frame, const Fixture& fixture) {
    // Los dos ángulos están en el convenio de la imagen (y hacia abajo), así que
    // la diferencia es directa; se normaliza para no dar saltos de 360°.
    return normalizeAngle(fixture.angleDeg - frame.angleDeg);
}

BoardReading toMillimeters(const BoardReading& reading, double mmPerPixel) {
    if (!(mmPerPixel > 0.0)) {
        return reading;  // sin calibración: los valores siguen siendo píxeles
    }
    BoardReading scaled = reading;
    scaled.dx *= mmPerPixel;
    scaled.dy *= mmPerPixel;
    scaled.radius *= mmPerPixel;
    return scaled;  // el ángulo no cambia con la escala
}

double niceGridStep(double span, int targetDivisions) {
    const double usableSpan = std::abs(span);
    const int divisions = std::max(1, targetDivisions);
    if (!(usableSpan > 0.0) || !std::isfinite(usableSpan)) {
        return 1.0;
    }
    const double raw = usableSpan / divisions;
    const double magnitude = std::pow(10.0, std::floor(std::log10(raw)));
    const double normalized = raw / magnitude;  // [1, 10)
    // Escalones 1-2-5: los que un operador lee de un vistazo.
    double step = 10.0;
    if (normalized <= 1.0) {
        step = 1.0;
    } else if (normalized <= 2.0) {
        step = 2.0;
    } else if (normalized <= 5.0) {
        step = 5.0;
    }
    return step * magnitude;
}

std::string_view originKey(BoardOrigin origin) {
    switch (origin) {
        case BoardOrigin::PieceCenter:
            return "piece";
        case BoardOrigin::ImageCenter:
            return "image";
        case BoardOrigin::FixedPoint:
            return "fixed";
    }
    return "piece";
}

BoardOrigin originFromKey(std::string_view key, BoardOrigin fallback) {
    if (key == originKey(BoardOrigin::PieceCenter)) {
        return BoardOrigin::PieceCenter;
    }
    if (key == originKey(BoardOrigin::ImageCenter)) {
        return BoardOrigin::ImageCenter;
    }
    if (key == originKey(BoardOrigin::FixedPoint)) {
        return BoardOrigin::FixedPoint;
    }
    return fallback;
}

}  // namespace pci::vision
