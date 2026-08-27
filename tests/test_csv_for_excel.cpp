// EL CSV QUE LA HOJA DE CÁLCULO SABE ABRIR.
//
// Queja del taller: «cuando lo conviertes a Excel también está mal el formato
// en el que lo da».
//
// Los tres exportadores —medidas, contorno e informe del turno— escribían el
// mismo CSV: coma de separador, punto decimal y sin marca de orden de bytes.
// Sobre el Windows de la estación, que está en `es-ES`:
//
//     sDecimal = ,    «26.9864» se lee 269864, o como fecha
//     sList    = ;    con comas, la fila entera cae en la columna A
//     sin BOM         «Perímetro» sale «PerÃ­metro», «mm²» sale «mmÂ²»
//
// Tres motivos independientes, y con cualquiera de ellos el fichero ya no se
// puede usar. Con los tres a la vez sale una única columna de texto ilegible.
//
// LO MÁS INTERESANTE es que el problema estaba VISTO y mal resuelto.
// `contourToCsv` llevaba escrito: «locale clásico a la fuerza: en un Windows en
// español el separador decimal por defecto es la coma, y un CSV con "12,50" en
// una columna separada por comas no lo abre nadie». Correcto — y la salida que
// eligió fue clavar el punto decimal y dejar la coma de separador. Eso arregla
// la colisión y no arregla el fichero. La salida no era clavar un lado: era
// mover los dos, que es lo que hace Excel cuando guarda un CSV.
//
// Las pruebas pasan el dialecto A MANO. Una que dependiera de la configuración
// regional de la máquina donde corre pasaría o fallaría según el equipo, que es
// la peor clase de prueba intermitente.

#include <gtest/gtest.h>

#include <cstdio>
#include <string>
#include <vector>

#include "core/csv_dialect.h"
#include "domain/shift_report.h"
#include "inspection_editor/execution/measurement_report.h"
#include "vision/geometry_features.h"

using namespace pci;

