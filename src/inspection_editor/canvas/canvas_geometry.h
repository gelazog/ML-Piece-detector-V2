#pragma once

#include <opencv2/core.hpp>

#include <vector>

#include "inspection_editor/tools/tool_geometry.h"
#include "vision/types.h"

namespace pci::inspection {

// Geometría del trazado, extraída del widget para poder probarla: aquí vive lo
// que decide DÓNDE cae cada cosa en pantalla y QUÉ herramienta estás tocando.
// Es la parte del lienzo donde un error se nota en cada clic y, hasta ahora,
// la única que no tenía red de pruebas.
//
// Sin Qt: se trabaja con rectángulos y puntos propios para que los tests no
// necesiten una ventana.

struct ViewRect {
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;

    [[nodiscard]] bool empty() const { return width <= 0.0 || height <= 0.0; }
    [[nodiscard]] double left() const { return x; }
    [[nodiscard]] double top() const { return y; }
    [[nodiscard]] double right() const { return x + width; }
    [[nodiscard]] double bottom() const { return y + height; }
    [[nodiscard]] double centerX() const { return x + width / 2.0; }
    [[nodiscard]] double centerY() const { return y + height / 2.0; }

    // Solapamiento estricto: tocarse por el borde no cuenta.
    [[nodiscard]] bool intersects(const ViewRect& other) const {
        return left() < other.right() && other.left() < right() && top() < other.bottom() &&
               other.top() < bottom();
    }
    [[nodiscard]] bool containedIn(const ViewRect& bounds) const {
        return left() >= bounds.left() && right() <= bounds.right() &&
               top() >= bounds.top() && bottom() <= bounds.bottom();
    }
};

// Transformación de la vista: imagen ajustada al widget, con zoom y
// desplazamiento. Es el ÚNICO camino entre píxeles de pantalla y píxeles de
// imagen, así que de su corrección depende que dibujar, medir y pintar caigan
// en el mismo sitio.
class ViewTransform {
public:
    ViewTransform(cv::Size imageSize, cv::Size widgetSize, double zoom, cv::Point2d pan);

    // Encuadre base: la imagen ajustada al widget conservando la proporción.
    [[nodiscard]] ViewRect fitRect() const;
    // Encuadre efectivo = ajuste × zoom, desplazado y con el límite aplicado.
    [[nodiscard]] ViewRect targetRect() const;
    // Limita el desplazamiento para que no se descubra fondo; si con el zoom
    // actual la imagen cabe en un eje, en ese eje queda centrada.
    [[nodiscard]] cv::Point2d clampedPan(const cv::Point2d& pan) const;

    [[nodiscard]] cv::Point2d imageToWidget(const cv::Point2f& imagePoint) const;
    [[nodiscard]] cv::Point2f widgetToImage(const cv::Point2d& widgetPoint) const;

    // Píxeles de pantalla por píxel de imagen (1.0 = 100 %).
    [[nodiscard]] double displayScale() const;

private:
    cv::Size imageSize_;
    cv::Size widgetSize_;
    double zoom_ = 1.0;
    cv::Point2d pan_{0.0, 0.0};
};

// Distancia de un punto al segmento a-b (no a la recta infinita).
[[nodiscard]] double distanceToSegment(const cv::Point2f& p, const cv::Point2f& a,
                                       const cv::Point2f& b);

// Puntos representativos de una geometría (coords de pieza), para el marco de
// selección múltiple.
[[nodiscard]] std::vector<cv::Point2f> referencePoints(const ToolGeometry& geometry);

// Puntos-manija editables, en orden fijo. Casos especiales: la 2ª manija del
// círculo es el radio; la 2ª del blob redimensiona el rectángulo.
[[nodiscard]] std::vector<cv::Point2f> handlePoints(const ToolGeometry& geometry);

// Reposiciona una sola manija manteniendo la geometría coherente (radios y
// tamaños mínimos).
void setHandlePoint(ToolGeometry& geometry, int handle, const cv::Point2f& q);

// Distancia de un punto de IMAGEN a la geometría de una herramienta, ya llevada
// a coordenadas de imagen por el fixture. Es lo que decide qué herramienta
// selecciona un clic.
[[nodiscard]] double distanceToGeometry(const ToolGeometry& geometry,
                                        const vision::Fixture& fixture,
                                        const cv::Point2f& imagePoint);

// Zona de clic: el operador apunta con el ratón, así que la tolerancia se
// decide en píxeles de PANTALLA y se traduce a píxeles de imagen dividiendo por
// la escala de la vista. Si se fijara en píxeles de imagen (como se hacía), la
// zona de agarre crecería con el zoom: al 800 % una manija de 7 px dibujados se
// agarraba desde ~70 px de distancia, así que un clic en un sitio vacío
// deformaba la herramienta; y con una imagen grande en una ventana pequeña
// pasaba lo contrario, la manija que se ve no se podía agarrar.
[[nodiscard]] double pickTolerance(double screenPixels, double displayScale);

// Manija más cercana al punto de imagen dentro de la tolerancia, o -1. El
// índice corresponde al orden de handlePoints.
[[nodiscard]] int pickHandle(const ToolGeometry& geometry, const vision::Fixture& fixture,
                             const cv::Point2f& imagePoint, double tolerance);

// Sitio para la etiqueta de una medida: se busca el más cercano a `preferred`
// que no pise ninguna de las ya colocadas, probando primero hacia abajo y luego
// hacia arriba, y SIEMPRE dentro de `bounds`.
//
// Lo de quedarse dentro no es un detalle: la versión anterior solo empujaba
// hacia abajo y no miraba el borde, así que una medida anclada en la parte baja
// del lienzo se empujaba fuera de la vista y el operador no veía ninguna
// lectura. Más vale una etiqueta visible que se solape con otra que una
// etiqueta perfectamente colocada donde nadie la ve; si no hay hueco, se
// devuelve la posición preferida metida dentro del área.
[[nodiscard]] ViewRect placeLabel(const ViewRect& preferred,
                                  const std::vector<ViewRect>& taken,
                                  const ViewRect& bounds);

}  // namespace pci::inspection
