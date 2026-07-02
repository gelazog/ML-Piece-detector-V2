#pragma once

#include <QImage>

namespace cv {
class Mat;
}

namespace pci::camera {

// Copia profunda: la QImage devuelta es dueña de su buffer y puede cruzar
// hilos con seguridad aunque el cv::Mat de origen se reutilice.
QImage matToQImage(const cv::Mat& mat);

// Copia profunda inversa: BGR (CV_8UC3) o gris (CV_8UC1); otros formatos de
// QImage se convierten primero. QImage nula -> Mat vacío.
cv::Mat qImageToMat(const QImage& image);

}  // namespace pci::camera
