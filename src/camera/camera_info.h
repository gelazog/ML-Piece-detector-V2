#pragma once

#include <string>

namespace pci::camera {

struct CameraInfo {
    int index = -1;
    std::string name;
    int width = 0;
    int height = 0;
};

}  // namespace pci::camera
