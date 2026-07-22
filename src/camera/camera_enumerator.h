#pragma once

#include <vector>

#include "camera/camera_info.h"

namespace pci::camera {

// Lista las cámaras que el SO reporta, con su nombre amigable real, SIN abrir
// ninguna (ver native_cameras.h). Es rápida y segura, pero conviene llamarla
// desde un hilo de trabajo por si el subsistema del SO tarda. La resolución no
// se conoce hasta conectar (width/height quedan en 0), porque leerla exigiría
// abrir el dispositivo.
class CameraEnumerator {
public:
    static std::vector<CameraInfo> enumerate(int maxIndex = 8);
};

}  // namespace pci::camera
