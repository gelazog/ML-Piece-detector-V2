#include "camera/camera_enumerator.h"

#include <opencv2/videoio.hpp>

#include "core/logging.h"

namespace pci::camera {

namespace {

constexpr int kBackend =
#ifdef _WIN32
    cv::CAP_MSMF;
#else
    cv::CAP_V4L2;
#endif

// Los índices de cámara pueden tener huecos (p. ej. una cámara virtual
// desinstalada); se tolera un hueco antes de dar por terminado el sondeo.
constexpr int kMaxConsecutiveMisses = 2;

}  // namespace

std::vector<CameraInfo> CameraEnumerator::enumerate(int maxIndex) {
    std::vector<CameraInfo> cameras;
    int misses = 0;

    for (int i = 0; i < maxIndex && misses < kMaxConsecutiveMisses; ++i) {
        cv::VideoCapture capture;
        try {
            capture.open(i, kBackend);
        } catch (const cv::Exception& e) {
            core::logWarning("Excepción de OpenCV sondeando cámara " + std::to_string(i) +
                             ": " + e.what());
            ++misses;
            continue;
        }

        if (capture.isOpened()) {
            CameraInfo info;
            info.index = i;
            info.name = "Cámara " + std::to_string(i);
            info.width = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_WIDTH));
            info.height = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_HEIGHT));
            cameras.push_back(info);
            misses = 0;
            core::logInfo("Detectada " + info.name + " (" + std::to_string(info.width) + "x" +
                          std::to_string(info.height) + ")");
        } else {
            ++misses;
        }
        capture.release();
    }

    core::logInfo("Enumeración de cámaras terminada: " + std::to_string(cameras.size()) +
                  " encontradas");
    return cameras;
}

}  // namespace pci::camera
