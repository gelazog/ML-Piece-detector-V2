#include "inspection_editor/execution/tool_executor.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

#include "inspection_editor/execution/edge_detection.h"
#include "vision/position_fixture.h"

namespace pci::inspection {

namespace {

constexpr double kPi = 3.14159265358979323846;

using vision::Fixture;
using vision::toImageCoords;

cv::Point2f toImg(const Fixture& f, const cv::Point2f& p) {
    return toImageCoords(f, p);
}

// Empaqueta la escala y la unidad elegida por el operador.
struct Fmt {
    double mmPerPixel = 0.0;
    LengthUnit unit = LengthUnit::Auto;
};

std::string fmt2(double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.2f", value);
    return buffer;
}

// Longitud con unidades. Sin escala o unidad Px: píxeles. Con escala: mm, cm
// o automático (cm a partir de 10 cm) según la elección del operador.
std::string fmtLen(double px, const Fmt& f) {
    char buffer[64];
    if (f.mmPerPixel <= 0.0 || f.unit == LengthUnit::Pixels) {
        std::snprintf(buffer, sizeof(buffer), "%.1fpx", px);
        return buffer;
    }
    const double mm = px * f.mmPerPixel;
    const bool useCm = f.unit == LengthUnit::Centimeters ||
                       (f.unit == LengthUnit::Auto && mm >= 100.0);
    if (useCm) {
        std::snprintf(buffer, sizeof(buffer), "%.2fcm (%.1fpx)", mm / 10.0, px);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.2fmm (%.1fpx)", mm, px);
    }
    return buffer;
}

std::string fmtArea(double px2, const Fmt& f) {
    char buffer[64];
    if (f.mmPerPixel <= 0.0 || f.unit == LengthUnit::Pixels) {
        std::snprintf(buffer, sizeof(buffer), "%.0fpx²", px2);
        return buffer;
    }
    const double mm2 = px2 * f.mmPerPixel * f.mmPerPixel;
    const bool useCm = f.unit == LengthUnit::Centimeters ||
                       (f.unit == LengthUnit::Auto && mm2 >= 10000.0);
    if (useCm) {
        std::snprintf(buffer, sizeof(buffer), "%.2fcm² (%.0fpx²)", mm2 / 100.0, px2);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.1fmm² (%.0fpx²)", mm2, px2);
    }
    return buffer;
}

bool withinTolerance(const ToolConfig& config, double value) {
    return value >= config.toleranceMin && value <= config.toleranceMax;
}

ToolRunResult baseResult(const ToolConfig& config) {
    ToolRunResult result;
    result.toolId = config.id;
    result.name = config.name;
    result.type = config.type;
    return result;
}

ToolRunResult runCaliper(const cv::Mat& gray, const Fixture& fixture,
                         const ToolConfig& config, const CaliperGeometry& g,
                         const Fmt& fmt) {
    ToolRunResult result = baseResult(config);
    const cv::Point2f p0 = toImg(fixture, g.p0);
    const cv::Point2f p1 = toImg(fixture, g.p1);
    result.overlaySegments.push_back({p0, p1});

    const auto edges = detectEdges(gray, p0, p1, g.bandWidth, 6);
    if (edges.size() < 2) {
        result.detail = "Se necesitan 2 bordes y se detectaron " +
                        std::to_string(edges.size());
        return result;
    }

    // Preferir un par de polaridad opuesta (subida + bajada): mide el ancho
    // real de la pieza en vez de dos bordes del mismo lado.
    std::size_t first = 0;
    std::size_t second = 1;
    double bestScore = -1.0;
    for (std::size_t i = 0; i < edges.size(); ++i) {
        for (std::size_t j = i + 1; j < edges.size(); ++j) {
            if (edges[i].strength * edges[j].strength >= 0.0) {
                continue;  // misma polaridad
            }
            const double score =
                std::min(std::abs(edges[i].strength), std::abs(edges[j].strength));
            if (score > bestScore) {
                bestScore = score;
                first = i;
                second = j;
            }
        }
    }
    // Sin par opuesto (p. ej. escalón simple): caer a los dos más fuertes.

    result.measured = std::abs(edges[first].position - edges[second].position);
    result.ok = withinTolerance(config, result.measured);
    result.detail = "d=" + fmtLen(result.measured, fmt);
    result.overlayPoints.push_back(edges[first].point);
    result.overlayPoints.push_back(edges[second].point);
    return result;
}

ToolRunResult runRuler(const Fixture& fixture, const ToolConfig& config,
                       const RulerGeometry& g, const Fmt& fmt) {
    ToolRunResult result = baseResult(config);
    const cv::Point2f p0 = toImg(fixture, g.p0);
    const cv::Point2f p1 = toImg(fixture, g.p1);
    result.overlaySegments.push_back({p0, p1});
    result.overlayPoints.push_back(p0);
    result.overlayPoints.push_back(p1);

    result.measured = cv::norm(p1 - p0);
    result.ok = withinTolerance(config, result.measured);
    result.detail = "L=" + fmtLen(result.measured, fmt);
    return result;
}

ToolRunResult runLineToLine(const Fixture& fixture, const ToolConfig& config,
                            const LineToLineGeometry& g, const Fmt& fmt) {
    ToolRunResult result = baseResult(config);
    result.measuredIsAngle = true;
    const cv::Point2f a0 = toImg(fixture, g.a0);
    const cv::Point2f a1 = toImg(fixture, g.a1);
    const cv::Point2f b0 = toImg(fixture, g.b0);
    const cv::Point2f b1 = toImg(fixture, g.b1);
    result.overlaySegments.push_back({a0, a1});
    result.overlaySegments.push_back({b0, b1});

    const cv::Point2f dirA = a1 - a0;
    const cv::Point2f dirB = b1 - b0;
    if (cv::norm(dirA) < 1.0 || cv::norm(dirB) < 1.0) {
        result.detail = "Líneas demasiado cortas";
        return result;
    }

    // Ángulo entre líneas (no dirigido): 0°..90°. atan2 del producto cruz y
    // el punto, tomando el valor absoluto y plegando a [0, 90].
    const double cross = static_cast<double>(dirA.x) * dirB.y -
                         static_cast<double>(dirA.y) * dirB.x;
    const double dot = static_cast<double>(dirA.x) * dirB.x +
                       static_cast<double>(dirA.y) * dirB.y;
    double angleDeg = std::abs(std::atan2(cross, dot) * 180.0 / kPi);
    if (angleDeg > 90.0) {
        angleDeg = 180.0 - angleDeg;
    }

    // Separación: distancia perpendicular del punto medio de B a la línea A.
    const cv::Point2f midB = (b0 + b1) * 0.5F;
    const double lenA = cv::norm(dirA);
    const double sep = std::abs(static_cast<double>(dirA.x) * (midB.y - a0.y) -
                                static_cast<double>(dirA.y) * (midB.x - a0.x)) /
                       lenA;

    result.measured = angleDeg;
    result.ok = withinTolerance(config, result.measured);
    result.detail = "ángulo=" + fmt2(angleDeg) + "°, separación=" + fmtLen(sep, fmt);
    return result;
}

ToolRunResult runCircle(const cv::Mat& gray, const Fixture& fixture,
                        const ToolConfig& config, const CircleGeometry& g,
                        const Fmt& fmt) {
    ToolRunResult result = baseResult(config);
    const cv::Point2f center = toImg(fixture, g.center);

    const int rays = std::clamp(g.rayCount, 8, 360);
    std::vector<cv::Point2f> points;
    for (int k = 0; k < rays; ++k) {
        const double theta = 2.0 * kPi * k / rays;
        const cv::Point2f dir(static_cast<float>(std::cos(theta)),
                              static_cast<float>(std::sin(theta)));
        const cv::Point2f from = center + dir * (g.radius - g.searchBand);
        const cv::Point2f to = center + dir * (g.radius + g.searchBand);
        const auto edges = detectEdges(gray, from, to, 3.0F, 1);
        if (!edges.empty()) {
            points.push_back(edges[0].point);
        }
    }

    if (static_cast<int>(points.size()) < rays * 6 / 10) {
        result.detail = "Borde circular insuficiente (" + std::to_string(points.size()) +
                        "/" + std::to_string(rays) + " rayos)";
        return result;
    }

    // Ajuste algebraico de círculo (Kasa): minimiza x^2+y^2 - 2ax - 2by - c.
    cv::Mat A(static_cast<int>(points.size()), 3, CV_64F);
    cv::Mat b(static_cast<int>(points.size()), 1, CV_64F);
    for (int i = 0; i < static_cast<int>(points.size()); ++i) {
        const auto& p = points[static_cast<std::size_t>(i)];
        A.at<double>(i, 0) = 2.0 * p.x;
        A.at<double>(i, 1) = 2.0 * p.y;
        A.at<double>(i, 2) = 1.0;
        b.at<double>(i, 0) = static_cast<double>(p.x) * p.x + static_cast<double>(p.y) * p.y;
    }
    cv::Mat solution;
    if (!cv::solve(A, b, solution, cv::DECOMP_SVD)) {
        result.detail = "No se pudo ajustar el círculo";
        return result;
    }
    const double cx = solution.at<double>(0);
    const double cy = solution.at<double>(1);
    const double r =
        std::sqrt(std::max(0.0, solution.at<double>(2) + cx * cx + cy * cy));

    double roundness = 0.0;
    for (const auto& p : points) {
        const double d = std::hypot(p.x - cx, p.y - cy);
        roundness = std::max(roundness, std::abs(d - r));
    }

    result.measured = 2.0 * r;
    result.ok = withinTolerance(config, result.measured);
    result.detail = "D=" + fmtLen(result.measured, fmt) +
                    ", R=" + fmtLen(r, fmt) +
                    ", redondez=" + fmtLen(roundness, fmt);
    result.overlayPoints = std::move(points);
    result.overlayPoints.push_back(
        {static_cast<float>(cx), static_cast<float>(cy)});
    return result;
}

ToolRunResult runPointToLine(const cv::Mat& gray, const Fixture& fixture,
                             const ToolConfig& config, const PointToLineGeometry& g,
                             const Fmt& fmt) {
    ToolRunResult result = baseResult(config);
    const cv::Point2f lineA = toImg(fixture, g.lineA);
    const cv::Point2f lineB = toImg(fixture, g.lineB);
    const cv::Point2f scanA = toImg(fixture, g.scanA);
    const cv::Point2f scanB = toImg(fixture, g.scanB);
    result.overlaySegments.push_back({lineA, lineB});
    result.overlaySegments.push_back({scanA, scanB});

    const cv::Point2f lineDelta = lineB - lineA;
    const double lineLength = cv::norm(lineDelta);
    if (lineLength < 1.0) {
        result.detail = "Línea de referencia degenerada";
        return result;
    }

    const auto edges = detectEdges(gray, scanA, scanB, 5.0F, 1);
    if (edges.empty()) {
        result.detail = "No se detectó ningún borde en el escaneo";
        return result;
    }

    const cv::Point2f p = edges[0].point;
    const double cross = static_cast<double>(lineDelta.x) * (p.y - lineA.y) -
                         static_cast<double>(lineDelta.y) * (p.x - lineA.x);
    result.measured = std::abs(cross) / lineLength;
    result.ok = withinTolerance(config, result.measured);
    result.detail = "d=" + fmtLen(result.measured, fmt);
    result.overlayPoints.push_back(p);
    return result;
}

ToolRunResult runEdgeFlaw(const cv::Mat& gray, const Fixture& fixture,
                          const ToolConfig& config, const EdgeFlawGeometry& g,
                          const Fmt& fmt) {
    ToolRunResult result = baseResult(config);
    const cv::Point2f p0 = toImg(fixture, g.p0);
    const cv::Point2f p1 = toImg(fixture, g.p1);
    result.overlaySegments.push_back({p0, p1});

    const cv::Point2f delta = p1 - p0;
    const float length = static_cast<float>(cv::norm(delta));
    const int scans = std::clamp(g.scanCount, 3, 200);
    if (length < static_cast<float>(scans)) {
        result.detail = "Tramo demasiado corto para " + std::to_string(scans) + " escaneos";
        return result;
    }
    const cv::Point2f u = delta / length;
    const cv::Point2f n(-u.y, u.x);

    // Un escaneo perpendicular por posición; el borde debería quedar a offset
    // constante. La desviación máxima respecto a la recta ajustada es el flaw.
    std::vector<double> ts;
    std::vector<double> offsets;
    for (int k = 0; k < scans; ++k) {
        const float t = length * static_cast<float>(k) / static_cast<float>(scans - 1);
        const cv::Point2f base = p0 + u * t;
        const cv::Point2f from = base - n * (g.scanLength / 2.0F);
        const cv::Point2f to = base + n * (g.scanLength / 2.0F);
        const auto edges = detectEdges(gray, from, to, 1.0F, 1);
        if (edges.empty()) {
            continue;
        }
        ts.push_back(static_cast<double>(t));
        offsets.push_back(edges[0].position - static_cast<double>(g.scanLength) / 2.0);
        result.overlayPoints.push_back(edges[0].point);
    }

    if (ts.size() < static_cast<std::size_t>(scans) * 6 / 10) {
        result.detail = "Borde no detectado en suficientes escaneos (" +
                        std::to_string(ts.size()) + "/" + std::to_string(scans) + ")";
        return result;
    }

    // Recta offset = a + b*t por mínimos cuadrados; residuos = irregularidad.
    const double count = static_cast<double>(ts.size());
    double sumT = 0.0;
    double sumO = 0.0;
    double sumTT = 0.0;
    double sumTO = 0.0;
    for (std::size_t i = 0; i < ts.size(); ++i) {
        sumT += ts[i];
        sumO += offsets[i];
        sumTT += ts[i] * ts[i];
        sumTO += ts[i] * offsets[i];
    }
    const double denom = count * sumTT - sumT * sumT;
    const double slope = std::abs(denom) > 1e-9 ? (count * sumTO - sumT * sumO) / denom : 0.0;
    const double intercept = (sumO - slope * sumT) / count;

    double maxDeviation = 0.0;
    for (std::size_t i = 0; i < ts.size(); ++i) {
        maxDeviation = std::max(maxDeviation,
                                std::abs(offsets[i] - (intercept + slope * ts[i])));
    }

    result.measured = maxDeviation;
    result.ok = withinTolerance(config, result.measured);
    result.detail = "desv. máx=" + fmtLen(result.measured, fmt) + " (" +
                    std::to_string(ts.size()) + " escaneos)";
    return result;
}

ToolRunResult runBlob(const cv::Mat& gray, const Fixture& fixture, const ToolConfig& config,
                      const BlobGeometry& g, const Fmt& fmt) {
    ToolRunResult result = baseResult(config);

    // Rectángulo alineado a los ejes de la pieza -> cuadrilátero en imagen.
    const float hw = g.width / 2.0F;
    const float hh = g.height / 2.0F;
    const std::array<cv::Point2f, 4> quad = {
        toImg(fixture, g.center + cv::Point2f(-hw, -hh)),
        toImg(fixture, g.center + cv::Point2f(hw, -hh)),
        toImg(fixture, g.center + cv::Point2f(hw, hh)),
        toImg(fixture, g.center + cv::Point2f(-hw, hh)),
    };
    for (int i = 0; i < 4; ++i) {
        result.overlaySegments.push_back(
            {quad[static_cast<std::size_t>(i)], quad[static_cast<std::size_t>((i + 1) % 4)]});
    }

    std::vector<cv::Point> quadInt;
    for (const auto& p : quad) {
        quadInt.emplace_back(cvRound(p.x), cvRound(p.y));
    }
    const cv::Rect bounds = cv::boundingRect(quadInt) & cv::Rect(0, 0, gray.cols, gray.rows);
    if (bounds.area() < 9) {
        result.detail = "La región cae fuera de la imagen";
        return result;
    }

    cv::Mat regionMask = cv::Mat::zeros(bounds.size(), CV_8UC1);
    std::vector<cv::Point> quadLocal;
    for (const auto& p : quadInt) {
        quadLocal.emplace_back(p.x - bounds.x, p.y - bounds.y);
    }
    cv::fillConvexPoly(regionMask, quadLocal, cv::Scalar(255));

    const cv::Mat roi = gray(bounds);
    cv::Mat binary;
    cv::threshold(roi, binary, 0.0, 255.0,
                  (g.darkBlobs ? cv::THRESH_BINARY_INV : cv::THRESH_BINARY) |
                      cv::THRESH_OTSU);
    cv::bitwise_and(binary, regionMask, binary);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    int count = 0;
    double totalArea = 0.0;
    for (const auto& contour : contours) {
        const double area = cv::contourArea(contour);
        if (area < static_cast<double>(g.minArea)) {
            continue;
        }
        ++count;
        totalArea += area;
        const cv::Moments m = cv::moments(contour);
        if (m.m00 > 0.0) {
            result.overlayPoints.emplace_back(
                static_cast<float>(m.m10 / m.m00 + bounds.x),
                static_cast<float>(m.m01 / m.m00 + bounds.y));
        }
    }

    result.measured = count;
    result.ok = withinTolerance(config, result.measured);
    result.detail =
        std::to_string(count) + " blob(s), área=" + fmtArea(totalArea, fmt);
    return result;
}

}  // namespace

core::Result<ToolRunResult> runTool(const cv::Mat& image, const vision::Fixture& fixture,
                                    const ToolConfig& config, double mmPerPixel,
                                    LengthUnit unit) {
    using ResultT = core::Result<ToolRunResult>;
    const Fmt fmt{mmPerPixel, unit};

    if (image.empty()) {
        return ResultT::err("Imagen vacía");
    }
    cv::Mat gray;
    if (image.channels() == 3) {
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    } else if (image.channels() == 1) {
        gray = image;
    } else {
        return ResultT::err("Formato de imagen no soportado");
    }

    auto geometry = geometryFromJson(config.type, config.geometryJson);
    if (!geometry.isOk()) {
        return ResultT::err(geometry.error().message);
    }

    try {
        switch (config.type) {
            case ToolType::Caliper:
                return ResultT::ok(runCaliper(gray, fixture, config,
                                              std::get<CaliperGeometry>(geometry.value()),
                                              fmt));
            case ToolType::Circle:
                return ResultT::ok(runCircle(gray, fixture, config,
                                             std::get<CircleGeometry>(geometry.value()),
                                             fmt));
            case ToolType::PointToLine:
                return ResultT::ok(
                    runPointToLine(gray, fixture, config,
                                   std::get<PointToLineGeometry>(geometry.value()),
                                   fmt));
            case ToolType::EdgeFlaw:
                return ResultT::ok(runEdgeFlaw(gray, fixture, config,
                                               std::get<EdgeFlawGeometry>(geometry.value()),
                                               fmt));
            case ToolType::Blob:
                return ResultT::ok(runBlob(gray, fixture, config,
                                           std::get<BlobGeometry>(geometry.value()),
                                           fmt));
            case ToolType::Ruler:
                return ResultT::ok(runRuler(fixture, config,
                                            std::get<RulerGeometry>(geometry.value()),
                                            fmt));
            case ToolType::LineToLine:
                return ResultT::ok(runLineToLine(
                    fixture, config, std::get<LineToLineGeometry>(geometry.value()), fmt));
        }
        return ResultT::err("Tipo de herramienta no soportado");
    } catch (const cv::Exception& e) {
        return ResultT::err(std::string("Excepción de OpenCV ejecutando '") + config.name +
                            "': " + e.what());
    }
}

std::vector<ToolRunResult> runTools(const cv::Mat& image, const vision::Fixture& fixture,
                                    const std::vector<ToolConfig>& tools, double mmPerPixel,
                                    LengthUnit unit) {
    std::vector<ToolRunResult> results;
    for (const auto& config : tools) {
        if (!config.enabled) {
            continue;
        }
        auto result = runTool(image, fixture, config, mmPerPixel, unit);
        if (result.isOk()) {
            results.push_back(std::move(result.value()));
        } else {
            ToolRunResult failed;
            failed.toolId = config.id;
            failed.name = config.name;
            failed.type = config.type;
            failed.ok = false;
            failed.detail = result.error().message;
            results.push_back(std::move(failed));
        }
    }
    return results;
}

std::string formatLength(double px, double mmPerPixel, LengthUnit unit) {
    return fmtLen(px, Fmt{mmPerPixel, unit});
}

}  // namespace pci::inspection
