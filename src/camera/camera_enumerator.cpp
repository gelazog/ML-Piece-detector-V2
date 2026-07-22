#include "camera/camera_enumerator.h"

#include <opencv2/videoio.hpp>

#include <utility>

#include "camera/native_cameras.h"
#include "core/logging.h"

namespace pci::camera {

std::vector<CameraInfo> CameraEnumerator::enumerate(int maxIndex) {
    std::vector<CameraInfo> cameras;

    // Enumeración SIN abrir dispositivos. Antes se hacía capture.open() sobre
    // cada índice solo para leer nombre y resolución, pero abrir una cámara
    // virtual no lista (p. ej. AndroidCam antes de conectar el celular) hace que
    // su driver negocie formato, divida por cero y tumbe TODO el proceso con una
    // excepción estructurada que ningún try/catch de C++ atrapa. Ahora pedimos
    // la lista al SO por su API nativa (DirectShow / V4L2), que solo lee
    // metadatos y nunca abre el pin de captura.
    const std::vector<NativeCamera> natives = enumerateNativeCameras();

    for (const auto& native : natives) {
        if (native.index < 0 || native.index >= maxIndex) {
            continue;
        }
        CameraInfo info;
        info.index = native.index;
#ifdef _WIN32
        // El índice nativo viene del orden de DirectShow, que es el mismo que
        // usa el backend CAP_DSHOW de OpenCV. Abrimos con DSHOW por coherencia
        // (además hay cámaras integradas que MSMF no abre pero DirectShow sí).
        info.backend = cv::CAP_DSHOW;
#elif defined(__linux__)
        info.backend = cv::CAP_V4L2;
#else
        info.backend = 0;  // cv::CAP_ANY
#endif
        info.name = native.friendlyName;
        // Resolución desconocida hasta conectar: no abrimos el dispositivo aquí.
        info.width = 0;
        info.height = 0;
        core::logInfo("Detectada cámara: " + info.name + " (índice " +
                      std::to_string(info.index) + ")");
        cameras.push_back(std::move(info));
    }

#if !defined(_WIN32) && !defined(__linux__)
    // En plataformas sin enumeración nativa ofrecemos el índice 0 como respaldo
    // para no dejar al usuario sin opciones; en Windows/Linux la lista del SO es
    // autoritativa y vacía significa, honestamente, "no hay cámaras".
    if (cameras.empty()) {
        CameraInfo fallback;
        fallback.index = 0;
        fallback.backend = 0;  // cv::CAP_ANY
        fallback.name = "Cámara 0";
        cameras.push_back(fallback);
        core::logWarning("Sin enumeración nativa; se ofrece la cámara 0 como respaldo");
    }
#endif

    core::logInfo("Enumeración de cámaras terminada: " + std::to_string(cameras.size()) +
                  " encontradas");
    return cameras;
}

}  // namespace pci::camera
