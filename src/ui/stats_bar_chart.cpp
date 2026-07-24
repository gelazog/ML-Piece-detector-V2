#include "ui/stats_bar_chart.h"

#include <QPainter>

#include <algorithm>
#include <utility>

namespace pci::ui {

StatsBarChart::StatsBarChart(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(120);
}

QSize StatsBarChart::sizeHint() const {
    return {480, 140};
}

void StatsBarChart::setData(std::vector<repositories::InspectionRepository::DailyStat> data) {
    data_ = std::move(data);
    update();
}

void StatsBarChart::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QColor axis(120, 120, 120);
    const QColor okColor(0, 170, 0);
    const QColor ngColor(210, 60, 60);
    const QColor text = palette().color(QPalette::WindowText);

    if (data_.empty()) {
        painter.setPen(text);
        painter.drawText(rect(), Qt::AlignCenter, tr("Sin inspecciones registradas."));
        return;
    }

    constexpr int kLabelH = 16;   // franja inferior para la fecha
    constexpr int kMargin = 6;
    const int chartTop = kMargin;
    const int chartBottom = height() - kLabelH;
    const int chartHeight = std::max(1, chartBottom - chartTop);
    const int count = static_cast<int>(data_.size());
    const double slot = static_cast<double>(width() - 2 * kMargin) / count;
    const double barWidth = std::min(slot * 0.7, 46.0);

    int maxTotal = 1;
    for (const auto& d : data_) {
        maxTotal = std::max(maxTotal, d.total);
    }

    // Eje base.
    painter.setPen(axis);
    painter.drawLine(kMargin, chartBottom, width() - kMargin, chartBottom);

    QFont labelFont = painter.font();
    labelFont.setPointSizeF(std::max(6.0, labelFont.pointSizeF() - 1.0));
    painter.setFont(labelFont);

    for (int i = 0; i < count; ++i) {
        const auto& d = data_[static_cast<std::size_t>(i)];
        const double cx = kMargin + slot * (i + 0.5);
        const double left = cx - barWidth / 2.0;
        const double barH = chartHeight * (static_cast<double>(d.total) / maxTotal);
        const double okH = d.total > 0 ? barH * (static_cast<double>(d.okCount) / d.total) : 0.0;
        const double ngH = barH - okH;

        // NG arriba (rojo), OK abajo (verde): pila que suma el total del día.
        painter.fillRect(QRectF(left, chartBottom - barH, barWidth, ngH), ngColor);
        painter.fillRect(QRectF(left, chartBottom - okH, barWidth, okH), okColor);

        // Total encima de la barra.
        painter.setPen(text);
        painter.drawText(QRectF(left - 4, chartBottom - barH - 15, barWidth + 8, 14),
                         Qt::AlignCenter, QString::number(d.total));

        // Fecha (MM-DD) debajo, si la barra da espacio.
        if (slot >= 26.0) {
            const QString date = QString::fromStdString(d.date).right(5);  // MM-DD
            painter.drawText(QRectF(cx - slot / 2.0, chartBottom + 1, slot, kLabelH - 1),
                             Qt::AlignCenter, date);
        }
    }
}

}  // namespace pci::ui
