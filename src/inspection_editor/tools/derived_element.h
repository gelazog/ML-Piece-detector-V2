#pragma once

#include <opencv2/core.hpp>

#include <map>
#include <string>

namespace pci::inspection {

// Elemento geométrico que una herramienta produce además de su medida, para que
// OTRA lo use como referencia.
//
// Es el mecanismo del que cuelgan las construcciones geométricas y todo el
// GD&T, y existe porque paralelismo, perpendicularidad, angularidad y posición
// verdadera **no son medidas absolutas**: son medidas respecto a una referencia
// declarada. Una herramienta que dijera "paralelismo = 0,08" sin decir *paralelo
// a qué* estaría inventándose un número con nombre de norma.
//
// Va en **coordenadas de pieza**, como todo lo demás: así la referencia sigue a
// la pieza cuando esta se mueve o gira, igual que la herramienta que la usa.
enum class DerivedKind {
    None,    // la herramienta no produce ningún elemento
    Point,   // un punto: centro, intersección, punto medio
    Line,    // una recta: `point` + `direction` (unitaria)
    Circle,  // `point` = centro, `radius`
};

struct DerivedElement {
    DerivedKind kind = DerivedKind::None;
    cv::Point2f point{0.0F, 0.0F};
    cv::Point2f direction{1.0F, 0.0F};
    double radius = 0.0;

    [[nodiscard]] bool valid() const { return kind != DerivedKind::None; }
};

// Elementos disponibles como referencia, indexados por el NOMBRE de la
// herramienta que los produjo. Por nombre y no por id porque el operador
// referencia lo que ve escrito en la lista, y porque una plantilla exportada e
// importada en otra pieza cambia de ids pero conserva los nombres.
using DerivedElements = std::map<std::string, DerivedElement>;

}  // namespace pci::inspection
