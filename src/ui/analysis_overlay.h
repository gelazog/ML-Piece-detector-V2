#pragma once

#include <QPointF>
#include <QPolygonF>
#include <QSize>
#include <QString>

namespace pci::ui {

// Resultado del análisis de visión listo para dibujar sobre el video.
// Todas las coordenadas están en píxeles del frame original.
struct AnalysisOverlay {
    bool valid = false;
    QPolygonF contour;
    QPointF centroid;
    double angleDeg = 0.0;
    QSize frameSize;
    QString error;
};

}  // namespace pci::ui
