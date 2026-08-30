#pragma once

#include <QImage>
#include <QPointF>
#include <QPolygonF>
#include <QSize>
#include <QString>

#include <vector>

#include "inspection_editor/execution/tool_executor.h"
#include "vision/pipeline.h"

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
    // Reparto del tiempo de este análisis (R2). Solo se rellena si el operador
    // encendió el desglose en la pestaña de Rendimiento: apagado no se llama al
    // reloj ni una vez, porque esto corre en cada frame.
    vision::StageTimings timings;
    bool timed = false;
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
    // EL CONTORNO DE TODAS, en orden de lectura.
    //
    // Sale de una queja de uso: «solo toma un borde de una sola pieza, aunque
    // diga que haya muchos». Era literal — el recuento decía «6 piezas» y en
    // pantalla se dibujaba UNA línea verde. El operador no tenía forma de saber
    // cuáles eran las otras cinco, ni si el programa las había encontrado donde
    // él las veía o en cualquier otro sitio.
    //
    // Vacío = no se contaron. `contour` sigue siendo el de la pieza MEDIDA, que
    // es el que usa todo lo demás.
    std::vector<QPolygonF> pieceContours;
    // CUÁL de ellas es la que se ha medido, numerada en orden de lectura y
    // empezando por 1. -1 = no se contaron, así que la pregunta no aplica.
    //
    // Hace falta decirlo, y no solo elegirlo: con seis piezas en la mesa, un
    // informe que no dice de cuál de las seis son las cotas no se puede
    // interpretar ni repetir al día siguiente.
    int measuredPiece = -1;
    // Cuantas se estan TRATANDO como piezas, que con un numero declarado a mano
    // puede ser menos que las que se ven.
    //
    // Son dos cifras y no una porque dicen cosas distintas, y juntarlas seria
    // mentir en una de las dos direcciones: `piecesFound` es lo que hay delante
    // de la camara y `piecesUsed` es con lo que trabaja el programa. Si se
    // guardara solo la segunda, una sombra de mas desapareceria del informe sin
    // dejar rastro; si se guardara solo la primera, el operador que declaro seis
    // veria siete y no sabria cuales se han medido.
    int piecesUsed = -1;
    // MANCHAS QUE NO LLEGAN AL ÁREA MÍNIMA, y por eso no llegan a ser piezas.
    //
    // No es lo mismo que `piecesFound - piecesUsed`, que son las que sobran del
    // número declarado. Estas se caen ANTES de contarse, así que sin este campo
    // no aparecían en ningún sitio: el operador veía «1 pieza» sobre una foto
    // con dieciséis y no tenía ni el número ni qué tocar.
    int piecesTooSmall = 0;
    // Y EL ÁREA DE CADA MANCHA, filtrada o no, en px². No se pinta: viaja para
    // que el ajuste que decide con ella pueda enseñar qué pasaría con otro
    // valor sin volver a segmentar.
    std::vector<double> blobAreas;
    // Si este frame llegó a SEGMENTARSE. Con la pose congelada (contorno
    // oculto) no se segmenta: las herramientas se miden con el fixture del
    // frame anterior y no hay contorno nuevo. Distinguirlo de «se segmentó y no
    // había pieza» importa, porque la zona automática decide con eso: dar por
    // perdida una pieza que nadie ha buscado es afirmar lo que no se ha mirado.
    bool analysed = false;
};

}  // namespace pci::ui
