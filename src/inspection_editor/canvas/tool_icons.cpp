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
        case ToolType::Gear:
            return makeIcon([](QPainter& p, const QColor&) {
                // Rueda dentada: un aro con dientes radiales.
                p.drawEllipse(QPointF(15, 15), 7.0, 7.0);
                for (int k = 0; k < 8; ++k) {
                    const double a = k * 3.14159265358979323846 / 4.0;
                    p.drawLine(QPointF(15 + 7 * std::cos(a), 15 + 7 * std::sin(a)),
                               QPointF(15 + 11 * std::cos(a), 15 + 11 * std::sin(a)));
                }
            });
        case ToolType::Thread:
            return makeIcon([](QPainter& p, const QColor&) {
                // Perfil dentado a los dos lados de un eje.
                const QPolygonF top({QPointF(4, 11), QPointF(8, 5), QPointF(12, 11),
                                     QPointF(16, 5), QPointF(20, 11), QPointF(24, 5),
                                     QPointF(27, 11)});
                p.drawPolyline(top);
                p.drawPolyline(top.translated(0, 8));
                p.drawLine(4, 15, 27, 15);
            });
        case ToolType::Shaft:
            return makeIcon([](QPainter& p, const QColor&) {
                // Dos bordes paralelos con el eje discontinuo por el medio.
                p.drawLine(4, 8, 26, 8);
                p.drawLine(4, 20, 26, 20);
                p.drawLine(4, 14, 8, 14);
                p.drawLine(12, 14, 18, 14);
                p.drawLine(22, 14, 26, 14);
            });
        case ToolType::Arc:
            return makeIcon([](QPainter& p, const QColor&) {
                // Un arco con la flecha del radio saliendo de su centro.
                p.drawArc(QRectF(4, 4, 22, 22), 20 * 16, 140 * 16);
                p.drawLine(15, 15, 6, 9);
                p.drawLine(6, 9, 9, 9);
                p.drawLine(6, 9, 6, 12);
            });
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
        case ToolType::EdgeDefects:
            return makeIcon([](QPainter& p, const QColor& c) {
                // Un borde con DOS defectos, uno hacia fuera y otro hacia
                // dentro: es lo que la distingue del Borde liso, que da uno solo.
                QPolygonF edge;
                edge << QPointF(3, 16) << QPointF(8, 16) << QPointF(9, 9)
                     << QPointF(11, 16) << QPointF(17, 16) << QPointF(19, 22)
                     << QPointF(21, 16) << QPointF(25, 16);
                p.drawPolyline(edge);
                p.setBrush(c);
                p.setPen(Qt::NoPen);
                p.drawEllipse(QPointF(9, 9), 1.6, 1.6);
                p.drawEllipse(QPointF(19, 22), 1.6, 1.6);
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
        case ToolType::LineToLine:
            return makeIcon([](QPainter& p, const QColor&) {
                // Dos líneas que forman un ángulo, con un arco entre ellas.
                p.drawLine(5, 23, 24, 8);   // línea A
                p.drawLine(5, 23, 24, 20);  // línea B
                QPen thin = p.pen();
                thin.setWidthF(1.2);
                p.setPen(thin);
                p.setBrush(Qt::NoBrush);
                p.drawArc(QRectF(1, 15, 16, 16), 0 * 16, 38 * 16);  // arco del ángulo
            });
        case ToolType::Angle:
            return makeIcon([](QPainter& p, const QColor& c) {
                // Esquina: vértice con dos lados y un arco marcando el ángulo.
                p.drawLine(6, 22, 6, 4);    // lado vertical
                p.drawLine(6, 22, 24, 22);  // lado horizontal
                p.setBrush(c);
                p.drawEllipse(QPointF(6, 22), 1.8, 1.8);  // vértice
                QPen thin = p.pen();
                thin.setWidthF(1.2);
                p.setPen(thin);
                p.setBrush(Qt::NoBrush);
                p.drawArc(QRectF(0, 16, 12, 12), 0 * 16, 90 * 16);  // arco de 90°
            });
        case ToolType::PolyBlob:
            return makeIcon([](QPainter& p, const QColor& c) {
                // Polígono libre con un par de manchas dentro.
                QPolygonF poly;
                poly << QPointF(6, 8) << QPointF(20, 5) << QPointF(24, 17)
                     << QPointF(14, 24) << QPointF(4, 17);
                p.drawPolygon(poly);
                p.setBrush(c);
                p.setPen(Qt::NoPen);
                p.drawEllipse(QPointF(12, 13), 2.0, 2.0);
                p.drawEllipse(QPointF(18, 15), 1.6, 1.6);
            });
        case ToolType::Polygon:
            return makeIcon([](QPainter& p, const QColor& c) {
                // Un hexagono -la tuerca- con sus vertices marcados.
                QPolygonF hex;
                for (int k = 0; k < 6; ++k) {
                    const double a = k * 3.14159265358979323846 / 3.0;
                    hex << QPointF(14 + 10 * std::cos(a), 14 + 10 * std::sin(a));
                }
                p.drawPolygon(hex);
                p.setBrush(c);
                p.setPen(Qt::NoPen);
                for (const QPointF& v : hex) {
                    p.drawEllipse(v, 1.7, 1.7);
                }
            });
        case ToolType::Symmetry:
            return makeIcon([](QPainter& p, const QColor&) {
                // Dos mitades iguales enfrentadas y el eje de trazo y punto que
                // las separa: el dibujo de un plano para "simétrico respecto a".
                QPolygonF left;
                left << QPointF(11, 5) << QPointF(4, 12) << QPointF(7, 23)
                     << QPointF(11, 23);
                p.drawPolyline(left);
                QPolygonF right;
                right << QPointF(17, 5) << QPointF(24, 12) << QPointF(21, 23)
                      << QPointF(17, 23);
                p.drawPolyline(right);
                QPen axis = p.pen();
                axis.setStyle(Qt::DashDotLine);
                axis.setWidthF(1.4);
                p.setPen(axis);
                p.drawLine(14, 2, 14, 26);
            });
        case ToolType::Region:
            return makeIcon([](QPainter& p, const QColor& c) {
                // Una silueta con su area tramada: lo que se mide es la region,
                // no su borde.
                QPolygonF blob;
                blob << QPointF(6, 9) << QPointF(15, 4) << QPointF(23, 11)
                     << QPointF(21, 21) << QPointF(10, 23);
                p.drawPolygon(blob);
                QPen hatch(c);
                hatch.setWidthF(0.9);
                p.setPen(hatch);
                for (int k = -8; k < 24; k += 5) {
                    p.drawLine(QPointF(k + 2, 24), QPointF(k + 14, 4));
                }
            });
        case ToolType::MedianAxis:
            return makeIcon([](QPainter& p, const QColor&) {
                // Dos flancos tenues y, entre ellos, el eje de trazo y punto —
                // el símbolo de línea de eje de un plano.
                QPen faint = p.pen();
                faint.setWidthF(1.2);
                p.setPen(faint);
                p.drawLine(4, 7, 24, 7);
                p.drawLine(4, 21, 24, 21);
                QPen axis = p.pen();
                axis.setWidthF(2.0);
                axis.setStyle(Qt::DashDotLine);
                p.setPen(axis);
                p.drawLine(3, 14, 25, 14);
            });
        case ToolType::ConstructedPoint:
            return makeIcon([](QPainter& p, const QColor& c) {
                // Dos rectas de origen finas y punteadas, y el punto que sale de
                // ellas bien sólido: lo construido se ve, lo que lo construye se
                // insinúa.
                QPen faint = p.pen();
                faint.setStyle(Qt::DashLine);
                faint.setWidthF(1.2);
                p.setPen(faint);
                p.drawLine(3, 8, 25, 20);
                p.drawLine(3, 20, 25, 8);
                p.setPen(Qt::NoPen);
                p.setBrush(c);
                p.drawEllipse(QPointF(14, 14), 3.2, 3.2);
            });
        case ToolType::ConstructedLine:
            return makeIcon([](QPainter& p, const QColor& c) {
                // Dos puntos de origen y la recta que pasa por ellos.
                QPen solid = p.pen();
                p.drawLine(4, 21, 24, 7);
                QPen faint = p.pen();
                faint.setStyle(Qt::DashLine);
                faint.setWidthF(1.2);
                p.setPen(faint);
                p.setBrush(Qt::NoBrush);
                p.drawEllipse(QPointF(8, 18), 3.6, 3.6);
                p.drawEllipse(QPointF(20, 10), 3.6, 3.6);
                p.setPen(Qt::NoPen);
                p.setBrush(c);
                p.drawEllipse(QPointF(8, 18), 1.8, 1.8);
                p.drawEllipse(QPointF(20, 10), 1.8, 1.8);
                p.setPen(solid);
            });
        case ToolType::Position:
            return makeIcon([](QPainter& p, const QColor& c) {
                // Diana con ejes: "dónde debe caer este rasgo".
                QPen thin = p.pen();
                thin.setWidthF(1.2);
                p.setPen(thin);
                p.drawLine(14, 3, 14, 25);
                p.drawLine(3, 14, 25, 14);
                p.setPen(c);
                p.setBrush(Qt::NoBrush);
                p.drawEllipse(QPointF(14, 14), 7.0, 7.0);
                p.setBrush(c);
                p.drawEllipse(QPointF(17, 10), 2.0, 2.0);  // rasgo fuera del cero
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
