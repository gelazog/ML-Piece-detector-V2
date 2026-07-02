#include "ui/video_widget.h"

#include <QPainter>
#include <QPaintEvent>

namespace pci::ui {

VideoWidget::VideoWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(320, 240);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

QSize VideoWidget::sizeHint() const {
    return {640, 480};
}

void VideoWidget::setFrame(const QImage& frame) {
    frame_ = frame;
    update();
}

void VideoWidget::clear() {
    frame_ = QImage();
    update();
}

void VideoWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);

    if (frame_.isNull()) {
        painter.setPen(Qt::gray);
        painter.drawText(rect(), Qt::AlignCenter, tr("Sin señal"));
        return;
    }

    // Escalado con aspecto preservado, centrado sobre fondo negro.
    QSize target = frame_.size();
    target.scale(size(), Qt::KeepAspectRatio);
    QRect targetRect(QPoint(0, 0), target);
    targetRect.moveCenter(rect().center());

    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(targetRect, frame_);
}

}  // namespace pci::ui
