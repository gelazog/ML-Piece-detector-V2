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
    // --- Corrección manual del borde ---
    //
    // Dos máscaras del tamaño del frame (vacías = sin corrección): lo que el
    // operador ha marcado como pieza a mano, y lo que ha marcado como fondo. Se
    // aplican justo después de segmentar, en el mismo sitio donde la zona libre
    // recorta, y por la misma razón: sobre la máscara ya segmentada, no sobre la
    // imagen.
    //
    // Dos máscaras y no una con tres estados porque así cada una dice UNA cosa y
    // se combinan sin ambigüedad. Y en este orden: primero se añade lo que falta
    // y después se quita lo que sobra, de modo que marcar fondo sobre algo que
    // se acababa de marcar pieza gana lo último que hizo el operador — que es lo
    // que espera cualquiera que haya usado un pincel.
    //
    // OJO: esto solo tiene sentido sobre una imagen QUIETA. En vídeo en vivo el
    // contorno se recalcula en cada frame, así que un borde corregido a mano
    // sería mentira en cuanto la pieza se moviera un píxel. Quien lo ofrezca
    // tiene que ofrecerlo solo con una foto o un fichero.
    cv::Mat forcePiece;
    cv::Mat forceBackground;
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

// La máscara de la pieza **con sus agujeros**.
//
// `analyzeFrame` devuelve la máscara del contorno exterior RELLENA, y lo hace a
// propósito: el fixture no puede sesgarse con los blobs de ruido que sobreviven
// a la morfología. Pero rellenar borra los agujeros, y con ellos desaparecen el
// diámetro interior de una arandela, el recuento de agujeros y la medida de cada
// uno — justo las cotas que definen esa clase de pieza.
//
// El fallo no se veía en el banco de pruebas porque allí las máscaras se dibujan
// a mano y conservan su agujero; solo se veía sondeando una imagen de verdad,
// donde una arandela salía clasificada como «círculo» y con dos cotas.
//
// Se vuelve a segmentar y se cruza con la máscara rellena: eso devuelve los
// agujeros y a la vez descarta cualquier mancha fuera de la pieza elegida. Va
// aparte y no dentro de `analyzeFrame` porque esto lo paga quien MIDE —un gesto
// puntual— y no cada frame del vídeo.
[[nodiscard]] cv::Mat pieceMaskWithHoles(const cv::Mat& image, const cv::Mat& filledMask,
                                         const SegmentationOptions& options = {});

}  // namespace pci::vision
