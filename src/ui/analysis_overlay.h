#pragma once

#include <QImage>
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
    // Recorte canónico orientado de la pieza (256x256): alimenta el panel de
    // comparación "registrada vs actual".
    QImage normalized;
};

}  // namespace pci::ui
