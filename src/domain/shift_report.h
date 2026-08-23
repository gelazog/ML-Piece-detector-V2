#pragma once

#include <string>
#include <utility>
#include <vector>

namespace pci::domain {

// EL INFORME DE UN TURNO.
//
// Sacar el historial a CSV es fácil y no es lo que hace falta. Un turno son
// cientos de inspecciones, y una hoja con cuatrocientas filas contesta «qué pasó
// exactamente a las 14:32» —que casi nunca se pregunta— y esconde las tres que
// sí se preguntan:
//
//   ¿Cuántas van y cuántas han pasado?
//   ¿QUÉ está fallando?
//   ¿DESDE CUÁNDO?
//
// Las filas se exportan igual, porque quien quiera cruzarlas con otra cosa las
// necesita. Pero el informe empieza por las respuestas.

struct InspectionRow {
    std::string startedAt;  // "YYYY-MM-DD HH:MM:SS"
    std::string piece;
    bool ok = true;
    double similarity = 0.0;
    int referenceVersion = 0;
    // Por qué salió NG. Vacío si pasó.
    //
    // Es el campo que convierte el historial en información: sin él, un turno
    // con 47 rechazos es un número, y con él es «31 de los 47 fueron por el
    // diámetro exterior», que ya dice qué hay que ir a mirar.
    std::string reason;
};

// Cuántos rechazos comparten un mismo motivo.
struct ReasonCount {
    std::string reason;
    int count = 0;
};

// Cómo fue una hora del turno. Sirve para contestar «¿desde cuándo?», que es la
// pregunta que una lista de filas ordenada por fecha no contesta: hay que leerla
// entera y llevar la cuenta a mano.
struct HourSlice {
    std::string hour;  // "YYYY-MM-DD HH"
    int total = 0;
    int ngCount = 0;
};

struct ShiftSummary {
    int total = 0;
    int okCount = 0;
    int ngCount = 0;
    // Fracción de piezas que pasaron, 0..1. -1 si no hubo ninguna: un
    // rendimiento del 0 % y «no se inspeccionó nada» son cosas distintas, y
    // enseñar 0 % cuando no ha pasado ninguna pieza es afirmar un desastre que
    // no ha ocurrido.
    double yield = -1.0;
    std::string from;
    std::string to;
    // Motivos de rechazo, del más frecuente al menos.
    std::vector<ReasonCount> reasons;
    std::vector<HourSlice> hours;
    // La hora con MÁS rechazos, si hubo alguno. Es la respuesta corta a «¿desde
    // cuándo?», para no obligar a leer el desglose entero.
    std::string worstHour;
};

[[nodiscard]] ShiftSummary summarise(const std::vector<InspectionRow>& rows);

// CSV con el resumen ARRIBA y las filas debajo, separados por una línea en
// blanco.
//
// El resumen va primero por lo mismo que los avisos del informe de pieza: quien
// abra esto en una hoja de cálculo lee lo de arriba, y un resumen al final de
// cuatrocientas filas no lo ve nadie. La cabecera de las filas sigue estando
// donde estaba para quien parsee el fichero.
[[nodiscard]] std::string shiftReportCsv(const std::vector<InspectionRow>& rows,
                                         const ShiftSummary& summary);

// El mismo informe en texto corrido, para pegarlo en un parte o en un correo.
[[nodiscard]] std::string shiftReportText(const std::vector<InspectionRow>& rows,
                                          const ShiftSummary& summary);

}  // namespace pci::domain
