#include "vision/subpixel_edge.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <opencv2/imgproc.hpp>

namespace pci::vision {
namespace {

// Intensidad en un punto no entero, interpolando entre los cuatro vecinos.
//
// Sin esto, recorrer la normal a pasos de medio píxel devolvería el mismo valor
// dos veces y el afinado no podría dar nada mejor que un píxel entero, que es
// justo lo que se está intentando superar.
double sampleAt(const cv::Mat& gray, double x, double y) {
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    if (x0 < 0 || y0 < 0 || x0 + 1 >= gray.cols || y0 + 1 >= gray.rows) {
        return -1.0;  // fuera de la imagen: el que llama lo trata como sin dato
    }
    const double fx = x - x0;
    const double fy = y - y0;
    const double v00 = gray.at<unsigned char>(y0, x0);
    const double v01 = gray.at<unsigned char>(y0, x0 + 1);
    const double v10 = gray.at<unsigned char>(y0 + 1, x0);
    const double v11 = gray.at<unsigned char>(y0 + 1, x0 + 1);
    return (v00 * (1.0 - fx) + v01 * fx) * (1.0 - fy) +
           (v10 * (1.0 - fx) + v11 * fx) * fy;
}

// La normal al contorno en un punto, sacada de sus vecinos.
//
// Se usan vecinos SEPARADOS y no los inmediatos: en un contorno de máscara, dos
// puntos consecutivos difieren en un píxel y su dirección solo puede ser una de
// ocho. Con esa resolución la normal apunta en diagonal donde debería apuntar
// casi recto, y el perfil se muestrea torcido.
cv::Point2f normalAt(const std::vector<cv::Point>& contour, std::size_t index, int span) {
    const std::size_t n = contour.size();
    const cv::Point& before = contour[(index + n - static_cast<std::size_t>(span)) % n];
    const cv::Point& after = contour[(index + static_cast<std::size_t>(span)) % n];
    const double tx = static_cast<double>(after.x - before.x);
    const double ty = static_cast<double>(after.y - before.y);
    const double length = std::hypot(tx, ty);
    if (length < 1e-9) {
        return {0.0F, 0.0F};
    }
    // Normal = tangente girada 90°.
    return {static_cast<float>(-ty / length), static_cast<float>(tx / length)};
}

}  // namespace

SubpixelContour refineContourSubpixel(const cv::Mat& gray,
                                      const std::vector<cv::Point>& contour,
                                      const SubpixelOptions& options) {
    SubpixelContour result;
    result.points.reserve(contour.size());
    for (const auto& point : contour) {
        result.points.emplace_back(static_cast<float>(point.x), static_cast<float>(point.y));
    }
    if (contour.size() < 3 || gray.empty() || gray.type() != CV_8UC1 || options.reach < 2) {
        result.kept = static_cast<int>(contour.size());
        return result;
    }

    const int span = std::max(2, static_cast<int>(contour.size() / 64));
    const double step = 1.0 / std::max(1, options.samplesPerPixel);
    const int steps = static_cast<int>(options.reach / step);
    double totalShift = 0.0;

    for (std::size_t i = 0; i < contour.size(); ++i) {
        const cv::Point2f normal = normalAt(contour, i, span);
        if (normal.x == 0.0F && normal.y == 0.0F) {
            ++result.kept;
            continue;
        }
        const double cx = contour[i].x;
        const double cy = contour[i].y;

        // Perfil a lo largo de la normal, de dentro hacia fuera.
        std::vector<double> profile;
        profile.reserve(static_cast<std::size_t>(2 * steps + 1));
        bool complete = true;
        for (int s = -steps; s <= steps; ++s) {
            const double t = s * step;
            const double value = sampleAt(gray, cx + normal.x * t, cy + normal.y * t);
            if (value < 0.0) {
                complete = false;
                break;
            }
            profile.push_back(value);
        }
        if (!complete || profile.size() < 5) {
            ++result.kept;
            continue;
        }

        // Niveles LOCALES: la media del cuarto más interior y la del más
        // exterior. Se usa un cuarto y no un solo píxel porque un píxel de ruido
        // en el extremo desplazaría el punto medio y con él todo el borde.
        const std::size_t quarter = std::max<std::size_t>(2, profile.size() / 4);
        double innerSum = 0.0;
        double outerSum = 0.0;
        for (std::size_t k = 0; k < quarter; ++k) {
            innerSum += profile[k];
            outerSum += profile[profile.size() - 1 - k];
        }
        const double inner = innerSum / static_cast<double>(quarter);
        const double outer = outerSum / static_cast<double>(quarter);
        if (std::abs(outer - inner) < options.minContrast) {
            // Perfil plano: aquí no hay borde que afinar. Se deja el punto donde
            // estaba en vez de moverlo a un sitio inventado.
            ++result.kept;
            continue;
        }

        // El borde está donde el perfil cruza la MITAD entre los dos niveles.
        // Se busca el cruce más cercano al punto de partida, no el primero: un
        // reflejo puede hacer que el perfil cruce el nivel medio dos veces, y el
        // bueno es el que está donde la máscara ya dijo que había borde.
        const double half = 0.5 * (inner + outer);
        const bool rising = outer > inner;
        double bestT = 0.0;
        double bestDistance = std::numeric_limits<double>::infinity();
        bool found = false;
        for (std::size_t k = 0; k + 1 < profile.size(); ++k) {
            const double a = profile[k];
            const double b = profile[k + 1];
            const bool crosses = rising ? (a < half && b >= half) : (a > half && b <= half);
            if (!crosses) {
                continue;
            }
            const double fraction = (std::abs(b - a) < 1e-9) ? 0.0 : (half - a) / (b - a);
            const double t = (static_cast<double>(k) + fraction) * step -
                             static_cast<double>(steps) * step;
            if (std::abs(t) < bestDistance) {
                bestDistance = std::abs(t);
                bestT = t;
                found = true;
            }
        }
        if (!found || std::abs(bestT) > options.maxShift) {
            ++result.kept;
            continue;
        }

        result.points[i] = cv::Point2f(static_cast<float>(cx + normal.x * bestT),
                                       static_cast<float>(cy + normal.y * bestT));
        totalShift += std::abs(bestT);
        ++result.refined;
    }

    result.meanShift =
        result.refined > 0 ? totalShift / static_cast<double>(result.refined) : 0.0;

    // Suavizado a lo largo del contorno, con media móvil circular.
    //
    // Se hace DESPUÉS de afinar y no antes: suavizar primero movería los puntos
    // de partida y el perfil se muestrearía en el sitio equivocado. Y se hace
    // sobre una copia, porque una media móvil que lee lo que ella misma acaba
    // de escribir arrastra el contorno en la dirección del recorrido.
    //
    // Y SOLO si el afinado encontró bordes de verdad. El suavizado existe para
    // limpiar el ruido que introducen el afinado y la rejilla de píxeles; si no
    // se afinó nada es que no había borde visible, y entonces mover el contorno
    // sería modificar datos sin ninguna prueba a favor. La garantía de que en el
    // peor caso esto NO HACE NADA vale más que un perímetro algo mejor.
    const bool refinedEnough =
        result.refined > 0 &&
        result.refined * 2 > static_cast<int>(result.points.size());
    if (refinedEnough && options.smoothSpan > 0 &&
        result.points.size() > static_cast<std::size_t>(2 * options.smoothSpan + 1)) {
        const std::vector<cv::Point2f> source = result.points;
        const std::size_t n = source.size();
        const int span = options.smoothSpan;
        for (std::size_t i = 0; i < n; ++i) {
            cv::Point2f sum(0.0F, 0.0F);
            for (int k = -span; k <= span; ++k) {
                const std::size_t j = (i + n + static_cast<std::size_t>(
                                              (k % static_cast<int>(n)) + static_cast<int>(n))) % n;
                sum += source[j];
            }
            result.points[i] = sum / static_cast<float>(2 * span + 1);
        }
    }
    return result;
}

double subpixelArea(const std::vector<cv::Point2f>& points) {
    if (points.size() < 3) {
        return 0.0;
    }
    // Fórmula del cordón de zapato, en coma flotante de principio a fin.
    double sum = 0.0;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const cv::Point2f& a = points[i];
        const cv::Point2f& b = points[(i + 1) % points.size()];
        sum += static_cast<double>(a.x) * b.y - static_cast<double>(b.x) * a.y;
    }
    return std::abs(sum) / 2.0;
}

double subpixelPerimeter(const std::vector<cv::Point2f>& points) {
    if (points.size() < 2) {
        return 0.0;
    }
    double total = 0.0;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const cv::Point2f& a = points[i];
        const cv::Point2f& b = points[(i + 1) % points.size()];
        total += std::hypot(static_cast<double>(b.x) - a.x, static_cast<double>(b.y) - a.y);
    }
    return total;
}

}  // namespace pci::vision
