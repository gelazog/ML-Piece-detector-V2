#include "inspection_editor/canvas/tool_icons.h"

#include <QApplication>
#include <QPainter>
#include <QPalette>
#include <QPixmap>

#include <functional>

namespace pci::inspection {

namespace {

constexpr int kSize = 28;

QIcon makeIcon(const std::function<void(QPainter&, const QColor&)>& draw) {
    QPixmap pixmap(kSize, kSize);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor color = QApplication::palette().color(QPalette::ButtonText);
    QPen pen(color);
    pen.setWidthF(2.0);
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);
    draw(painter, color);
    return QIcon(pixmap);
}

}  // namespace

QIcon moveModeIcon() {
    return makeIcon([](QPainter& p, const QColor&) {
        // Cruz de mover con puntas de flecha.
        p.drawLine(14, 5, 14, 23);
        p.drawLine(5, 14, 23, 14);
        p.drawLine(14, 5, 11, 8);
        p.drawLine(14, 5, 17, 8);
        p.drawLine(14, 23, 11, 20);
        p.drawLine(14, 23, 17, 20);
        p.drawLine(5, 14, 8, 11);
        p.drawLine(5, 14, 8, 17);
        p.drawLine(23, 14, 20, 11);
        p.drawLine(23, 14, 20, 17);
    });
}

QIcon toolIcon(ToolType type) {
    switch (type) {
        case ToolType::Caliper:
            return makeIcon([](QPainter& p, const QColor&) {
                // Dos mordazas y flecha de distancia entre ellas.
                p.drawLine(6, 5, 6, 23);
                p.drawLine(22, 5, 22, 23);
                p.drawLine(8, 14, 20, 14);
                p.drawLine(8, 14, 11, 11);
                p.drawLine(8, 14, 11, 17);
                p.drawLine(20, 14, 17, 11);
                p.drawLine(20, 14, 17, 17);
            });
        case ToolType::Circle:
            return makeIcon([](QPainter& p, const QColor& c) {
                p.drawEllipse(QPointF(14, 14), 9.0, 9.0);
                p.setBrush(c);
                p.drawEllipse(QPointF(14, 14), 1.6, 1.6);
            });
        case ToolType::PointToLine:
            return makeIcon([](QPainter& p, const QColor& c) {
                p.drawLine(5, 21, 23, 21);
                p.setBrush(c);
                p.drawEllipse(QPointF(14, 8), 2.5, 2.5);
                QPen dashed = p.pen();
                dashed.setStyle(Qt::DashLine);
                dashed.setWidthF(1.4);
                p.setPen(dashed);
                p.drawLine(14, 11, 14, 19);
            });
        case ToolType::EdgeFlaw:
            return makeIcon([](QPainter& p, const QColor&) {
                // Borde recto con una muesca al medio.
                p.drawLine(4, 16, 11, 16);
                p.drawLine(11, 16, 14, 21);
                p.drawLine(14, 21, 17, 16);
                p.drawLine(17, 16, 24, 16);
            });
        case ToolType::Ruler:
            return makeIcon([](QPainter& p, const QColor&) {
                // Regla diagonal con marcas.
                p.drawLine(6, 22, 22, 6);
                p.drawLine(4, 18, 10, 24);   // tope A
                p.drawLine(18, 2, 24, 8);    // tope B
                p.drawLine(11, 15, 14, 18);  // marcas intermedias
                p.drawLine(15, 11, 18, 14);
            });
        case ToolType::Blob:
            return makeIcon([](QPainter& p, const QColor& c) {
                QPen thin = p.pen();
                thin.setWidthF(1.6);
                p.setPen(thin);
                p.drawRect(5, 5, 18, 18);
                p.setBrush(c);
                p.setPen(Qt::NoPen);
                p.drawEllipse(QPointF(10, 11), 2.4, 2.4);
                p.drawEllipse(QPointF(18, 10), 1.9, 1.9);
                p.drawEllipse(QPointF(15, 18), 2.7, 2.7);
            });
    }
    return {};
}

QIcon anchorIcon() {
    return makeIcon([](QPainter& p, const QColor&) {
        QPolygonF diamond;
        diamond << QPointF(14, 5) << QPointF(23, 14) << QPointF(14, 23) << QPointF(5, 14);
        p.drawPolygon(diamond);
        p.drawPoint(14, 14);
    });
}

QIcon regionIcon() {
    return makeIcon([](QPainter& p, const QColor&) {
        QPen dashed = p.pen();
        dashed.setStyle(Qt::DashLine);
        dashed.setWidthF(1.8);
        p.setPen(dashed);
        p.drawRect(5, 6, 18, 16);
        QPen solid = p.pen();
        solid.setStyle(Qt::SolidLine);
        p.setPen(solid);
        // Esquinas marcadas: "enfocar aquí".
        p.drawLine(5, 10, 5, 6);
        p.drawLine(5, 6, 9, 6);
        p.drawLine(19, 22, 23, 22);
        p.drawLine(23, 22, 23, 18);
    });
}

}  // namespace pci::inspection
