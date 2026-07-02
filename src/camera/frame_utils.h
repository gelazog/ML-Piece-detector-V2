#pragma once

#include <QImage>

namespace cv {
class Mat;
}

namespace pci::camera {

// Copia profunda: la QImage devuelta es dueña de su buffer y puede cruzar
// hilos con seguridad aunque el cv::Mat de origen se reutilice.
QImage matToQImage(const cv::Mat& mat);

}  // namespace pci::camera
