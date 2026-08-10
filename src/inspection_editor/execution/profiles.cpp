#include "inspection_editor/execution/profiles.h"

#include <algorithm>
#include <cmath>

#include "inspection_editor/execution/edge_detection.h"

namespace pci::inspection {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kRadToDeg = 180.0 / kPi;

bool usableImage(const cv::Mat& gray) {
    return !gray.empty() && gray.type() == CV_8UC1;
}

}  // namespace

std::vector<RadialSample> radialProfile(const cv::Mat& gray, cv::Point2f center, double rMin,
                                        double rMax, int rayCount, float thickness) {
    std::vector<RadialSample> profile;
    if (!usableImage(gray) || rayCount < 2 || !(rMax > rMin) || rMin < 0.0) {
        return profile;
    }

    profile.reserve(static_cast<std::size_t>(rayCount));
    for (int k = 0; k < rayCount; ++k) {
        const double theta = 2.0 * kPi * k / rayCount;
        const cv::Point2f dir(static_cast<float>(std::cos(theta)),
                              static_cast<float>(std::sin(theta)));
        RadialSample sample;
        sample.angleDeg = theta * kRadToDeg;

        const cv::Point2f from = center + dir * static_cast<float>(rMin);
        const cv::Point2f to = center + dir * static_cast<float>(rMax);
        const auto edges = detectEdges(gray, from, to, thickness, 1);
        if (!edges.empty()) {
            sample.found = true;
            // `position` es la distancia recorrida desde el inicio del segmento,
            // que empieza en rMin y no en el centro.
            sample.radius = rMin + edges.front().position;
            sample.strength = std::abs(edges.front().strength);
            sample.point = edges.front().point;
        }
        profile.push_back(sample);
    }
    return profile;
}

cv::Point2f profileNormal(cv::Point2f from, cv::Point2f to) {
    const cv::Point2f axis = to - from;
    const double length = std::hypot(static_cast<double>(axis.x), static_cast<double>(axis.y));
    if (length < 1e-6) {
        return {0.0F, 0.0F};
    }
    return {static_cast<float>(-axis.y / length), static_cast<float>(axis.x / length)};
}

std::vector<AxialSample> axialProfile(const cv::Mat& gray, cv::Point2f from, cv::Point2f to,
                                      ProfileSide side, int stations, double reach,
                                      float thickness) {
    std::vector<AxialSample> profile;
    const cv::Point2f normal = profileNormal(from, to);
    if (!usableImage(gray) || stations < 2 || !(reach > 0.0) ||
        (normal.x == 0.0F && normal.y == 0.0F)) {
        return profile;
    }

    const cv::Point2f axis = to - from;
    const double length = std::hypot(static_cast<double>(axis.x), static_cast<double>(axis.y));
    const cv::Point2f step = axis / static_cast<float>(stations - 1);
    const cv::Point2f outward =
        side == ProfileSide::Positive ? normal : cv::Point2f(-normal.x, -normal.y);

    profile.reserve(static_cast<std::size_t>(stations));
    for (int i = 0; i < stations; ++i) {
        const cv::Point2f base = from + step * static_cast<float>(i);
        AxialSample sample;
        sample.t = length * i / (stations - 1);

        // Se explora del eje hacia fuera, así que la posición devuelta por
        // detectEdges ya es la distancia perpendicular buscada.
        const auto edges =
            detectEdges(gray, base, base + outward * static_cast<float>(reach), thickness, 1);
        if (!edges.empty()) {
            sample.found = true;
            sample.offset = edges.front().position;
            sample.strength = std::abs(edges.front().strength);
            sample.point = edges.front().point;
        }
        profile.push_back(sample);
    }
    return profile;
}

int foundCount(const std::vector<RadialSample>& profile) {
    return static_cast<int>(
        std::count_if(profile.begin(), profile.end(), [](const RadialSample& s) {
            return s.found;
        }));
}

int foundCount(const std::vector<AxialSample>& profile) {
    return static_cast<int>(
        std::count_if(profile.begin(), profile.end(), [](const AxialSample& s) {
            return s.found;
        }));
}

}  // namespace pci::inspection
