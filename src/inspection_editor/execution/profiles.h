#pragma once

#include <opencv2/core.hpp>

#include <vector>

namespace pci::inspection {

// Las dos formas de "recorrer un borde" que necesitan las herramientas de pieza
// torneada. Las dos devuelven una señal muestreada uniformemente, y eso es lo
// importante: la rosca y el engranaje son señales PERIÓDICAS —el perfil se
// repite cada paso, o cada diente— y el paso y el número de dientes salen de su
// periodo. Un muestreo irregular haría inútil ese cálculo.
//
// Viven aquí y no en `vision/` porque usan `detectEdges`, que está en esta
// capa; `pci_vision` está por debajo y no puede depender de ella.

// Una muestra puede no encontrar borde (un hueco, un reflejo, un tramo sin
// contraste). Se devuelve igualmente con `found=false` en lugar de omitirla:
// quitarla desplazaría todas las demás y rompería la uniformidad del muestreo,
// que es justo de lo que depende la medida del periodo.
struct RadialSample {
    double angleDeg = 0.0;  // ángulo del rayo en [0, 360)
    double radius = 0.0;    // distancia del centro al borde (px), si found
    double strength = 0.0;  // |gradiente| del borde encontrado
    cv::Point2f point{0.0F, 0.0F};  // el borde, en coordenadas de imagen
    bool found = false;
};

// Barrido radial desde `center`: para cada uno de los `rayCount` ángulos
// repartidos por igual, busca el borde entre los radios `rMin` y `rMax`.
//
// El ángulo se mide desde el eje +X y crece hacia +Y. Ojo: en coordenadas de
// imagen +Y va hacia ABAJO, así que el barrido avanza en el sentido de las
// agujas del reloj tal como se ve en pantalla. Da igual para medir un periodo,
// pero importa al dibujar.
//
// Es la base del engranaje (el perfil r(θ) se repite una vez por diente) y del
// arco. Devuelve vacío si el centro o los radios no tienen sentido.
[[nodiscard]] std::vector<RadialSample> radialProfile(const cv::Mat& gray,
                                                      cv::Point2f center, double rMin,
                                                      double rMax, int rayCount,
                                                      float thickness = 3.0F);

struct AxialSample {
    double t = 0.0;         // posición a lo largo del eje, en px desde `from`
    double offset = 0.0;    // distancia perpendicular del eje al borde (px)
    double strength = 0.0;
    cv::Point2f point{0.0F, 0.0F};
    bool found = false;
};

// Lado del eje que se explora, definido sin ambigüedad respecto a la normal
// que devuelve `profileNormal`: `Positive` explora hacia +n y `Negative` hacia
// −n. Se nombra así, y no "arriba/abajo" ni "izquierda/derecha", porque cuál de
// esos es depende de cómo se haya trazado el eje.
enum class ProfileSide { Positive, Negative };

// Normal unitaria del eje `from`->`to`: (−dy, dx) normalizada. Se expone para
// que quien llame pueda calcular el mismo vector que usa el barrido y no tenga
// que adivinar el convenio. Vector nulo si el eje es degenerado.
[[nodiscard]] cv::Point2f profileNormal(cv::Point2f from, cv::Point2f to);

// Perfil a lo largo de un eje: en cada una de las `stations` estaciones
// repartidas por igual entre `from` y `to`, busca el borde más fuerte
// explorando perpendicularmente hasta `reach` píxeles por el lado indicado.
//
// Es la base del eje torneado (dos perfiles, uno por lado: el diámetro es la
// suma de los dos offsets) y de la rosca (un perfil; su rizado periódico es el
// paso). Devuelve vacío si el eje es degenerado o los parámetros no tienen
// sentido.
[[nodiscard]] std::vector<AxialSample> axialProfile(const cv::Mat& gray, cv::Point2f from,
                                                    cv::Point2f to, ProfileSide side,
                                                    int stations, double reach,
                                                    float thickness = 3.0F);

// Cuántas muestras encontraron borde. Es la comprobación de cordura antes de
// fiarse de cualquier medida sacada de un perfil.
[[nodiscard]] int foundCount(const std::vector<RadialSample>& profile);
[[nodiscard]] int foundCount(const std::vector<AxialSample>& profile);

}  // namespace pci::inspection
