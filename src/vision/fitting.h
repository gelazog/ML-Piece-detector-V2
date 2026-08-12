#pragma once

#include <opencv2/core.hpp>

#include <vector>

namespace pci::vision {

// Ajustes geométricos sobre nubes de puntos de borde. Viven aquí, separados de
// las herramientas, porque los comparten el círculo, el arco, el eje, la rosca
// y el engranaje: todos acaban preguntando lo mismo — "qué circunferencia (o
// qué recta) explica estos puntos".

struct CircleFit {
    cv::Point2f center{0.0F, 0.0F};
    double radius = 0.0;
    // Residuo cuadrático medio a la circunferencia, en píxeles. Es la medida
    // honesta de cuánto se parece la nube a un círculo.
    double rmsResidual = 0.0;
    // Puntos que el ajuste consideró buenos. En el ajuste simple son todos.
    int inlierCount = 0;
    bool valid = false;
};

// Ajuste algebraico de Taubin.
//
// Por qué Taubin y no Kasa (el que se usaba): Kasa minimiza el residuo
// algebraico sin normalizar, lo que equivale a dar más peso a los puntos
// lejanos al centro. Sobre una circunferencia completa apenas se nota, pero
// sobre un ARCO PARCIAL —el radio de una esquina, un sector de engranaje— el
// radio sale sistemáticamente corto, y cuanto más corto es el arco, peor.
// Taubin normaliza por el gradiente y es prácticamente insesgado, con el mismo
// coste. Hay un test que mide esa diferencia en arcos de 30° a 360°.
//
// Necesita al menos 3 puntos no alineados; si no, devuelve valid=false.
[[nodiscard]] CircleFit fitCircleTaubin(const std::vector<cv::Point2f>& points);

// Taubin con reponderación iterativa (IRLS con biponderada de Tukey).
//
// El borde de una pieza real trae puntos que no pertenecen al círculo: una
// rebaba, un reflejo, una viruta pegada, un rayo que enganchó el borde
// equivocado. Un ajuste por mínimos cuadrados los promedia y desplaza el
// resultado; aquí se les baja el peso hasta anularlos, midiendo la dispersión
// con la MAD (mediana de desviaciones absolutas), que no se deja arrastrar por
// los propios atípicos.
//
// `inlierCount` dice cuántos puntos acabaron contando: si baja mucho respecto
// al total, la nube no era un círculo y el resultado no es de fiar.
[[nodiscard]] CircleFit fitCircleRobust(const std::vector<cv::Point2f>& points,
                                        int iterations = 5);

// Circunferencia que pasa por tres puntos, con el TRAMO que definen: el arco
// que va del primero al tercero pasando por el de en medio. Ese punto
// intermedio es lo que resuelve la ambigüedad — por dos extremos pasan dos
// arcos, el corto y el largo, y sin decir cuál no se sabe qué radio se está
// midiendo ni por dónde buscar el borde.
struct ArcSpan {
    cv::Point2f center{0.0F, 0.0F};
    double radius = 0.0;
    double startAngleDeg = 0.0;  // ángulo del primer punto, medido desde +X
    // Recorrido con signo desde el primer punto hasta el tercero, pasando por
    // el intermedio. Positivo = hacia +Y (en imagen, el sentido horario visto
    // en pantalla). |sweep| < 360.
    double sweepDeg = 0.0;
    bool valid = false;  // false si los tres puntos están alineados o repetidos
};

[[nodiscard]] ArcSpan circleThroughThreePoints(const cv::Point2f& start,
                                               const cv::Point2f& mid,
                                               const cv::Point2f& end);

// ¿Cae `angleDeg` dentro del recorrido que arranca en `startAngleDeg`? Sirve
// para saber si un punto está sobre el arco o más allá de sus extremos.
[[nodiscard]] bool angleWithinSweep(double angleDeg, double startAngleDeg, double sweepDeg);

struct LineFit {
    cv::Point2f point{0.0F, 0.0F};  // un punto de la recta (el centroide)
    // Dirección unitaria, en forma canónica: x > 0, o x = 0 e y > 0. Una recta
    // no tiene sentido, así que fijar el signo evita que el mismo conjunto de
    // puntos devuelva a veces d y a veces -d y que los ángulos salten 180°.
    cv::Point2f direction{1.0F, 0.0F};
    double rmsResidual = 0.0;  // distancia perpendicular cuadrática media (px)
    int inlierCount = 0;
    // Cuán alargada es la nube: **0 = redonda, ~1 = línea**. Misma definición
    // que `Fixture::anisotropy` (1 − √(λmenor/λmayor)), y por el mismo motivo:
    // una nube redonda no tiene eje principal, así que su dirección es ruido.
    // Se expone en vez de decidir por el llamante con un umbral escondido —
    // cuánta anisotropía hace falta depende de para qué se pida la recta.
    double anisotropy = 0.0;
    bool valid = false;

