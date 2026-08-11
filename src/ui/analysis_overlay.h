#pragma once

#include <QImage>
#include <QPointF>
#include <QPolygonF>
#include <QSize>
#include <QString>

#include <vector>

#include "inspection_editor/execution/tool_executor.h"

namespace pci::ui {

// Resultado del análisis de visión listo para dibujar sobre el video.
// Todas las coordenadas están en píxeles del frame original.
struct AnalysisOverlay {
    bool valid = false;
    QPolygonF contour;
    QPointF centroid;      // centro de MASA (fixture.origin)
    // Centro GEOMÉTRICO del contorno (minAreaRect): es el que se ve centrado y
    // el que usa el tablero cuando se centra automáticamente. En piezas
    // asimétricas no coincide con el centroide.
    QPointF boundsCenter;
    double angleDeg = 0.0;
    QSize frameSize;
    QString error;
    // Recorte canónico orientado de la pieza (256x256): alimenta el panel de
    // comparación "registrada vs actual".
    QImage normalized;
    // Medición en vivo de las herramientas dibujadas sobre este frame.
    std::vector<inspection::ToolRunResult> toolResults;
    // Escala derivada del marcador ArUco en este frame (0 = no detectado).
    double liveMmPerPixel = 0.0;
    // Calidad de esa escala (0..1): 1.0 = cámara perpendicular al plano; baja
    // con perspectiva/inclinación (D5). Solo válida si liveMmPerPixel > 0.
    double liveScaleQuality = 0.0;
    // Nitidez para el asistente de enfoque (C2), medida SOBRE LA PIEZA cuando
    // se detecta. `sharpnessOnPiece` distingue eso de la medida de reserva
    // sobre el centro del encuadre: con fondo texturizado, la del frame entero
    // puede subir mientras la de la pieza baja.
    double sharpness = 0.0;
    bool sharpnessOnPiece = false;
    // Piezas encontradas en el frame (C5). -1 = no se contaron: contar cuesta y
    // solo se hace cuando alguien mira el número.
    int piecesFound = -1;
    // Si este frame llegó a SEGMENTARSE. Con la pose congelada (contorno
    // oculto) no se segmenta: las herramientas se miden con el fixture del
    // frame anterior y no hay contorno nuevo. Distinguirlo de «se segmentó y no
    // había pieza» importa, porque la zona automática decide con eso: dar por
    // perdida una pieza que nadie ha buscado es afirmar lo que no se ha mirado.
    bool analysed = false;
};

}  // namespace pci::ui
