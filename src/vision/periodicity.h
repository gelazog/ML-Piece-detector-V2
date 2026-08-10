#pragma once

#include <vector>

namespace pci::vision {

// Medida del periodo de una señal repetitiva. La rosca y el engranaje son el
// mismo problema visto de dos maneras: el perfil de una rosca a lo largo del
// eje se repite cada paso, y el perfil radial de un engranaje se repite cada
// diente. El paso y el número de dientes salen los dos de aquí.
//
// Por qué autocorrelación y no contar picos: contar picos parece más simple
// hasta que la pieza tiene un diente mellado, una rebaba o ruido — entonces
// aparece o desaparece un pico y el recuento se descuadra entero. La
// autocorrelación mira la señal completa, así que un periodo defectuoso baja la
// confianza pero no cambia el resultado. Hay un test que lo comprueba con un
// diente dañado.

struct PeriodEstimate {
    // Periodo en MUESTRAS, con parte fraccionaria (el pico se refina por
    // interpolación parabólica; un paso de rosca rara vez cae en un número
    // entero de estaciones).
    double period = 0.0;
    // Cuán periódica es la señal, 0..1: es el valor de la autocorrelación
    // normalizada en el pico. 1 = se repite exacta; por debajo de ~0,5 conviene
    // desconfiar del resultado en vez de publicarlo como medida.
    double confidence = 0.0;
    bool valid = false;
};

// Periodo dominante de `signal`, buscando entre `minPeriod` y `maxPeriod`
// muestras.
//
// `circular` distingue los dos casos reales y no es un detalle: el perfil
// radial de un engranaje recorre una vuelta completa y **cierra sobre sí
// mismo**, así que la correlación debe dar la vuelta y usa todas las muestras
// en todos los desfases. El perfil de una rosca a lo largo del eje no cierra:
// ahí la correlación es lineal y el solape se acorta al crecer el desfase.
//
// En el caso lineal se le quita a la señal su tendencia recta antes de
// correlar, que es como se separa la conicidad de una pieza del rizado que
// interesa. En el circular solo se le quita la media: restar una recta a una
// señal que cierra crearía un escalón artificial justo en el cierre.
//
// Devuelve valid=false si la señal es demasiado corta para contener dos
// periodos del tamaño pedido, o si los límites no tienen sentido.
[[nodiscard]] PeriodEstimate dominantPeriod(const std::vector<double>& signal,
                                            double minPeriod, double maxPeriod,
                                            bool circular = false);

}  // namespace pci::vision
