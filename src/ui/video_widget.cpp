#include "ui/video_widget.h"

#include "ui/theme.h"

#include <QPainter>
#include <QPaintEvent>

#include <cmath>

namespace pci::ui {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

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

void VideoWidget::setOverlay(const AnalysisOverlay& overlay) {
    overlay_ = overlay;
    update();
}

void VideoWidget::clearOverlay() {
    overlay_ = AnalysisOverlay{};
    update();
}

void VideoWidget::clear() {
    frame_ = QImage();
    overlay_ = AnalysisOverlay{};
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

    // El overlay solo es válido para frames de la misma resolución.
    if (overlay_.valid && overlay_.frameSize == frame_.size()) {
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing);
        painter.translate(targetRect.topLeft());
        painter.scale(static_cast<double>(target.width()) / frame_.width(),
                      static_cast<double>(target.height()) / frame_.height());

        // CON HALO. El color solo no basta sobre una foto: medido, el contorno
        // contra los píxeles por los que pasa da 1,2-2,6 de contraste, y con el
        // borde oscuro debajo pasa a 5-15. Lo hacía ya el lienzo del editor y
        // no lo hacía el vídeo en vivo, que es donde el operador mira todo el
        // día.
        theme::withHalo(painter, theme::drawColor(theme::kDrawFound),
                        [&] { painter.drawPolygon(overlay_.contour); });

        const double rad = overlay_.angleDeg * kPi / 180.0;
        const double len = frame_.width() * 0.12;
        const QPointF axisEnd =
            overlay_.centroid + QPointF(std::cos(rad) * len, std::sin(rad) * len);
        theme::withHalo(painter, theme::drawColor(theme::kDrawAxis),
                        [&] { painter.drawLine(overlay_.centroid, axisEnd); });

        painter.setPen(Qt::NoPen);
        painter.setBrush(theme::drawColor(theme::kDrawOrigin));
        painter.drawEllipse(overlay_.centroid, 4.0, 4.0);
        painter.restore();
    }

    // Texto de estado del análisis arriba a la izquierda.
    if (overlay_.valid || !overlay_.error.isEmpty()) {
        const QString text = overlay_.valid
                                 ? tr("Pieza: %1°").arg(overlay_.angleDeg, 0, 'f', 1)
                                 : overlay_.error;
        painter.setPen(Qt::NoPen);
        painter.setBrush(theme::veil());
        const QRect textRect(targetRect.left() + 8, targetRect.top() + 8, 260, 24);
        painter.drawRect(textRect);
        painter.setPen(overlay_.valid ? theme::drawColor(theme::kDrawFound)
                              : theme::drawColor(theme::kDrawMissing));
        painter.drawText(textRect.adjusted(6, 0, 0, 0), Qt::AlignVCenter, text);
    }
}

}  // namespace pci::ui