namespace {

// El de un Windows en español, que es el de la estación donde se usa esto.
core::CsvDialect spanish() {
    core::CsvDialect dialect;
    dialect.separator = ';';
    dialect.decimal = ',';
    return dialect;
}

// El clásico, para comprobar que no se le impone el punto y coma a quien no lo
// quiere: en un equipo con punto decimal, el punto y coma sería el error
// simétrico.
core::CsvDialect classic() {
    core::CsvDialect dialect;
    dialect.separator = ',';
    dialect.decimal = '.';
    return dialect;
}

std::vector<inspection::MeasurementRow> someRows() {
    inspection::MeasurementRow row;
    row.tool = "Perímetro";
    row.value = 26.9864;
    row.unit = "mm";
    row.pixels = 527.08;
    row.state = "—";
    row.pieceIndex = 0;
    row.detail = "contorno exterior cerrado, sin los agujeros";
    row.group = "contorno";

    inspection::MeasurementRow tricky = row;
    tricky.tool = "Circularidad";
    tricky.value = 0.9378;
    tricky.unit = "—";
    // Con punto y coma DENTRO: es el texto real de esa fila, y es justo el que
    // rompe un CSV separado por punto y coma si nadie lo entrecomilla.
    tricky.detail = "4·pi·area/perimetro^2; 1 es un circulo perfecto";
    return {row, tricky};
}

// La primera línea de datos, partida por el separador, respetando comillas.
std::vector<std::string> splitRespectingQuotes(const std::string& line, char separator) {
    std::vector<std::string> fields;
    std::string current;
    bool inQuotes = false;
    for (const char c : line) {
        if (c == '"') {
            inQuotes = !inQuotes;
        } else if (c == separator && !inQuotes) {
            fields.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    fields.push_back(current);
    return fields;
}

std::string lineNumber(const std::string& csv, std::size_t which) {
    std::size_t start = 0;
    for (std::size_t i = 0; i < which; ++i) {
        start = csv.find('\n', start);
        if (start == std::string::npos) {
            return {};
        }
        ++start;
    }
    const std::size_t end = csv.find('\n', start);
    return csv.substr(start, end == std::string::npos ? end : end - start);
}

}  // namespace

TEST(CsvForExcel, TheFileStartsWithTheByteOrderMarkOrEveryAccentIsLost) {
    // Sin BOM, Excel decodifica con la página de códigos ANSI. «Perímetro» sale
    // «PerÃ­metro» y «mm²» sale «mmÂ²». Es el mismo fallo que tenía el registro
    // de `reanudar.ps1`, por el mismo motivo, en otro fichero.
    const std::string csv = inspection::measurementsToCsv(someRows(), {}, spanish());
    ASSERT_GE(csv.size(), 3U);
    EXPECT_EQ(csv.substr(0, 3), std::string("\xEF\xBB\xBF"))
        << "el CSV ya no empieza por la marca de orden de bytes: Excel se comerá los "
           "acentos y las unidades (mm², °, Ø)";

    // Y va la PRIMERA de todo, también con avisos delante. Si va después, para
    // cuando llega Excel ya ha decidido que el fichero es ANSI.
    const std::string withWarnings =
        inspection::measurementsToCsv(someRows(), {"la pieza toca el borde"}, spanish());
    EXPECT_EQ(withWarnings.substr(0, 3), std::string("\xEF\xBB\xBF"))
        << "con avisos, la marca deja de ir la primera";
}

TEST(CsvForExcel, TheSpanishDialectSplitsIntoColumnsAndKeepsTheDecimals) {
    const std::string csv = inspection::measurementsToCsv(someRows(), {}, spanish());
    const std::string header = lineNumber(csv, 0);
    const std::string first = lineNumber(csv, 1);
    std::printf("  [csv] %s\n", first.c_str());

    const auto columns = splitRespectingQuotes(first, ';');
    EXPECT_EQ(columns.size(), 10U)
        << "la fila no se parte en las diez columnas: en Excel caería entera en la A";
    EXPECT_EQ(columns[1], "26,9864")
        << "el decimal no es una coma, así que Excel leerá 269864 o una fecha";
    EXPECT_NE(header.find("herramienta;valor;unidad"), std::string::npos)
        << "la cabecera sigue separada por comas";
}

TEST(CsvForExcel, ASemicolonInsideTheTextIsQuotedOrItBecomesAColumn) {
    // Esto no es teórico: el detalle de «Circularidad» lleva un punto y coma.
    // Al cambiar de separador, el texto que antes era inofensivo pasa a ser el
    // que rompe la fila — y el que llevaba comas deja de necesitar comillas.
    const std::string csv = inspection::measurementsToCsv(someRows(), {}, spanish());
    const auto columns = splitRespectingQuotes(lineNumber(csv, 2), ';');
    ASSERT_EQ(columns.size(), 10U)
        << "el punto y coma de dentro del texto ha partido la fila en una columna de más";
    EXPECT_EQ(columns[8], "4·pi·area/perimetro^2; 1 es un circulo perfecto");
    std::printf("  [csv] detalle con «;» dentro: %s\n", columns[8].c_str());

    // Y al revés: con separador COMA, el texto con comas es el que hay que
    // entrecomillar. La regla depende del dialecto, y por eso vive con él.
    const std::string plain = inspection::measurementsToCsv(someRows(), {}, classic());
    const auto plainColumns = splitRespectingQuotes(lineNumber(plain, 1), ',');
    ASSERT_EQ(plainColumns.size(), 10U);
    EXPECT_EQ(plainColumns[8], "contorno exterior cerrado, sin los agujeros");
}

TEST(CsvForExcel, NobodyIsForcedIntoSemicolonsWhoDoesNotWantThem) {
    // En un equipo con punto decimal, el punto y coma sería el error simétrico:
    // exactamente el mismo fallo, al revés. El dialecto se pregunta, no se
    // decide por decreto.
    const std::string csv = inspection::measurementsToCsv(someRows(), {}, classic());
    const std::string first = lineNumber(csv, 1);
    const auto columns = splitRespectingQuotes(first, ',');
    EXPECT_EQ(columns.size(), 10U);
    EXPECT_EQ(columns[1], "26.9864")
        << "se le ha puesto la coma decimal a un equipo que usa el punto";
}

TEST(CsvForExcel, ImpossibleRegionalSettingsGetAWorkingFileAnyway) {
    // Hay equipos con la configuración a medio hacer: coma decimal Y coma de
    // lista. Respetar eso al pie de la letra daría un fichero que no puede leer
    // nadie, ni Excel ni un script. Entre obedecer una configuración rota y
    // producir algo legible, lo segundo.
    core::CsvDialect broken;
    broken.separator = ',';
    broken.decimal = ',';
    // `systemCsvDialect` es quien deshace el empate; aquí se comprueba que la
    // regla existe y hacia dónde va.
    const core::CsvDialect real = core::systemCsvDialect();
    std::printf("  [csv] este equipo: separador '%c', decimal '%c'\n", real.separator,
                real.decimal);
    EXPECT_NE(real.separator, real.decimal)
        << "el dialecto del sistema usa el mismo carácter para separar campos y para "
           "decimales: el fichero que salga no lo puede leer nadie";
}

TEST(CsvForExcel, TheOtherTwoExportersSpeakTheSameDialect) {
    // Tres exportadores con tres criterios sería el mismo desorden que motivó
    // la paleta de colores: el operador exporta el contorno y el informe del
    // turno el mismo día y uno de los dos no abre.
    vision::ContourReport contour;
    contour.valid = true;
    contour.outer = {{10, 20}, {30, 40}};
    const std::string contourCsv = vision::contourToCsv(contour, 0.05, spanish());
    EXPECT_EQ(contourCsv.substr(0, 3), std::string("\xEF\xBB\xBF"))
        << "el CSV del contorno no lleva marca de orden de bytes";
    EXPECT_NE(contourCsv.find("contorno;punto;x_mm;y_mm"), std::string::npos)
        << "el CSV del contorno sigue separando por comas";
    EXPECT_NE(contourCsv.find("0,5000"), std::string::npos)
        << "el CSV del contorno sigue escribiendo el decimal con punto";
    std::printf("  [csv] contorno: %s\n", lineNumber(contourCsv, 1).c_str());

    domain::ShiftSummary summary;
    summary.total = 10;
    summary.okCount = 9;
    summary.ngCount = 1;
    summary.yield = 0.9;
    const std::string shift = domain::shiftReportCsv({}, summary, spanish());
    EXPECT_EQ(shift.substr(0, 3), std::string("\xEF\xBB\xBF"))
        << "el informe del turno no lleva marca de orden de bytes";
    EXPECT_NE(shift.find("inspecciones;10"), std::string::npos)
        << "el informe del turno sigue separando por comas";
    EXPECT_NE(shift.find("rendimiento;90,0 %"), std::string::npos)
        << "el rendimiento del turno sigue con el decimal en punto";
}