    // Distancia perpendicular con signo: positiva al lado izquierdo de
    // `direction`. El signo es lo que permite saber de qué lado del eje cae un
    // borde, que es como se separan los dos flancos de un eje torneado.
    [[nodiscard]] double signedDistance(const cv::Point2f& p) const;
    // Orientación en grados, normalizada a (-90, 90].
    [[nodiscard]] double angleDeg() const;
};

// Mínimos cuadrados totales: minimiza la distancia PERPENDICULAR a la recta,
// no el error vertical. La diferencia no es cosmética — el ajuste clásico
// `y = mx + b` no puede representar una recta vertical (la pendiente se va a
// infinito) y se degrada mucho antes de llegar a ella. Aquí la recta se
// describe por punto y dirección, así que todas las orientaciones cuestan lo
// mismo. Se resuelve con la forma cerrada del eje principal de la covarianza.
//
// Devuelve valid=false solo cuando no hay recta posible: menos de 2 puntos o
// todos en el mismo sitio. Para el caso intermedio —una nube redonda, donde sí
// sale una dirección pero no significa nada— está `anisotropy`, que es la que
// hay que mirar antes de fiarse del ángulo.
[[nodiscard]] LineFit fitLineTotal(const std::vector<cv::Point2f>& points);

// Igual, con reponderación iterativa contra atípicos (misma biponderada de
// Tukey sobre la MAD que el círculo). Lo necesitan los flancos de la rosca y
// los dos costados de un eje, donde una viruta o una marca de mecanizado mete
// puntos que no pertenecen al borde.
[[nodiscard]] LineFit fitLineRobust(const std::vector<cv::Point2f>& points,
                                    int iterations = 5);

// La banda MÁS ESTRECHA de dos rectas paralelas que contiene todos los puntos.
//
// Es el valor con el que la norma define la rectitud, y no es lo mismo que la
// desviación respecto a la recta de mínimos cuadrados: esa banda es una
// candidata más entre todas las orientaciones posibles, así que su anchura
// nunca puede ser menor que la de la mejor. `width` <= cualquier otra.
//
// Se resuelve con el casco convexo y calibres rotantes. El teorema que lo hace
// barato: la banda mínima **siempre apoya uno de sus lados en una arista del
// casco**, así que basta con probar tantas orientaciones como aristas tenga, y
// no un barrido de ángulos.
//
// **`cv::minAreaRect` NO sirve para esto.** Minimiza el ÁREA del rectángulo, no
// su anchura, y son dos mínimos distintos: un rectángulo un poco más ancho pero
// mucho más corto puede tener menos área. Es el error que hay que no cometer.
struct MinimumZone {
    double width = 0.0;             // anchura de la banda (px)
    cv::Point2f direction{1.0F, 0.0F};  // dirección de las dos rectas, unitaria
    cv::Point2f point{0.0F, 0.0F};      // un punto de la recta CENTRAL de la banda
    bool valid = false;
};

[[nodiscard]] MinimumZone minimumZoneBand(const std::vector<cv::Point2f>& points);

// Redondez por ZONA MÍNIMA (MZC), que es como la define la norma: los dos
// círculos CONCÉNTRICOS de separación radial más pequeña que contienen el
// perfil. El valor es esa separación, `outerRadius − innerRadius`.
//
// No es lo que da un ajuste de mínimos cuadrados. Ese fija el centro
// minimizando los residuos al cuadrado, que es otra cosa: el centro que menos
// separa el radio máximo del mínimo casi nunca es el mismo. La zona mínima
// nunca puede ser mayor que la del centro de mínimos cuadrados, porque ese
// centro es un candidato más.
//
// Se resuelve con Nelder-Mead sobre el centro, sembrado con el ajuste de
// Taubin. La función `max‖p−c‖ − min‖p−c‖` no es convexa, así que la semilla
// importa: partir del centro de mínimos cuadrados es partir muy cerca.
struct MinimumZoneCircle {
    cv::Point2f center{0.0F, 0.0F};
    double innerRadius = 0.0;
    double outerRadius = 0.0;
    bool valid = false;

    [[nodiscard]] double width() const { return outerRadius - innerRadius; }
};

[[nodiscard]] MinimumZoneCircle minimumZoneCircle(const std::vector<cv::Point2f>& points);

}  // namespace pci::vision
