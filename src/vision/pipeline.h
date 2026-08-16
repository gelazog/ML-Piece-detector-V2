#pragma once

#include <opencv2/core.hpp>

#include <vector>

#include "core/result.h"
#include "vision/segmentation.h"
#include "vision/types.h"

namespace pci::vision {

struct PipelineConfig {
    double minAreaFraction = 0.005;
    double maxAreaFraction = 0.9;
    int canonicalSize = 256;
    // Si es false, la pieza no se rota al eje principal: se deja vertical tal
    // como la ve la cámara (recorte upright). El eje principal de los momentos
    // es arbitrario e inestable en piezas poco alargadas, así que por defecto
    // no se sigue la rotación (más estable y sin inclinación espuria).
    bool autoOrient = false;
    SegmentationOptions segmentation;
    // Zona de detección: si no está vacía, el contorno automático solo se
    // busca dentro de este rectángulo (coords de imagen) — luces, sombras y
    // objetos fuera de la zona dejan de estorbar. Los resultados se devuelven
    // en coordenadas de la imagen completa.
    cv::Rect roi;
    // Zona de detección LIBRE: un polígono en coordenadas de imagen. Cuando
    // tiene al menos tres vértices manda sobre `roi` y solo se busca pieza
    // dentro de él.
    //
    // Existe porque un rectángulo obliga a elegir entre dejar fuera parte de la
    // pieza o dejar dentro lo que estorba, y en una mesa real lo que estorba
    // —el borde del útil, la sombra pegada a un lado, la pieza de al lado— casi
    // nunca cae en un rectángulo que no toque también a la pieza. Con un
    // polígono se ciñe al hueco que de verdad ocupa.
    //
    // El rectángulo NO desaparece: la envolvente del polígono se sigue usando
    // para recortar, así que la ganancia de velocidad de la zona se conserva y
    // el polígono solo añade precisión. Las dos cosas suman en vez de competir.
    std::vector<cv::Point> roiPolygon;
    // Cuántas piezas se esperan en la imagen (C5). No cambia la detección: la
    // usa quien juzga, para poder decir "esperaba 6, veo 5". 0 = no vigilar.
    int expectedPieces = 1;
};

// Reparto del tiempo de un análisis, en milisegundos (R2).
//
// Existe porque los tiempos que hay documentados se midieron una vez con un
// programa suelto: sirvió para decidir entonces, y no sirve para saber si hoy
// sigue siendo verdad en otra máquina, con otra resolución y con herramientas
// dibujadas. Optimizar sin esto es optimizar a ciegas, que es como se llegó a
// implementar y retirar la escala de trabajo adaptativa.
//
// `tools` no lo rellena `analyzeFrame` —`runTools` corre más arriba, sobre el
// resultado— pero vive aquí para que el reparto que ve el operador sea el del
// frame entero y no el de un trozo.
struct StageTimings {
    double segment = 0.0;
    double contour = 0.0;
    double fixture = 0.0;
    double normalize = 0.0;
    double tools = 0.0;
    double total = 0.0;

    [[nodiscard]] double stagesSum() const {
        return segment + contour + fixture + normalize + tools;
    }
};

// Punto de entrada único del módulo: segmentación -> contorno mayor ->
// fixture -> recorte normalizado. Todos los fallos regresan como Result.
//
// `timings` opcional: si es nulo NO se llama al reloj ni una vez. Un cronómetro
// por etapa que corriera siempre sería pagar en el sitio más caliente del
// programa para que nadie lo mire; así el desglose se enciende desde la pestaña
// de Rendimiento y, apagado, cuesta una comparación con nulo por etapa.
core::Result<PieceAnalysis> analyzeFrame(const cv::Mat& image,
                                         const PipelineConfig& config = {},
                                         StageTimings* timings = nullptr);

// Todas las piezas de la imagen, **ordenadas de mayor a menor**.
//
// Va aparte de `analyzeFrame` y no al revés (analyzeFrame = la primera de
// estas) por una razón de coste: el camino de una sola pieza es el que corre en
// cada frame del vídeo, y hacerle analizar también las manchas de ruido que
// pasan el filtro de área sería pagar de más en el sitio más caliente.
[[nodiscard]] core::Result<std::vector<PieceAnalysis>> analyzeFrames(
    const cv::Mat& image, const PipelineConfig& config = {});

}  // namespace pci::vision
