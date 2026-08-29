#pragma once

#include <opencv2/core.hpp>

#include <string>
#include <vector>

#include "inspection_editor/auto_measure.h"
#include "inspection_editor/execution/measurement_report.h"
#include "vision/shape_class.h"
#include "vision/types.h"

namespace pci::inspection {

// **Medir la pieza entera de un tirón**, a partir de su contorno y de nada más.
//
// Todo lo que hace falta existía suelto: `describeContour` saca el perímetro, el
// área y los agujeros; `classifyShape` dice qué figura es; `proposeTools` deduce
// qué cotas tienen sentido para esa figura. Lo que no existía era **quien lo
// juntara y lo enseñara como una sola respuesta**, así que para saber cuánto
// mide una pieza había que abrir el editor, pulsar «Medir automáticamente»,
// revisar una lista de propuestas y aceptarlas como herramientas. Eso está bien
// para PREPARAR una plantilla y es un rodeo absurdo para la pregunta que más se
// hace delante de una pieza: *¿cuánto mide esto?*
//
// La diferencia con `proposeTools` no es el cálculo —es el mismo— sino a qué
// pregunta responde. Aquel construye una lista corta y REVISABLE para vigilar la
// pieza en producción, y por eso se corta en doce. Este es un INFORME: se quiere
// entero, se lee de una vez y se exporta. Cortarlo sería contestar a medias.
//
// Se conservan las propuestas junto a las filas porque las dos cosas hacen
// falta: las filas para leer y exportar, y las propuestas para el paso
// siguiente —convertirlas en herramientas vigiladas— sin tener que volver a
// medir la pieza.

struct PieceReport {
    bool ok = false;
    // Por qué no se pudo, cuando no se pudo. Un informe vacío sin motivo se
    // confunde con una pieza sin cotas, que es lo contrario de lo que pasa.
    std::string problem;

    vision::ShapeClass shape;
    // Qué es la pieza, en una frase para el título: «Hexágono de 6 lados».
    std::string headline;

    // TODO junto y ya con unidades: primero los hechos del contorno, después
    // las cotas. Es lo que se pinta en la tabla y lo que se exporta.
    std::vector<MeasurementRow> rows;

    // Las que además pueden pasar a ser herramientas vigiladas. Los hechos del
    // contorno no están aquí: no hay herramienta que mida «el área de la
    // pieza», y ofrecer vigilar algo que luego no se puede vigilar sería
    // prometer de más.
    std::vector<AutoProposal> watchable;

    // Cuántas filas son hechos del contorno. Las de después son cotas.
    [[nodiscard]] std::size_t contourFactCount() const;

    // AVISOS SOBRE LO QUE SE ACABA DE MEDIR, vacío si no hay ninguno.
    //
    // No son errores: el informe se entrega igual. Son las razones por las que
    // los números de este informe pueden no significar lo que parece, y tienen
    // que viajar CON él — un aviso que se queda en la barra de estado mientras
    // las cotas se exportan a un CSV llega a la mitad de la gente que lo
    // necesita.
    //
    // Hoy son dos, y los dos salieron de fotografías reales:
    //
    //  - La pieza toca el borde del encuadre: está cortada, y entonces sus cotas
    //    son límites inferiores y no medidas.
    //  - El contorno está dentado muy por encima de lo normal: o la pieza lo es,
    //    o la detección está siguiendo el dibujo de la superficie en vez del
    //    borde, y entonces el perímetro no significa lo que dice.
    std::vector<std::string> warnings;
};

// Nombres de grupo, escritos una vez porque los comparan la interfaz y los
// tests.
inline constexpr const char* kGroupContour = "contorno";
inline constexpr const char* kGroupDimension = "cota";

// `gray` es la imagen para medir, `mask` la máscara de la pieza y `fixture` su
// sistema de coordenadas. `mmPerPixel` a 0 devuelve píxeles y lo dice.
// `frame`, si se pasa con tamaño, es el encuadre completo del que salió la
// máscara: hace falta para saber si la pieza está cortada por el borde. Vacío =
// no se comprueba, y entonces el informe no puede avisar de ello.
// CÓMO SE LLAMA UNA FIGURA, en una frase para leer.
//
// Existe UNA sola lista y está aquí porque es la que ya se usaba para el título
// del informe: «Polígono de 6 lados», «Arandela», «Pieza de contorno libre».
//
// No confundir con `vision::shapeKindName`, que devuelve una CLAVE —«poligono»,
// «circulo», sin tildes— pensada para las trazas de las pruebas y los registros.
// Esa clave se coló una vez en un mensaje de pantalla y el operador leía «esta
// pieza se ha reconocido como poligono»: la misma familia de fallo que la fila
// de casillas que decía «caliper  circle  point_to_line».
[[nodiscard]] std::string describeShape(const vision::ShapeClass& shape);

[[nodiscard]] PieceReport measureWholePiece(const cv::Mat& gray, const cv::Mat& mask,
                                            const vision::Fixture& fixture,
                                            double mmPerPixel = 0.0,
                                            LengthUnit unit = LengthUnit::Auto,
                                            const cv::Size& frame = {});

}  // namespace pci::inspection
