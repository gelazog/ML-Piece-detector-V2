#include "vision/position_fixture.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>

#include "vision/orientation.h"

namespace pci::vision {

core::Result<Fixture> computeFixture(const cv::Mat& mask, bool autoOrient) {
    if (mask.empty()) {
        return core::Result<Fixture>::err("Máscara vacía: no hay pieza");
    }
    // Los momentos se calculan UNA vez y se comparten. Antes había dos pasadas
    // sobre la máscara (tres con autoOrient), porque el centroide, la
    // anisotropía y el ángulo se pedían por separado y cada uno llamaba a
    // `cv::moments`. Sobre 2560×1440 eso eran 14 ms, el 40 % del análisis.
    //
    // Y se calculan sobre la ENVOLVENTE de la pieza, no sobre la máscara
    // entera: es exactamente equivalente. Los momentos centrales (mu*) son
    // invariantes a la traslación, así que la anisotropía y el ángulo no
    // cambian; y al centroide solo hay que sumarle la esquina del recorte.
    const cv::Rect box = cv::boundingRect(mask);
    if (box.empty()) {
        return core::Result<Fixture>::err("Máscara vacía: no hay pieza");
    }
    const cv::Moments m = cv::moments(mask(box), true);
    if (m.m00 <= 0.0) {
        return core::Result<Fixture>::err("Máscara vacía: no hay pieza");
    }

    Fixture fixture;
    fixture.origin = {static_cast<float>(m.m10 / m.m00 + box.x),
                      static_cast<float>(m.m01 / m.m00 + box.y)};
    fixture.anisotropy = principalAnisotropy(m);

    if (autoOrient) {
        const auto angle = principalAngleDeg(m);
        if (!angle.isOk()) {
            return core::Result<Fixture>::err(angle.error().message);
        }
        fixture.angleDeg = angle.value();
    } else {
        fixture.angleDeg = 0.0;  // pieza vertical
    }
    return core::Result<Fixture>::ok(fixture);
}

cv::Point2f toPieceCoords(const Fixture& fixture, const cv::Point2f& imagePoint) {
    const double rad = fixture.angleDeg * CV_PI / 180.0;
    const float c = static_cast<float>(std::cos(rad));
    const float s = static_cast<float>(std::sin(rad));
    const cv::Point2f d = imagePoint - fixture.origin;
    // Rotación inversa: proyección sobre el eje principal y su normal.
    return {d.x * c + d.y * s, -d.x * s + d.y * c};
}

cv::Point2f toImageCoords(const Fixture& fixture, const cv::Point2f& piecePoint) {
    const double rad = fixture.angleDeg * CV_PI / 180.0;
    const float c = static_cast<float>(std::cos(rad));
    const float s = static_cast<float>(std::sin(rad));
    return {fixture.origin.x + piecePoint.x * c - piecePoint.y * s,
            fixture.origin.y + piecePoint.x * s + piecePoint.y * c};
}

core::Result<cv::Mat> normalizePiece(const cv::Mat& image, const cv::Mat& mask,
                                     const Fixture& fixture, int canonicalSize) {
    if (image.empty() || mask.empty() || image.size() != mask.size()) {
        return core::Result<cv::Mat>::err("Imagen y máscara vacías o de tamaños distintos");
    }
    if (canonicalSize <= 0) {
        return core::Result<cv::Mat>::err("Tamaño canónico inválido");
    }

    // Sin giro no hay nada que deformar, y ese es el caso POR DEFECTO
    // (`autoOrient` es false: la pieza se deja vertical). Aun así se hacían dos
    // `warpAffine` de imagen completa con una rotación identidad: 12 ms
    // tirados, el 34 % del análisis. Con el atajo se recorta y punto.
    const bool upright = std::abs(fixture.angleDeg) < 1e-9;

    cv::Mat rotated;
    cv::Rect box;
    if (upright) {
        box = cv::boundingRect(mask);
        if (box.empty()) {
            return core::Result<cv::Mat>::err("La pieza quedó fuera del encuadre al rotar");
        }
        // El fondo se borra solo dentro del recorte, no en la imagen entera.
        rotated = cv::Mat::zeros(box.size(), image.type());
        image(box).copyTo(rotated, mask(box));
        box = cv::Rect(0, 0, box.width, box.height);
    } else {
        // Fondo eliminado antes de rotar: solo la pieza sobrevive.
        cv::Mat clean = cv::Mat::zeros(image.size(), image.type());
        image.copyTo(clean, mask);

        const cv::Mat rotation =
            cv::getRotationMatrix2D(fixture.origin, fixture.angleDeg, 1.0);
        cv::Mat rotatedMask;
        cv::warpAffine(clean, rotated, rotation, clean.size(), cv::INTER_LINEAR,
                       cv::BORDER_CONSTANT, cv::Scalar());
        cv::warpAffine(mask, rotatedMask, rotation, mask.size(), cv::INTER_NEAREST,
                       cv::BORDER_CONSTANT, cv::Scalar());

        box = cv::boundingRect(rotatedMask);
        if (box.empty()) {
            return core::Result<cv::Mat>::err("La pieza quedó fuera del encuadre al rotar");
        }
    }

    const cv::Mat cropped = rotated(box);
    const double scale = static_cast<double>(canonicalSize) /
                         static_cast<double>(std::max(box.width, box.height));
    cv::Mat resized;
    cv::resize(cropped, resized, cv::Size(), scale, scale,
               scale < 1.0 ? cv::INTER_AREA : cv::INTER_LINEAR);

    cv::Mat canvas = cv::Mat::zeros(canonicalSize, canonicalSize, image.type());
    const int x = (canonicalSize - resized.cols) / 2;
    const int y = (canonicalSize - resized.rows) / 2;
    resized.copyTo(canvas(cv::Rect(x, y, resized.cols, resized.rows)));

    return core::Result<cv::Mat>::ok(std::move(canvas));
}

}  // namespace pci::vision
