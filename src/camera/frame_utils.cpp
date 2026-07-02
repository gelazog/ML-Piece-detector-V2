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

cv::Mat qImageToMat(const QImage& image) {
    if (image.isNull()) {
        return {};
    }

    switch (image.format()) {
        case QImage::Format_BGR888:
            return cv::Mat(image.height(), image.width(), CV_8UC3,
                           const_cast<uchar*>(image.constBits()),
                           static_cast<std::size_t>(image.bytesPerLine()))
                .clone();
        case QImage::Format_Grayscale8:
            return cv::Mat(image.height(), image.width(), CV_8UC1,
                           const_cast<uchar*>(image.constBits()),
                           static_cast<std::size_t>(image.bytesPerLine()))
                .clone();
        default:
            return qImageToMat(image.convertToFormat(QImage::Format_BGR888));
    }
}

}  // namespace pci::camera
