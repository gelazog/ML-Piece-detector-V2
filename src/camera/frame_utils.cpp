#include "camera/frame_utils.h"

#include <opencv2/core.hpp>

namespace pci::camera {

QImage matToQImage(const cv::Mat& mat) {
    if (mat.empty()) {
        return {};
    }

    switch (mat.type()) {
        case CV_8UC3:
            // Format_BGR888 evita un cvtColor por frame: Qt interpreta BGR directo.
            return QImage(mat.data, mat.cols, mat.rows, static_cast<qsizetype>(mat.step),
                          QImage::Format_BGR888)
                .copy();
        case CV_8UC1:
            return QImage(mat.data, mat.cols, mat.rows, static_cast<qsizetype>(mat.step),
                          QImage::Format_Grayscale8)
                .copy();
        default:
            return {};
    }
}

}  // namespace pci::camera
