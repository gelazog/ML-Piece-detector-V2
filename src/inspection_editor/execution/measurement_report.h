#pragma once

#include <string>
#include <vector>

#include "inspection_editor/execution/tool_executor.h"

namespace pci::inspection {

// Sacar las medidas de la aplicación.
//
// Hasta ahora no había ninguna forma. Se podían exportar los PUNTOS del
// contorno a CSV y el historial de veredictos, pero no las cotas: los números
// que el operador acaba de medir vivían en una tabla que solo se podía mirar.
// Una medición que no se puede sacar no entra en un informe de calidad, no se
// compara con la del turno anterior y no se manda a nadie — que son las tres
// cosas para las que se mide.
//
// Tres decisiones sobre el formato, y las tres se pagan si se hacen al revés:
//
// **El valor es un NÚMERO y la unidad va en su propia columna.** Escribir
// «50,00 mm (200,0 px)» en una celda convierte la columna en texto, y una
// exportación cuyas columnas no se pueden sumar ni promediar no es una
// exportación, es una captura de pantalla en letras.
//
// **Cada fila lleva su unidad**, porque las filas no son de la misma clase: en
// la misma tabla conviven longitudes, ángulos, recuentos y fracciones. Es la
// misma lección que costó la columna `unit` de la base de datos, que guardaba
// «px» para todo.
//
// **Los píxeles no se pierden.** Van en su propia columna junto a los
// milímetros: la escala puede resultar estar mal más tarde, y con los píxeles
// guardados se rehace la conversión sin volver a medir la pieza.

// Una fila del informe, ya resuelta. Se expone —en vez de generar solo el
// texto— porque el portapapeles, el CSV y cualquier tabla futura tienen que
// decir lo MISMO, y la única forma de garantizarlo es que salgan de aquí.
struct MeasurementRow {
    std::string tool;
    double value = 0.0;       // en la unidad de `unit`
    std::string unit;         // «mm», «cm», «px», «px²», «°», «n», «—»
    double pixels = 0.0;      // el valor crudo, tal como lo midió la herramienta
    std::string state;        // «OK», «NG» o «—» para las informativas
    double toleranceMin = 0.0;
    double toleranceMax = 0.0;
    bool hasTolerance = false;  // las informativas no juzgan y no tienen banda
    int pieceIndex = 0;
    std::string detail;
    // De dónde sale la fila. Sirve para separar en la tabla lo que es un HECHO
    // del contorno —perímetro, área, agujeros— de lo que es una COTA con su
    // banda: mezclarlos sin distinguirlos invita a buscarle tolerancia a un
    // área que nadie ha declarado.
    std::string group;
};

// `tolerances`, si se pasa, son las herramientas con las que se midió, en el
// mismo orden que los resultados: `ToolRunResult` no lleva su banda dentro.
// Sin ellas las columnas de tolerancia salen vacías, que es honesto — mejor eso
// que un cero que parece una tolerancia de cero.
[[nodiscard]] std::vector<MeasurementRow> measurementRows(
    const std::vector<ToolRunResult>& results, double mmPerPixel, LengthUnit unit,
    const std::vector<ToolConfig>* tolerances = nullptr);

// CSV con cabecera. Locale clásico a la fuerza y separador de coma, igual que
// `vision::contourToCsv`: en un Windows en español el separador decimal por
// defecto es la coma, y un CSV con «12,50» en una columna separada por comas no
// lo abre nadie.
[[nodiscard]] std::string measurementsToCsv(const std::vector<MeasurementRow>& rows);

// Lo mismo en texto alineado, para pegar en un correo o en un parte. El CSV es
// para la hoja de cálculo; esto es para que alguien lo lea.
[[nodiscard]] std::string measurementsToText(const std::vector<MeasurementRow>& rows);

}  // namespace pci::inspection
