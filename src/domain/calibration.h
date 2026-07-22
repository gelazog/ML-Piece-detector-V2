#pragma once

#include <string>

namespace pci::domain {

// Calibración de escala para un plano de trabajo a distancia fija de la
// cámara (modelo pinhole): mmPerPixel convierte medidas en píxeles a
// milímetros reales. Válida mientras no cambie la altura de la cámara.
struct ScaleCalibration {
    double mmPerPixel = 0.0;       // 0 = sin calibrar (todo queda en px)
    double cameraDistanceMm = 0.0; // distancia cámara -> plano (estimada o medida)
    double horizontalFovDeg = 60.0;
    // Resolución del frame en la que se calibró: si la cámara o su resolución
    // cambian, la escala en px deja de ser válida. 0 = desconocida (calibración
    // heredada sin este dato -> no se avisa para no dar falsas alarmas).
    int calibratedWidth = 0;
    int calibratedHeight = 0;

    [[nodiscard]] bool valid() const { return mmPerPixel > 0.0; }
    [[nodiscard]] double toMm(double px) const { return px * mmPerPixel; }

    [[nodiscard]] bool resolutionKnown() const {
        return calibratedWidth > 0 && calibratedHeight > 0;
    }
    // True si la calibración sirve para un frame de w x h: cuando la resolución
    // calibrada se conoce, debe coincidir; si no se conoce, no se cuestiona.
    [[nodiscard]] bool matchesResolution(int w, int h) const {
        return !resolutionKnown() || (w == calibratedWidth && h == calibratedHeight);
    }

    // "10.58 mm (41.8 px)" si está calibrada; "41.8 px" si no.
    [[nodiscard]] std::string formatLength(double px) const;
};

// Método A: objeto de referencia — una distancia medida en px cuya longitud
// real se conoce. measuredPx <= 0 o knownMm <= 0 -> inválida.
ScaleCalibration calibrationFromKnownLength(double measuredPx, double knownMm,
                                            int imageWidthPx, double horizontalFovDeg);

// Método B: distancia de cámara + FOV horizontal -> ancho visible del plano
// -> mm por píxel. Parámetros no positivos -> inválida.
ScaleCalibration calibrationFromCameraDistance(double cameraDistanceMm,
                                               double horizontalFovDeg, int imageWidthPx);

// Distancia estimada cámara -> plano a partir de la escala y el FOV asumido.
double estimateCameraDistanceMm(double mmPerPixel, double horizontalFovDeg,
                                int imageWidthPx);

}  // namespace pci::domain
