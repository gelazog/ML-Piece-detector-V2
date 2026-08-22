#include "vision/quality_metrics.h"

#include <opencv2/imgproc.hpp>

namespace pci::vision {

double sharpnessOf(const cv::Mat& image, const cv::Rect& roi) {
    if (image.empty()) {
        return 0.0;
    }
    // El recorte se acota contra la imagen: un ROI que se sale (la pieza pegada
    // al borde) recortaría fuera de memoria.
    const cv::Rect full(0, 0, image.cols, image.rows);
    const cv::Rect box = roi.area() > 0 ? (roi & full) : full;
    if (box.width < 8 || box.height < 8) {
        return 0.0;  // demasiado pequeño para que la varianza signifique nada
    }

    cv::Mat gray;
    if (image.channels() == 3) {
        cv::cvtColor(image(box), gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = image(box);
    }
    cv::Mat laplacian;
    cv::Laplacian(gray, laplacian, CV_64F);
    cv::Scalar mean;
    cv::Scalar stddev;
    cv::meanStdDev(laplacian, mean, stddev);
    return stddev[0] * stddev[0];
}

domain::QualityMetrics computeQualityMetrics(const cv::Mat& image,
                                             const PieceAnalysis* analysis) {
    domain::QualityMetrics metrics;
    if (image.empty()) {
        return metrics;
    }

    cv::Mat gray;
    if (image.channels() == 3) {
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = image;
    }

    // Nitidez: varianza del Laplaciano (bajo = borroso).
    cv::Mat laplacian;
    cv::Laplacian(gray, laplacian, CV_64F);
    cv::Scalar mean;
    cv::Scalar stddev;
    cv::meanStdDev(laplacian, mean, stddev);
    metrics.sharpness = stddev[0] * stddev[0];

    metrics.meanBrightness = cv::mean(gray)[0];

    const int nearBlack = cv::countNonZero(gray < 5);
    const int nearWhite = cv::countNonZero(gray > 250);
    metrics.clippedFraction =
        static_cast<double>(nearBlack + nearWhite) / static_cast<double>(gray.total());

    if (analysis != nullptr && !analysis->contour.points.empty()) {
        metrics.pieceFound = true;
        const cv::Rect box = cv::boundingRect(analysis->contour.points);
        constexpr int kMargin = 2;
        metrics.pieceTouchesBorder = box.x <= kMargin || box.y <= kMargin ||
                                     box.x + box.width >= gray.cols - kMargin ||
                                     box.y + box.height >= gray.rows - kMargin;

        // Cuánto se separa la pieza de su fondo. Es lo único que dice de verdad
        // si la imagen sirve para medir, y por eso se calcula aquí aunque el
        // brillo medio siga estando: un contraluz y una pieza oscura sobre mesa
        // blanca son montajes opuestos, los dos legítimos, y ningún nivel medio
        // los aprueba a los dos.
        cv::Mat pieceMask = cv::Mat::zeros(gray.size(), CV_8UC1);
        cv::fillPoly(pieceMask, std::vector<std::vector<cv::Point>>{analysis->contour.points},
                     cv::Scalar(255));
        cv::Mat background;
        cv::bitwise_not(pieceMask, background);
        if (cv::countNonZero(pieceMask) > 0 && cv::countNonZero(background) > 0) {
            metrics.pieceContrast =
                std::abs(cv::mean(gray, pieceMask)[0] - cv::mean(gray, background)[0]);
        }
    }
    return metrics;
}

double contourRaggedness(double areaPx, double perimeterPx) {
    if (areaPx <= 0.0 || perimeterPx <= 0.0) {
        return 0.0;  // sin pieza no hay nada que juzgar
    }
    // Perímetro del círculo que tendría esa misma área. Es el mínimo posible:
    // de todas las figuras con un área dada, la circunferencia es la de menor
    // perímetro (desigualdad isoperimétrica), así que la razón nunca baja de 1.
    const double minimumPerimeter = 2.0 * std::sqrt(CV_PI * areaPx);
    if (minimumPerimeter <= 0.0) {
        return 0.0;
    }
    return perimeterPx / minimumPerimeter;
}

bool contourLooksRagged(double areaPx, double perimeterPx) {
    const double ratio = contourRaggedness(areaPx, perimeterPx);
    return ratio > kRaggedContourWarning;
}

FrameContact pieceTouchesFrame(const std::vector<cv::Point>& contour,
                               const cv::Size& frame, int margin) {
    FrameContact contact;
    if (contour.empty() || frame.width <= 0 || frame.height <= 0) {
        return contact;
    }
    const cv::Rect box = cv::boundingRect(contour);
    contact.left = box.x <= margin;
    contact.top = box.y <= margin;
    contact.right = box.x + box.width >= frame.width - margin;
    contact.bottom = box.y + box.height >= frame.height - margin;
    return contact;
}

std::string frameContactWarning(const FrameContact& contact) {
    if (!contact.any()) {
        return {};
    }
    std::string sides;
    const auto add = [&sides](const char* name) {
        if (!sides.empty()) {
            sides += ", ";
        }
        sides += name;
    };
    if (contact.left) { add("izquierdo"); }
    if (contact.top) { add("superior"); }
    if (contact.right) { add("derecho"); }
    if (contact.bottom) { add("inferior"); }

    // El aviso dice QUE PASA CON LAS MEDIDAS, no solo que toca el borde. Saber
    // que toca el borde no le sirve de nada a quien esta leyendo un ancho: lo
    // que necesita saber es que ese ancho se queda corto.
    return "La pieza toca el borde " + sides +
           " del encuadre: esta cortada, asi que sus medidas (ancho, alto, area, "
           "perimetro) son limites inferiores y no medidas. Aleja la camara o mueve "
           "la pieza hasta que entre entera.";
}

}  // namespace pci::vision
