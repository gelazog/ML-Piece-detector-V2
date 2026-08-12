#pragma once

#include <opencv2/core.hpp>

#include <array>
#include <string>
#include <variant>
#include <vector>

#include "core/result.h"
#include "inspection_editor/tools/tool_types.h"

namespace pci::inspection {

// Falso, pero dependiente del tipo: permite cerrar una cadena de `if constexpr`
// sobre la variante con un `static_assert` que solo salta cuando de verdad se
// instancia esa rama. Es lo que convierte "se me olvidó tratar el tipo nuevo"
// en un error de compilación con nombre, en vez de una herramienta muda.
template <typename T>
inline constexpr bool alwaysFalse = false;

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

// --- Construcciones geométricas (X1) ----------------------------------------
//
// Un punto y una recta que no se miden: se CALCULAN a partir de los elementos
// derivados de otras herramientas. Existen para poder declarar un datum, que es
// lo que le falta al GD&T para poder medir contra algo que no sea un supuesto.
//
// Su geometría no lleva los operandos —esos son `reference` y `reference2`—
// sino solo qué construcción es y dónde se ancla su etiqueta en el lienzo. El
// ancla no entra en ningún cálculo: es únicamente el sitio donde el operador
// puede pinchar para seleccionar la herramienta y donde se escribe el
// resultado, porque un punto calculado puede caer fuera de la imagen y
// entonces no habría dónde hacer clic.

enum class PointConstruction {
    Midpoint,      // punto medio entre dos elementos con punto
    Intersection,  // corte de dos rectas
    Projection,    // pie de la perpendicular de un punto sobre una recta
    CircleCenter,  // centro del círculo ajustado por otra herramienta
};

enum class LineConstruction {
    ThroughTwoPoints,     // recta por dos elementos con punto
    Bisector,             // bisectriz de dos rectas; si son paralelas, la recta media
    ParallelThrough,      // paralela a una recta por un punto
    PerpendicularThrough, // perpendicular a una recta por un punto
};

struct ConstructedPointGeometry {
    PointConstruction mode = PointConstruction::Midpoint;
    cv::Point2f anchor;  // solo etiqueta y selección; no entra en el cálculo
};

struct ConstructedLineGeometry {
    LineConstruction mode = LineConstruction::ThroughTwoPoints;
    cv::Point2f anchor;
};

// Eje medio de la silueta: la línea que va por el centro de una pieza alargada,
// a media distancia entre sus dos flancos.
//
// Es el datum natural de una pieza torneada, y el sustituto honesto de la
// simetría que ASME Y14.5-2018 retiró. A diferencia de las dos construcciones de
// arriba, esta **sí mira la imagen**: los flancos hay que encontrarlos. Pero lo
// que produce es lo mismo —una recta para referenciar— y por eso vive en la
// misma familia.
//
// Se traza igual que el Eje torneado, con el mismo gesto y la misma geometría,
// porque es la misma exploración: en cada estación se buscan los dos bordes y
// se toma su punto medio. La diferencia está en qué se hace con ellos.
struct MedianAxisGeometry {
    cv::Point2f axisFrom;
    cv::Point2f axisTo;
    float searchBand = 60.0F;  // hasta dónde buscar el flanco a cada lado
    int stations = 32;         // cortes repartidos a lo largo del eje
};

// --- Región: los descriptores de forma de una silueta (F1) ------------------
//
// Una sola herramienta con un SELECTOR DE MEDIDA, como Posición tiene su
// selector de eje, y no seis herramientas. El motivo es práctico: cada
// instancia lleva su propia tolerancia, así que el operador pone dos Regiones
// —una que vigila el área y otra los agujeros— y deja fuera las cuatro que no
// le importan. Con seis herramientas tendría que borrarlas.
enum class RegionMeasure {
    Area,         // px² (o mm² con escala), con los agujeros descontados
    Perimeter,    // px del contorno exterior
    Solidity,     // área / área del casco convexo: 1 = sin entrantes
    Circularity,  // 4πA/P²: 1 = círculo, 0,785 = cuadrado
    AspectRatio,  // largo/ancho del rectángulo mínimo, siempre >= 1
    HoleCount,    // cuántos agujeros cerrados tiene dentro
};

struct RegionGeometry {
    cv::Point2f center;  // rectángulo alineado a los ejes de la PIEZA
    float width = 160.0F;
    float height = 120.0F;
    RegionMeasure measure = RegionMeasure::Area;
    bool darkPiece = true;  // la pieza es lo oscuro (o lo claro si false)
};

// Simetría de la silueta (F2). Es un DESCRIPTOR DE FORMA, no una tolerancia
// GD&T: la simetría de la norma se retiró en ASME Y14.5-2018 y darla con ese
// nombre sería vender como cota algo que ya no lo es.
//
// Para lo que sirve de verdad es para lo que se pidió: detectar una pieza
// montada del revés, o con un rasgo asimétrico que no debería estar.
struct SymmetryGeometry {
    cv::Point2f center;  // rectángulo alineado a los ejes de la pieza
    float width = 160.0F;
    float height = 120.0F;
    bool darkPiece = true;
};

// Lados de un perfil poligonal (F3): cuántos, cuánto miden y qué ángulo
// forman. El caso de uso obvio es el hexágono de una tuerca.
//
// Todo lo decide `epsilon`, la tolerancia con la que se simplifica el contorno,
// y por eso se guarda como **fracción del perímetro** y no en píxeles: en
// píxeles, la misma pieza vista desde más lejos daría otro número de lados, y
// una plantilla dejaría de valer al cambiar la distancia o la resolución.
struct PolygonGeometry {
    cv::Point2f center;  // rectángulo alineado a los ejes de la pieza
    float width = 160.0F;
    float height = 120.0F;
    // Fracción del perímetro. 0,02 (2 %) va bien de 3 a 10 lados; subirlo
    // simplifica más (menos lados) y bajarlo conserva detalle (más lados).
    float epsilonFraction = 0.02F;
    bool darkPiece = true;
};

// Rebabas y mellas de un borde (F4). Distinta del Borde liso, que devuelve UNA
// desviación máxima: aquí los defectos se detectan, se cuentan y se miden por
// separado.
//
// La diferencia importa: un borde con una mella de 0,5 mm y otro con veinte de
// 0,1 mm dan la misma lectura con el Borde liso y no son la misma pieza.
struct EdgeDefectsGeometry {
    cv::Point2f p0;  // tramo del borde a vigilar
    cv::Point2f p1;
    float scanLength = 16.0F;  // largo de cada escaneo perpendicular
    // Más escaneos que el Borde liso a propósito: aquí hay que RESOLVER los
    // eventos uno por uno, no quedarse con el máximo, y dos defectos separados
    // por menos de un escaneo se leerían como uno solo.
    int scanCount = 60;
    // A partir de qué altura una desviación cuenta como defecto (px). Es el
    // parámetro que define la herramienta: "cuántos defectos de más de esto
    // hay", que es una pregunta con respuesta, a diferencia de "cuántos
    // defectos hay".
    float minHeight = 1.5F;
    bool darkPiece = true;  // la pieza es lo oscuro (decide rebaba vs mella)
};

// Holgura: la separación MÁS CORTA entre dos figuras (L1).
//
// No es lo que da un calíper. El calíper mide donde el operador cruzó, y en una
// pieza el sitio donde algo está más apretado casi nunca es donde uno pone la
// línea. Aquí se busca el mínimo de verdad y se dice DÓNDE está, porque un
// mínimo que no se puede señalar en el lienzo no se puede verificar a ojo.
//
// Se dibuja UN recuadro que abarque las dos figuras y se miden las dos mayores
// que haya dentro. Es un recuadro y no dos porque el gesto de "mide la holgura
// de aquí" es uno solo; si dentro hay más de dos figuras, las pequeñas se
// ignoran y el detalle dice cuántas se vieron.
struct ClearanceGeometry {
    cv::Point2f center;  // rectángulo alineado a los ejes de la pieza
    float width = 200.0F;
    float height = 160.0F;
    bool darkPiece = true;
};

// Rectitud por ZONA MÍNIMA (G1), que es el valor de la norma: la anchura de la
// banda más estrecha de dos rectas paralelas que contiene todos los puntos del
// borde.
//
// No es lo que da el Borde liso. Aquel devuelve la desviación máxima respecto a
// la recta de mínimos cuadrados, que es otro número y siempre sale menor que la
// banda mínima (la banda de mínimos cuadrados es una candidata más entre todas
// las orientaciones, así que nunca puede ganar a la mejor).
struct StraightnessGeometry {
    cv::Point2f p0;  // tramo del borde cuya rectitud se juzga
    cv::Point2f p1;
    float scanLength = 16.0F;  // largo de cada escaneo perpendicular
    int scanCount = 60;
};

[[nodiscard]] const std::array<RegionMeasure, 6>& allRegionMeasures();
[[nodiscard]] const char* regionMeasureLabel(RegionMeasure measure);

using ToolGeometry = std::variant<CaliperGeometry, CircleGeometry, PointToLineGeometry,
                                  EdgeFlawGeometry, BlobGeometry, RulerGeometry,
                                  LineToLineGeometry, AngleGeometry, PolyBlobGeometry,
                                  PositionGeometry, ArcGeometry, ShaftGeometry,
                                  ThreadGeometry, GearGeometry, ConstructedPointGeometry,
                                  ConstructedLineGeometry, MedianAxisGeometry,
                                  RegionGeometry, SymmetryGeometry, PolygonGeometry,
                                  EdgeDefectsGeometry, ClearanceGeometry,
                                  StraightnessGeometry>;

// Nombres de las construcciones para la interfaz y para el JSON. Igual que con
// las herramientas, una sola lista: el desplegable del panel y el fichero de
// plantilla tienen que decir lo mismo.
[[nodiscard]] const std::array<PointConstruction, 4>& allPointConstructions();
[[nodiscard]] const std::array<LineConstruction, 4>& allLineConstructions();
[[nodiscard]] const char* constructionLabel(PointConstruction mode);
[[nodiscard]] const char* constructionLabel(LineConstruction mode);
// Qué hace falta en `reference` y `reference2` para cada construcción. Lo
// consulta el ejecutor para dar un motivo concreto cuando falta algo, y lo
// consultará el panel para etiquetar los dos desplegables.
enum class OperandKind { Point, Line, Circle, Unused };
[[nodiscard]] std::array<OperandKind, 2> operandsOf(PointConstruction mode);
[[nodiscard]] std::array<OperandKind, 2> operandsOf(LineConstruction mode);
[[nodiscard]] const char* operandKindLabel(OperandKind kind);

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
[[nodiscard]] const std::array<ToolType, 23>& allToolTypes();

// Familias de herramientas. Son un DATO, no el orden en que se pintan los
// botones: viven aquí, junto a `allToolTypes()`, por la misma razón por la que
// esa lista existe — una sola fuente de verdad, o las dos superficies de
// interfaz (la fila de la vista en vivo y la columna del editor) acabarían
// agrupando distinto.
//
// «Construcciones geométricas» nace vacía y se llena en `X`. No es un hueco por
// simetría: sin construcciones no se puede declarar un datum, y sin datum no
// hay GD&T.
enum class ToolCategory {
    BasicShape,         // forma, región, presencia y conteo
    InLine,             // la cota directa: distancias, diámetros, ángulos
    Construction,       // elementos derivados que otras herramientas referencian
    Gdt,                // tolerancias geométricas, siempre contra un datum
    TurnedAndExtremes,  // máximos y mínimos, y piezas de torno
};

[[nodiscard]] const std::array<ToolCategory, 5>& allToolCategories();
[[nodiscard]] ToolCategory categoryOf(ToolType type);
[[nodiscard]] const char* categoryLabel(ToolCategory category);
[[nodiscard]] const char* categoryDescription(ToolCategory category);
// Las herramientas de una familia, en el orden de `allToolTypes()`.
[[nodiscard]] std::vector<ToolType> toolsInCategory(ToolCategory category);

// Traslada in situ todos los puntos de la geometría (coords de pieza). Útil
// para mover o duplicar una herramienta con un pequeño desplazamiento.
void translateGeometry(ToolGeometry& geometry, const cv::Point2f& delta);

// Nombre corto de la herramienta para botones y nombres por defecto (UTF-8, en
// español). Vive aquí y no en cada ventana porque el editor y la vista en vivo
// tenían su propia copia idéntica, y dos copias es una divergencia esperando:
// la misma herramienta acabaría llamándose distinto en cada pantalla.
const char* toolTypeLabel(ToolType type);

// Las referencias de una herramienta viajan dentro de `paramsJson` (columna
// `params`, que existía sin usarse). Estas dos funciones son el único sitio que
// conoce ese formato, para que la base de datos y la exportación de plantillas
// no lo escriban cada una a su manera.
struct ToolReferences {
    std::string first;
    std::string second;
};
[[nodiscard]] std::string paramsWithReferences(const ToolReferences& references);
[[nodiscard]] ToolReferences referencesFromParams(const std::string& paramsJson);

// Descripción de uso para tooltips/ayuda (UTF-8, en español): qué mide la
// herramienta y cómo dibujarla.
const char* toolTypeDescription(ToolType type);

// Tolerancias sugeridas a partir de una primera medición sobre la pieza
// buena: banda de ±10% para distancias/diámetros, conteo exacto para blobs y
// techo holgado para la desviación de borde.
// Si lo que mide esta herramienta es una FRACCION entre 0 y 1 (un grado, una
// proporcion) y no una magnitud con unidades. Lo consulta `suggestTolerances`
// para recortar el techo en 1 —un grado mayor que 1 no existe, y dejar la banda
// abierta por arriba haria pasar por bueno un valor imposible— y lo consultan
// los barridos de coherencia para no probar con valores que la herramienta no
// puede dar. Vive aqui, en una sola funcion, porque tenerlo escrito en los dos
// sitios acabaria divergiendo.
[[nodiscard]] bool measuresFraction(ToolType type);

void suggestTolerances(ToolType type, double measured, double& toleranceMin,
                       double& toleranceMax);

// La misma sugerencia, pero mirando la geometría. Hace falta porque la Región
// mide seis cosas distintas con el mismo tipo: una banda de ±10 % vale para un
// área y no vale para una circularidad (que vive entre 0 y 1) ni para un
// recuento de agujeros (que es exacto). Para todo lo demás **delega** en la
// versión por tipo, así que sigue habiendo una sola regla por herramienta.
void suggestTolerances(const ToolGeometry& geometry, double measured, double& toleranceMin,
                       double& toleranceMax);

}  // namespace pci::inspection
