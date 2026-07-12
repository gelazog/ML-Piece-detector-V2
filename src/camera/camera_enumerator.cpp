#include "camera/camera_enumerator.h"

#include <opencv2/videoio.hpp>

#include "core/logging.h"

namespace pci::camera {

namespace {

struct BackendOption {
    int id;
    const char* name;
};

// En Windows se prueba MSMF primero (nativo, menor latencia) y DirectShow
// como respaldo: hay drivers —como cámaras integradas comunes— que MSMF no
// abre pero DirectShow sí.
constexpr BackendOption kBackends[] = {
#ifdef _WIN32
    {cv::CAP_MSMF, "MSMF"},
    {cv::CAP_DSHOW, "DirectShow"},
#else
    {cv::CAP_V4L2, "V4L2"},
#endif
};

// Los índices de cámara pueden tener huecos (p. ej. una cámara virtual
// desinstalada); se tolera un hueco antes de dar por terminado el sondeo.
constexpr int kMaxConsecutiveMisses = 2;

}  // namespace

std::vector<CameraInfo> CameraEnumerator::enumerate(int maxIndex) {
    std::vector<CameraInfo> cameras;
    int misses = 0;

    for (int i = 0; i < maxIndex && misses < kMaxConsecutiveMisses; ++i) {
        bool found = false;
        for (const auto& backend : kBackends) {
            // Blindaje total por backend: hay drivers que lanzan cualquier
            // cosa (no solo cv::Exception) al sondearlos; un driver roto no
            // debe tumbar la aplicación al arrancar.
            try {
                cv::VideoCapture capture;
                capture.open(i, backend.id);
                if (!capture.isOpened()) {
                    continue;
                }

                CameraInfo info;
                info.index = i;
                info.backend = backend.id;
                info.name = "Cámara " + std::to_string(i) + " (" + backend.name + ")";
                info.width = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_WIDTH));
                info.height = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_HEIGHT));
                capture.release();
                cameras.push_back(info);
                found = true;
                core::logInfo("Detectada " + info.name + " (" + std::to_string(info.width) +
                              "x" + std::to_string(info.height) + ")");
                break;
            } catch (const cv::Exception& e) {
                core::logWarning("Excepción de OpenCV sondeando cámara " +
                                 std::to_string(i) + " (" + backend.name + "): " + e.what());
            } catch (const std::exception& e) {
                core::logWarning(std::string("Excepción sondeando cámara: ") + e.what());
            } catch (...) {
                core::logWarning("Excepción desconocida sondeando cámara " +
                                 std::to_string(i));
            }
        }
        misses = found ? 0 : misses + 1;
    }

    core::logInfo("Enumeración de cámaras terminada: " + std::to_string(cameras.size()) +
                  " encontradas");
    return cameras;
}

}  // namespace pci::camera
