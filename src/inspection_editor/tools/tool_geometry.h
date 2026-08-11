#pragma once

#include <opencv2/core.hpp>

#include <array>
#include <string>
#include <variant>
#include <vector>

#include "core/result.h"
#include "inspection_editor/tools/tool_types.h"

namespace pci::inspection {

// Geometrías tipadas por herramienta, en coordenadas de PIEZA (píxeles).
// Las distancias se conservan al pasar a coordenadas de imagen porque el
// fixture es rotación + traslación pura (sin escala).

struct CaliperGeometry {
    cv::Point2f p0;
    cv::Point2f p1;
    float bandWidth = 10.0F;  // grosor perpendicular promediado del perfil
};

struct CircleGeometry {
    cv::Point2f center;
    float radius = 50.0F;
    float searchBand = 12.0F;  // banda radial de búsqueda del borde
    int rayCount = 36;         // puntos de escaneo alrededor del círculo
};

struct PointToLineGeometry {
    cv::Point2f lineA;
    cv::Point2f lineB;
    cv::Point2f scanA;  // segmento que localiza el punto (borde más fuerte)
    cv::Point2f scanB;
};

struct EdgeFlawGeometry {
    cv::Point2f p0;  // tramo del borde que debería ser recto
    cv::Point2f p1;
    float scanLength = 16.0F;  // largo de cada escaneo perpendicular
    int scanCount = 20;
};

struct BlobGeometry {
    cv::Point2f center;  // rectángulo alineado a los ejes de la pieza
    float width = 80.0F;
    float height = 60.0F;
    float minArea = 20.0F;
    bool darkBlobs = true;  // buscar manchas oscuras (o claras si false)
};

struct RulerGeometry {
    cv::Point2f p0;  // distancia directa entre dos puntos (sin buscar bordes)
    cv::Point2f p1;
};

struct LineToLineGeometry {
    cv::Point2f a0;  // línea de referencia A
    cv::Point2f a1;
    cv::Point2f b0;  // línea de referencia B
    cv::Point2f b1;
};

struct AngleGeometry {
    cv::Point2f vertex;  // esquina cuyo ángulo se mide
    cv::Point2f end0;    // extremo del primer lado
    cv::Point2f end1;    // extremo del segundo lado
};

struct PolyBlobGeometry {
    std::vector<cv::Point2f> vertices;  // región poligonal libre (>= 3 vértices)
    float minArea = 20.0F;
    bool darkBlobs = true;  // buscar manchas oscuras (o claras si false)
};

// Eje sobre el que se juzga la desviación de una herramienta Posición.
enum class PositionAxis { Radial, X, Y };

struct PositionGeometry {
    cv::Point2f point;  // rasgo marcado, en coords de pieza (viaja con ella)
    PositionAxis axis = PositionAxis::Radial;
};

// Arco: el radio de una esquina o de un redondeo, que es una medida de plano
// tan corriente como un diámetro y no se puede sacar con el Círculo — este pide
// centro y contorno cerrado, y en una esquina no hay ni uno ni otro. Se define
// por tres puntos SOBRE el arco (los dos extremos y uno intermedio), que es
// como se mide con una plantilla de radios.
struct ArcGeometry {
    cv::Point2f start;
    cv::Point2f mid;  // punto intermedio: fija por dónde va el arco
    cv::Point2f end;
    float searchBand = 12.0F;  // banda radial de búsqueda del borde
    int rayCount = 24;         // puntos de escaneo repartidos por el sector
};

// Eje torneado: una pieza de torno vista de perfil son dos bordes casi
// paralelos. Se traza el EJE por el medio y la herramienta explora a los dos
// lados a lo largo de él.
//
// No es un preset del Calíper: un calíper mide en un punto, y en un punto no se
// distingue un cilindro de un cono. Aquí se mide a lo largo, que es lo que
// permite dar de una vez diámetro, conicidad y rectitud — los tres números con
// los que se acepta una pieza al salir del torno.
struct ShaftGeometry {
    cv::Point2f axisFrom;
    cv::Point2f axisTo;
    float searchBand = 60.0F;  // hasta dónde buscar el borde a cada lado
    int stations = 32;         // cortes repartidos a lo largo del eje
};

// Rosca vista de perfil. Se traza el eje igual que en el Eje torneado -misma
// geometria, mismo gesto- y lo que cambia es la lectura: el perfil de una rosca
// a lo largo del eje es una senal PERIODICA, y de su periodo sale el paso.
struct ThreadGeometry {
    cv::Point2f axisFrom;
    cv::Point2f axisTo;
    float searchBand = 60.0F;
    // Hacen falta bastantes cortes por vuelta de rosca para medir bien el
    // periodo: con pocos, el paso se redondea al muestreo.
    int stations = 240;
};

// Engranaje visto DE CARA. El perfil radial desde el centro se repite una vez
// por diente, asi que el numero de dientes sale del mismo calculo de periodo
// que el paso de una rosca -en modo circular, porque una vuelta cierra-.
struct GearGeometry {
    cv::Point2f center;
    float innerRadius = 40.0F;  // por dentro de la raiz de los dientes
    float outerRadius = 90.0F;  // por fuera de la cabeza
    int rayCount = 1440;        // muchos rayos: hacen falta varios por diente
};

using ToolGeometry = std::variant<CaliperGeometry, CircleGeometry, PointToLineGeometry,
                                  EdgeFlawGeometry, BlobGeometry, RulerGeometry,
                                  LineToLineGeometry, AngleGeometry, PolyBlobGeometry,
                                  PositionGeometry, ArcGeometry, ShaftGeometry,
                                  ThreadGeometry, GearGeometry>;

// (De)serialización JSON (cv::FileStorage en memoria). El tipo del JSON debe
// coincidir con config.type al parsear.
std::string toJson(const ToolGeometry& geometry);
core::Result<ToolGeometry> geometryFromJson(ToolType type, const std::string& json);

ToolType typeOf(const ToolGeometry& geometry);

// Todos los tipos de herramienta, en el orden en que se ofrecen al operador.
//
// Hay UNA sola lista porque el precio de tenerla repetida ya se pagó: las cuatro
// herramientas de pieza torneada se añadieron al editor y se quedaron fuera de
// la fila "Dibujar" de la vista en vivo, donde nadie las echó de menos hasta el
// repaso de coherencia. Quien añada la decimoquinta la pone aquí y aparece en
// todas partes; las pruebas de coherencia recorren esta misma lista.
[[nodiscard]] const std::array<ToolType, 14>& allToolTypes();

// Traslada in situ todos los puntos de la geometría (coords de pieza). Útil
// para mover o duplicar una herramienta con un pequeño desplazamiento.
void translateGeometry(ToolGeometry& geometry, const cv::Point2f& delta);

// Nombre corto de la herramienta para botones y nombres por defecto (UTF-8, en
// español). Vive aquí y no en cada ventana porque el editor y la vista en vivo
// tenían su propia copia idéntica, y dos copias es una divergencia esperando:
// la misma herramienta acabaría llamándose distinto en cada pantalla.
const char* toolTypeLabel(ToolType type);

// Descripción de uso para tooltips/ayuda (UTF-8, en español): qué mide la
// herramienta y cómo dibujarla.
const char* toolTypeDescription(ToolType type);

// Tolerancias sugeridas a partir de una primera medición sobre la pieza
// buena: banda de ±10% para distancias/diámetros, conteo exacto para blobs y
// techo holgado para la desviación de borde.
void suggestTolerances(ToolType type, double measured, double& toleranceMin,
                       double& toleranceMax);

}  // namespace pci::inspection
