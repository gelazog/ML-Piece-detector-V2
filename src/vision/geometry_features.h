#pragma once

#include <opencv2/core.hpp>

#include <string>
#include <vector>
#include "core/csv_dialect.h"

namespace pci::vision {

// Convierte "una silueta" en "dos caras paralelas, cuatro esquinas redondeadas
// y tres agujeros". Es el paso que hace falta antes de poder proponer medidas
// automáticamente: el contorno que entrega `analyzeFrame` es una lista de
// puntos, y de una lista de puntos no se deduce qué medir.

enum class PrimitiveKind { Line, Arc };

struct ContourPrimitive {
    PrimitiveKind kind = PrimitiveKind::Line;
    cv::Point2f start{0.0F, 0.0F};
    cv::Point2f end{0.0F, 0.0F};
    cv::Point2f mid{0.0F, 0.0F};  // punto intermedio del tramo, sobre la forma
    // Solo para Arc.
    cv::Point2f center{0.0F, 0.0F};
    double radius = 0.0;
    double sweepDeg = 0.0;
    // Calidad del ajuste (px) y longitud recorrida del tramo (px). La longitud
    // sirve para ordenar por importancia: un rasgo de 3 px no merece una
    // herramienta.
    double rmsResidual = 0.0;
    double length = 0.0;
};

struct DecomposeOptions {
    // Paso del remuestreo uniforme. El contorno crudo va de píxel en píxel y
    // trae el dentado de la rasterización; remuestrear lo suaviza sin borrar la
    // forma.
    double resampleStep = 2.0;
    // Residuo máximo aceptable para dar un tramo por bueno (px).
    double maxResidual = 0.8;
    // Un tramo con menos puntos que esto no se parte más: por debajo, cualquier
    // recta corta se ajusta igual de bien a un arco enorme.
    int minPoints = 8;
    // Ángulo mínimo de arco para aceptarlo como tal. Por debajo, un arco de
    // radio gigantesco explica los puntos igual que una recta, y devolver ese
    // radio sería inventárselo.
    double minArcSweepDeg = 15.0;
};

// Descompone un contorno CERRADO en tramos rectos y arcos.
//
// El método es partir-y-unir recursivo, y no detección de esquinas, por una
// razón concreta: en la unión tangente de una recta con un redondeo **no hay
// esquina** —la dirección no cambia, solo la curvatura—, así que un detector de
// esquinas se salta justo las transiciones de una pieza mecanizada. Partir por
// el punto de peor ajuste sí las encuentra.
[[nodiscard]] std::vector<ContourPrimitive> decomposeContour(
    const std::vector<cv::Point>& contour, const DecomposeOptions& options = {});

// Contornos internos de la máscara: los agujeros de la pieza. Cada uno es
// candidato a un Círculo si es redondo. `minAreaPx` descarta el ruido de la
// segmentación.
[[nodiscard]] std::vector<std::vector<cv::Point>> findHoles(const cv::Mat& mask,
                                                            double minAreaPx = 40.0);

// Remuestrea un contorno cerrado a paso uniforme. Se expone porque es útil por
// separado (dibujar, exportar) y porque así se puede probar aparte.
[[nodiscard]] std::vector<cv::Point2f> resampleClosedContour(
    const std::vector<cv::Point>& contour, double step);

// Todo lo que se puede decir del contorno de una pieza de un vistazo: la forma
// en sí, en qué se descompone, y los cuatro números que el operador mira antes
// de medir nada (perímetro, área, agujeros, envolvente).
//
// Es una sola estructura y no cuatro funciones sueltas porque las partes tienen
// que ser COHERENTES entre sí: el área descuenta estos agujeros y no otros, y la
// descomposición describe este contorno exterior. Calculadas por separado se
// podrían mezclar resultados de dos segmentaciones distintas.
struct ContourReport {
    // Coordenadas de IMAGEN (px). Es lo que devuelve la segmentación; pasarlo a
    // coordenadas de pieza es cosa de quien lo dibuje.
    std::vector<cv::Point> outer;
    std::vector<std::vector<cv::Point>> holes;
    std::vector<ContourPrimitive> primitives;  // descomposición del exterior

