#pragma once

#include <string>

namespace pci::camera {

struct CameraInfo {
    int index = -1;
    std::string name;
    int width = 0;
    int height = 0;
    // Backend de OpenCV con el que este índice abrió de verdad (cv::CAP_*).
    // En Windows algunos drivers solo funcionan con DirectShow y no con MSMF.
    int backend = 0;  // 0 = cv::CAP_ANY
};

}  // namespace pci::camera
