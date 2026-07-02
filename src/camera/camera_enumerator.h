#pragma once

#include <vector>

#include "camera/camera_info.h"

namespace pci::camera {

// Sondea índices de captura de forma síncrona; cada sondeo puede tardar cientos
// de ms, así que debe llamarse desde un hilo de trabajo, nunca desde la UI.
// Limitación conocida: OpenCV no expone el nombre real del dispositivo, por lo
// que los nombres son genéricos ("Cámara 0", "Cámara 1", ...).
class CameraEnumerator {
public:
    static std::vector<CameraInfo> enumerate(int maxIndex = 8);
};

}  // namespace pci::camera