    double perimeter = 0.0;  // px, contorno exterior cerrado
    double area = 0.0;       // px², con los agujeros ya descontados
    cv::Rect bounds;         // envolvente recta, en px de imagen
    cv::RotatedRect minRect;  // envolvente girada (largo × ancho reales)
    bool valid = false;
};

// Describe la pieza más grande de la máscara. Sin pieza -> `valid == false`.
[[nodiscard]] ContourReport describeContour(const cv::Mat& mask,
                                            const DecomposeOptions& options = {});

// Perímetro de un contorno digital, estimado con Vossepoel–Smeulders:
// `0,980·Ne + 1,406·No − 0,091·Nc`, donde Ne son los pasos rectos, No los
// diagonales y Nc los cambios de dirección.
//
// **No es `cv::arcLength`, y la diferencia importa.** La longitud de cadena que
// devuelve `arcLength` mide el polígono que pasa por los centros de los píxeles
// del borde, y eso la hace depender de CÓMO ESTÉ GIRADA LA PIEZA. Medido sobre
// figuras sintéticas de perímetro conocido:
//
// | figura                | arcLength | Kulpa (×0,948) | Vossepoel–Smeulders |
// |-----------------------|-----------|----------------|---------------------|
// | círculo (r 15..240)   | +4,9…5,4 %| −0,5…−0,06 %   | −1,4…−0,12 %        |
// | cuadrado alineado     |   0,00 %  | −5,19 %        | −2,3…−2,0 %         |
// | el mismo a 45°        | +0,41 %   | −4,80 %        | −0,29 %             |
// | el mismo a 30°        | **+7,59 %**| +2,00 %       | −0,33 %             |
//
// El número que condena a `arcLength` no es el sesgo del círculo sino ese
// **+7,59 %**: el MISMO cuadrado leído un 7,6 % más largo solo por estar girado
// 30° respecto a la cámara. Una medida de inspección no puede depender de cómo
// se haya posado la pieza. Kulpa arregla el círculo y rompe el cuadrado; V–S
// deja el error por debajo del **2,3 %** en todos los casos. Barriendo el mismo
// cuadrado por 0°, 15°, 30°, 45° y 60°, la lectura de la cadena se dispersa
// **8,01 puntos** y la del estimador **2,79** — 2,9 veces menos.
//
// El precio, dicho claro: en un borde recto alineado con los ejes `arcLength`
// era EXACTO y V–S se queda un 2 % corto. Se acepta porque una pieza real no
// llega siempre alineada, y un error acotado en todas las orientaciones vale
// más que uno perfecto en una sola.
//
// Necesita un contorno de cadena de 8 vecinos (`CHAIN_APPROX_NONE`): con pasos
// de más de un píxel el conteo no significa nada, y entonces **cae a
// `cv::arcLength`** en vez de devolver un número inventado.
[[nodiscard]] double digitalPerimeter(const std::vector<cv::Point>& contour);

// Perfil de línea contra un nominal (G7): cuánto se separa el contorno medido
// del que debería tener.
//
// Es la tolerancia GD&T **más honesta que existe para una silueta**, porque
// está definida sobre un elemento lineal y no sobre una superficie: lo que se
// mide en la imagen es exactamente lo que la cota describe.
//
// **No hace falta ICP.** El plan lo pedía para alinear medida y nominal, pero
// este programa ya resuelve la alineación rígida antes de medir nada: el
// Position Fixture. Si los dos contornos están en coordenadas de PIEZA, ya
// están alineados, y meter un ICP encima sería alinear dos veces — y peor,
// dejaría que el ajuste se comiera una desviación real girando el nominal para
// que encajara.
//
// El signo dice de qué lado: positivo = el material sobresale del nominal,
// negativo = falta. La zona bilateral simétrica de la norma es `2·max|d|`.
struct ProfileDeviation {
    double worstOutside = 0.0;  // lo que más sobresale (px)
    double worstInside = 0.0;   // lo que más falta (px, positivo)
    double zoneWidth = 0.0;     // 2·max(|fuera|, |dentro|): la cota bilateral
    cv::Point2f worstAt{0.0F, 0.0F};  // dónde ocurre lo peor
    int comparedPoints = 0;
    bool valid = false;
};

// `measured` y `nominal` tienen que estar en el MISMO sistema (coordenadas de
// pieza). `nominal` se trata como polilínea cerrada.
[[nodiscard]] ProfileDeviation profileDeviation(const std::vector<cv::Point2f>& measured,
                                                const std::vector<cv::Point2f>& nominal);

// El contorno en CSV, para llevárselo a un CAD.
//
// `mmPerPixel > 0` exporta en mm; si no, en px. La unidad va en el NOMBRE de la
// columna (`x_mm` / `x_px`) en vez de en una línea de comentario porque los
// importadores de CAD y las hojas de cálculo tragan cabeceras pero no
// comentarios, y un archivo de coordenadas sin unidad es papel mojado.
// `dialect`: el formato que la hoja de cálculo del equipo sabe abrir. Ver
// `core/csv_dialect.h`.
[[nodiscard]] std::string contourToCsv(
    const ContourReport& report, double mmPerPixel = 0.0,
    const core::CsvDialect& dialect = core::systemCsvDialect());

}  // namespace pci::vision
