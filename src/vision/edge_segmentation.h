#pragma once

#include <opencv2/core.hpp>

#include <string>

#include "core/result.h"

namespace pci::vision {

// SEGMENTAR POR EL BORDE, no por el nivel.
//
// Otsu pregunta «¿este píxel es más claro o más oscuro que el corte?». Eso da
// por supuesto que la pieza entera cae de un lado del corte y el fondo del otro,
// y hay una familia de escenas donde eso es falso: piezas metálicas sobre una
// mesa clara.
//
// MEDIDO sobre una foto de siete tuercas surtidas (`tests/test_edge_segmentation`):
//
//     fondo de la mesa            176, con una desviación de 7
//     interiores de las piezas    medias de 96 a 172, con valores de 12 a 252
//     corte que elige Otsu        134
//
// Las piezas tienen reflejos POR ENCIMA del fondo y sombras POR DEBAJO, así que
// el corte que recoge una deja fuera a otra. El resultado en esa foto: seis
// contornos en vez de siete, tres piezas distintas fundidas en uno por puentes
// de sombra, y todos ellos trazando solo los trozos oscuros de cada pieza.
//
// Pero el BORDE sí las separa, y por mucho:
//
//     gradiente del fondo          4
//     gradiente en el canto        hasta 469
//
// Así que en vez de preguntar «¿esto es pieza o fondo?», este método pregunta
// «¿se puede llegar aquí desde fuera del encuadre sin cruzar un borde?». Lo que
// no se alcanza es pieza, con su interior entero, brille lo que brille.
//
// NO SUSTITUYE A OTSU, Y ESO ESTÁ MEDIDO. En la misma tanda de pruebas: con las
// tuercas encuentra las siete y Otsu seis; con una bola oscura sobre fondo claro
// encuentra tres donde hay una, y Otsu acierta. Son dos herramientas para dos
// escenas, y por eso esto es una opción y no un cambio de algoritmo.
//
// Cuándo elegirlo, dicho en una frase: cuando las piezas tengan a la vez brillos
// más claros y sombras más oscuras que la mesa. `edgeSegmentationLooksBetter`
// contesta esa pregunta con la imagen delante.

struct EdgeSegmentationOptions {
    // Cuántas veces por encima del ruido de gradiente del fondo tiene que estar
    // un píxel para contar como borde.
    //
    // 6 sale de barrer la tanda: con 4 el ruido de la mesa se cuela y parte las
    // piezas; con 8 los cantos de poco contraste se pierden y el relleno se
    // escapa por el hueco, que se lleva la pieza entera.
    double edgeSigmas = 6.0;
    // Cuánto se engorda el trazo del borde antes de inundar, en píxeles.
    //
    // Un borde real tiene huecos donde el contraste baja, y UN SOLO hueco deja
    // escapar el relleno: no se pierde un trozo de la pieza, se pierde la pieza.
    // Engordar los tapa. El precio está medido y es el que fija este valor en 1:
    // con 2, dos piezas separadas por menos de ocho píxeles se fusionan, y en la
    // foto de las tuercas eso hacía pasar de siete piezas a seis.
    int closeRadiusPx = 1;
    // Área mínima de pieza, en fracción del encuadre. La misma que el resto del
    // programa, para que no haya dos criterios de «esto es demasiado pequeño».
    double minAreaFraction = 0.005;
};

// Máscara binaria CV_8UC1 con pieza = 255, o el motivo por el que no se pudo.
[[nodiscard]] core::Result<cv::Mat> segmentByEdges(const cv::Mat& image,
                                                   const EdgeSegmentationOptions& options = {});

// Lo que hace falta saber para elegir método, medido sobre la imagen que hay
// delante.
struct SceneReading {
    // Nivel de gris del fondo y su ruido, estimados en el marco exterior.
    double backgroundLevel = 0.0;
    double backgroundNoise = 0.0;
    // Qué fracción de la imagen queda MÁS CLARA que el fondo y qué fracción más
    // oscura, contando solo lo que se aparta de él de forma apreciable.
    double brighterThanBackground = 0.0;
    double darkerThanBackground = 0.0;
    // Si las piezas caen a los dos lados del fondo. Es la condición que rompe a
    // Otsu, y la que hace falta para que segmentar por el borde compense.
    bool piecesStraddleTheBackground = false;
    // En castellano, para poder enseñárselo al operador.
    std::string summary;
};

[[nodiscard]] SceneReading readScene(const cv::Mat& image);

// Si conviene segmentar por el borde en ESTA imagen. Es `readScene` resumido a
// un sí o un no, para poder ofrecerlo sin que el operador tenga que interpretar
// nada.
[[nodiscard]] bool edgeSegmentationLooksBetter(const cv::Mat& image);

// ¿ESTÁ EL UMBRAL CORTANDO LA PIEZA?
//
// El fallo que esto detecta es el peor que puede tener una aplicación de
// medida: no falla, no avisa, y devuelve un número creíble y corto. Medido
// sobre las fotos reales del usuario, el umbral automático se come la cabeza
// cromada de un tornillo y le quita el 36 % de su área — y el aviso de contorno
// sucio no salta, porque el contorno recortado es perfectamente limpio.
//
// LA SEÑAL: aflojar el umbral unos niveles HACIA el fondo y mirar cuánta pieza
// nueva aparece. Con una pieza bien separada, entre ella y el fondo hay un
// desierto de grises y aflojar no encuentra nada. Si aparece mucha, es que
// había masa de pieza pegada al corte — o sea, que el corte estaba dentro de la
// pieza y no en su borde.
//
// Lo que la hace utilizable: NO HACE FALTA SABER LA VERDAD. No se compara con
// el área buena, que nadie conoce en producción: se compara la imagen consigo
// misma.
//
// Medido sobre las siete imágenes reales disponibles:
//
//   engranaje-1        +5,7 %      nivel correcto
//   engranajes-1       +5,5 %      nivel correcto (su problema es otro)
//   tornillo-1         +2,5 %      nivel correcto
//   tuerca suelta      +3,9 %      nivel correcto
//   bandeja de 100     +4,6 %      nivel correcto
//   tornillo-2        +15,4 %      CORTA — le faltaba el 32 % del área
//   tornillos-1       +23,8 %      CORTA — le faltaba el 36 % del área
//
// Todo lo correcto por debajo del 6 %, todo lo cortado por encima del 15 %. El
// umbral se pone en el 10 %, en medio del hueco y no pegado a ninguno de los
// dos lados.
//
// SE PROBÓ una versión barata que solo umbraliza y cuenta píxeles, sin pasar
// por el pipeline: cuesta menos de 1 ms pero NO SEPARA — daba 11,9 % en una
// imagen correcta contra 13,3 % en una cortada. El filtrado por área y la
// morfología del pipeline son lo que quita el ruido que confunde la señal, así
// que hay que pagarlos.
struct ClippingCheck {
    // Cuánta área aparece al aflojar el umbral, en fracción de la que había.
    double swing = 0.0;
    // Si eso es bastante como para afirmar que el umbral está dentro de la
    // pieza y no en su borde.
    bool thresholdCutsThePiece = false;
    // Cuántos niveles de gris se aflojó, para poder decirlo en el aviso.
    int loosenedBy = 0;
    // En castellano, para enseñárselo al operador.
    std::string summary;
};

// El hueco medido va de 5,7 % a 15,4 %; el umbral se pone en medio.
inline constexpr double kThresholdCutsTheSwing = 0.10;
// Cuánto se afloja. Doce niveles es lo que se midió, y es la distancia a la que
// una cabeza cromada aparece sin que aparezcan además las sombras del fondo.
inline constexpr int kLoosenThresholdBy = 12;

// Cuesta DOS análisis completos de más —60 ms con cien piezas, medido— así que
// no se llama por fotograma: es una comprobación que se pide.
[[nodiscard]] ClippingCheck checkThresholdClipping(const cv::Mat& image);

}  // namespace pci::vision
