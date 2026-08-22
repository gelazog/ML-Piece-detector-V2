#pragma once

#include <opencv2/core.hpp>

#include "vision/segmentation.h"

namespace pci::vision {

// Corregir el borde a mano arregla ESTA imagen. Pero una corrección dice algo
// más: dice dónde se equivoca la detección, y con qué signo.
//
// Si el operador tiene que añadir a la pieza zonas que el umbral dejó fuera, el
// umbral es demasiado estricto. Si tiene que quitar sombras que entraron, es
// demasiado laxo. La corrección es, literalmente, la respuesta correcta para
// esta imagen — y con la respuesta correcta se puede buscar qué ajuste la
// habría dado solo.
//
// Eso es lo que hace esto: no adivina, MIDE. Prueba ajustes, compara cada
// resultado con lo que el operador dio por bueno, y devuelve el que más se
// parece junto con las dos cifras de parecido, la de ahora y la del propuesto.
// Sin las dos cifras la sugerencia no se puede juzgar.
struct SegmentationSuggestion {
    SegmentationOptions options;      // el ajuste que mejor reproduce la corrección
    double agreementNow = 0.0;        // parecido de los ajustes ACTUALES (0..1)
    double agreementSuggested = 0.0;  // parecido del propuesto (0..1)
    bool found = false;               // false si no se pudo evaluar nada
    // Si merece la pena proponerlo. Una mejora de dos milésimas no justifica
    // interrumpir a nadie, y proponer un cambio que no arregla nada gasta la
    // confianza que hace falta para cuando sí lo arregle.
    [[nodiscard]] bool worthApplying() const;
};

// Cuánto se parecen dos máscaras binarias: intersección entre unión (IoU).
//
// Se usa IoU y no «porcentaje de píxeles iguales» porque el segundo miente:
// en una imagen donde la pieza ocupa el 10%, decir «todo es fondo» acierta el
// 90% de los píxeles y no detecta nada.
[[nodiscard]] double maskAgreement(const cv::Mat& a, const cv::Mat& b);

// Busca el ajuste de segmentación que mejor reproduce `truthMask` sobre
// `image`. `truthMask` es la máscara YA CORREGIDA: lo que el operador dice que
// es la pieza.
//
// La búsqueda es de grueso a fino sobre el umbral: barrido ancho para situar la
// zona buena y afinado alrededor. Recorrer los 256 valores de uno en uno sobre
// un frame grande cuesta más de un segundo, y esto corre a petición del
// operador, que está mirando.
[[nodiscard]] SegmentationSuggestion suggestSegmentation(const cv::Mat& image,
                                                         const cv::Mat& truthMask,
                                                         const SegmentationOptions& current);

}  // namespace pci::vision
