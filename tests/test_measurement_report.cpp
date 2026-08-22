// Banco de la EXPORTACIÓN de medidas.
//
// Hasta ahora no había ninguna: se podían sacar los puntos del contorno y el
// historial de veredictos, pero no las cotas. Los números que el operador acaba
// de medir vivían en una tabla que solo se podía mirar, y una medición que no
// se puede sacar no entra en un informe de calidad, no se compara con la del
// turno anterior y no se manda a nadie — que son las tres cosas para las que se
// mide.
//
// Lo que este banco vigila es que lo exportado sea USABLE, que es distinto de
// que exista: columnas que una hoja de cálculo pueda sumar, cada fila con su
// unidad, y los píxeles conservados por si la escala resulta estar mal después.
#include <gtest/gtest.h>

#include <cstdio>

#include <string>
#include <vector>

#include "inspection_editor/execution/measurement_report.h"

using namespace pci::inspection;

namespace {

ToolRunResult measured(const std::string& name, double value, MeasuredKind kind,
                       bool ok = true) {
    ToolRunResult result;
    result.name = name;
    result.measured = value;
    result.kind = kind;
    result.ok = ok;
    return result;
}

// 0,25 mm por píxel: escala redonda, para que el número esperado se pueda
// comprobar de cabeza.
constexpr double kScale = 0.25;

// La n-ésima celda de una línea de CSV, respetando las comillas. Se escribe
// aquí a mano a propósito: si el test partiera por comas a secas, no podría
// detectar el fallo del entrecomillado, que es justo uno de los que busca.
std::string cell(const std::string& line, int index) {
    std::string current;
    int found = 0;
    bool inQuotes = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (c == '"') {
            if (inQuotes && i + 1 < line.size() && line[i + 1] == '"') {
                current.push_back('"');
                ++i;
            } else {
                inQuotes = !inQuotes;
            }
            continue;
        }
        if (c == ',' && !inQuotes) {
            if (found == index) {
                return current;
            }
            current.clear();
            ++found;
            continue;
        }
        current.push_back(c);
    }
    return found == index ? current : std::string();
}

std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> out;
    std::string current;
    for (const char c : text) {
        if (c == '\n') {
            out.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(c);
    }
    if (!current.empty()) {
        out.push_back(current);
    }
    return out;
}

}  // namespace

TEST(MeasurementReport, TheValueIsANumberAndTheUnitIsItsOwnColumn) {
    // Si el valor saliera como «50,00 mm (200,0 px)», la columna sería texto y
    // no se podría ni sumar ni promediar. Una exportación cuyas columnas no se
    // pueden calcular no es una exportación, es una captura de pantalla en
    // letras.
    const auto rows = measurementRows({measured("Ancho", 200.0, MeasuredKind::Length)},
                                      kScale, LengthUnit::Millimeters);
    ASSERT_EQ(rows.size(), 1U);
    EXPECT_DOUBLE_EQ(rows.front().value, 50.0);  // 200 px · 0,25
    EXPECT_EQ(rows.front().unit, "mm");
    // Y los píxeles NO se pierden: la escala puede resultar estar mal más tarde
    // y con ellos se rehace la conversión sin volver a medir la pieza.
    EXPECT_DOUBLE_EQ(rows.front().pixels, 200.0);

    const auto csv = lines(measurementsToCsv(rows));
    ASSERT_EQ(csv.size(), 2U);
    EXPECT_EQ(cell(csv[0], 1), "valor");
    EXPECT_EQ(cell(csv[0], 2), "unidad");
    EXPECT_EQ(cell(csv[1], 1), "50.0000");
    EXPECT_EQ(cell(csv[1], 2), "mm");
    EXPECT_EQ(cell(csv[1], 3), "200.00");
}

TEST(MeasurementReport, EachRowCarriesItsOwnUnitBecauseTheyAreNotTheSameKind) {
    // En la misma tabla conviven longitudes, ángulos, recuentos y fracciones.
    // Es la misma lección que costó la columna `unit` de la base de datos, que
    // guardaba «px» para todo.
    const auto rows = measurementRows(
        {
            measured("Ancho", 200.0, MeasuredKind::Length),
            measured("Ángulo 1", 120.0, MeasuredKind::Angle),
            measured("Lados", 6.0, MeasuredKind::Count),
            measured("Circularidad", 0.93, MeasuredKind::Fraction),
            measured("Área", 1600.0, MeasuredKind::Area),
        },
        kScale, LengthUnit::Millimeters);
    ASSERT_EQ(rows.size(), 5U);
    EXPECT_EQ(rows[0].unit, "mm");
    EXPECT_EQ(rows[1].unit, "°");
    EXPECT_EQ(rows[2].unit, "n");
    EXPECT_EQ(rows[3].unit, "—");
    EXPECT_EQ(rows[4].unit, "mm²");

    // Lo que no es una longitud no se convierte con la escala.
    EXPECT_DOUBLE_EQ(rows[1].value, 120.0);
    EXPECT_DOUBLE_EQ(rows[2].value, 6.0);
    EXPECT_DOUBLE_EQ(rows[3].value, 0.93);
    // Y el área entra con la escala AL CUADRADO: 1600 · 0,25² = 100.
    EXPECT_DOUBLE_EQ(rows[4].value, 100.0);
}

TEST(MeasurementReport, ACommaInsideATextDoesNotShiftTheColumns) {
    // El fallo clásico del CSV, y el más traicionero porque no se ve hasta que
    // alguien abre la hoja y encuentra la tolerancia en la columna del estado.
    // Los detalles de las herramientas llevan comas a menudo.
    auto result = measured("Ø", 200.0, MeasuredKind::Length);
    result.detail = "Ø=50,00mm, redondez 0,12, cámara inclinada";
    const auto rows = measurementRows({result}, kScale, LengthUnit::Millimeters);
    const auto csv = lines(measurementsToCsv(rows));
    ASSERT_EQ(csv.size(), 2U);

    // La fila sigue teniendo el detalle ENTERO en su columna, la última.
    EXPECT_EQ(cell(csv[1], 8), result.detail);
    // Y las columnas de antes siguen en su sitio.
    EXPECT_EQ(cell(csv[1], 2), "mm");
    EXPECT_EQ(cell(csv[1], 4), "OK");

    // Unas comillas dentro del texto tampoco pueden romperlo.
    auto quotedResult = measured("Pieza", 10.0, MeasuredKind::Length);
    quotedResult.detail = "el borde \"bueno\" no se ve";
    const auto quotedCsv = lines(measurementsToCsv(
        measurementRows({quotedResult}, 0.0, LengthUnit::Pixels)));
    ASSERT_EQ(quotedCsv.size(), 2U);
    EXPECT_EQ(cell(quotedCsv[1], 8), quotedResult.detail);
}

TEST(MeasurementReport, TheDecimalSeparatorIsAlwaysADotWhateverTheMachineSays) {
    // En un Windows en español el separador decimal por defecto es la coma, y
    // un CSV con «12,50» en una columna separada por comas no lo abre nadie. Es
    // la misma decisión que ya tomó la exportación del contorno.
    const auto csv = measurementsToCsv(
        measurementRows({measured("Ancho", 200.0, MeasuredKind::Length)}, kScale,
                        LengthUnit::Millimeters));
    EXPECT_NE(csv.find("50.0000"), std::string::npos) << csv;
    EXPECT_EQ(csv.find("50,0000"), std::string::npos)
        << "el separador decimal salió como coma: el CSV no se puede abrir";
}

TEST(MeasurementReport, AConstructionThatJudgedNothingIsNotAnOk) {
    // Una construcción geométrica que salió bien no ha comprobado nada, solo ha
    // calculado un datum. Escribir «OK» sería dar por comprobado lo que nadie
    // comprobó.
    auto construction = measured("Punto medio", 0.0, MeasuredKind::Length);
    construction.informative = true;
    auto failed = measured("Ancho", 10.0, MeasuredKind::Length, /*ok=*/false);

    const auto rows = measurementRows({construction, failed}, kScale, LengthUnit::Auto);
    ASSERT_EQ(rows.size(), 2U);
    EXPECT_EQ(rows[0].state, "—");
    EXPECT_EQ(rows[1].state, "NG");
    // Y la informativa no lleva banda de tolerancia, porque no juzga.
    EXPECT_FALSE(rows[0].hasTolerance);
}

TEST(MeasurementReport, ToleranceTravelsInTheSameUnitAsTheMeasurement) {
    // Una banda que hay que convertir a mano al leerla es una banda que alguien
    // va a leer mal.
    auto result = measured("Ancho", 200.0, MeasuredKind::Length);
    result.toolId = 7;
    ToolConfig config;
    config.id = 7;
    config.name = "Ancho";
    config.toleranceMin = 180.0;  // px
    config.toleranceMax = 220.0;
    const std::vector<ToolConfig> tools{config};

    const auto rows = measurementRows({result}, kScale, LengthUnit::Millimeters, &tools);
    ASSERT_EQ(rows.size(), 1U);
    ASSERT_TRUE(rows.front().hasTolerance);
    EXPECT_DOUBLE_EQ(rows.front().toleranceMin, 45.0);  // 180 · 0,25
    EXPECT_DOUBLE_EQ(rows.front().toleranceMax, 55.0);
    // La medida cae dentro, que es lo que la banda debe reflejar.
    EXPECT_GE(rows.front().value, rows.front().toleranceMin);
    EXPECT_LE(rows.front().value, rows.front().toleranceMax);
}

TEST(MeasurementReport, WithoutTolerancesTheColumnsAreEmptyAndNotZero) {
    // Un cero en la columna de tolerancia parece una tolerancia de cero, que es
    // la más estricta que existe. Vacío dice la verdad: no se sabe.
    const auto rows = measurementRows({measured("Ancho", 200.0, MeasuredKind::Length)},
                                      kScale, LengthUnit::Millimeters);
    ASSERT_EQ(rows.size(), 1U);
    EXPECT_FALSE(rows.front().hasTolerance);
    const auto csv = lines(measurementsToCsv(rows));
    ASSERT_EQ(csv.size(), 2U);
    EXPECT_TRUE(cell(csv[1], 5).empty()) << "tolerancia mínima inventada";
    EXPECT_TRUE(cell(csv[1], 6).empty()) << "tolerancia máxima inventada";
}

TEST(MeasurementReport, WithoutCalibrationItGivesPixelsAndSaysSo) {
    // Inventar milímetros sin escala sería la peor salida de todas.
    const auto rows = measurementRows({measured("Ancho", 200.0, MeasuredKind::Length),
                                       measured("Área", 1600.0, MeasuredKind::Area)},
                                      0.0, LengthUnit::Auto);
    ASSERT_EQ(rows.size(), 2U);
    EXPECT_EQ(rows[0].unit, "px");
    EXPECT_DOUBLE_EQ(rows[0].value, 200.0);
    EXPECT_EQ(rows[1].unit, "px²");
    EXPECT_DOUBLE_EQ(rows[1].value, 1600.0);
}

TEST(MeasurementReport, TheTextFormAlignsWithTheLongestNameAndNotAFixedWidth) {
    // Con nombres largos, una tabla alineada a un ancho fijo deja de estar
    // alineada justo cuando más falta hace.
    const auto rows =
        measurementRows({measured("Ø", 200.0, MeasuredKind::Length),
                         measured("Distancia entre centros de agujeros", 90.0,
                                  MeasuredKind::Length)},
                        kScale, LengthUnit::Millimeters);
    const auto text = lines(measurementsToText(rows));
    ASSERT_EQ(text.size(), 2U);
    // Las dos filas empiezan la columna del valor en la misma posición.
    const auto valueAt = [](const std::string& line) {
        return line.find_first_of("0123456789", line.find("  "));
    };
    EXPECT_EQ(valueAt(text[0]), valueAt(text[1]))
        << "la tabla de texto no queda alineada:\n" << text[0] << '\n' << text[1];
}

TEST(MeasurementReport, NothingMeasuredGivesAHeaderAndNoRows) {
    // Un fichero vacío del todo no se distingue de un fallo de escritura. Con
    // la cabecera, se ve que se exportó y que no había nada.
    const auto csv = lines(measurementsToCsv({}));
    ASSERT_EQ(csv.size(), 1U);
    EXPECT_EQ(cell(csv[0], 0), "herramienta");
    EXPECT_TRUE(measurementsToText({}).empty());
}

// EL CSV ES LO QUE SOBREVIVE.
//
// Los avisos se metieron en el informe con este argumento: «un aviso que se
// queda en la ventana llega a la mitad de la gente, la otra mitad exporta el CSV
// y se lleva las cifras sin él». Y luego el CSV se exportaba SIN los avisos, con
// lo que el argumento quedaba sin cumplir en el único sitio donde importaba.
//
// La ventana se cierra. El fichero se guarda, se manda por correo y se abre tres
// semanas después, cuando ya nadie se acuerda de si la pieza entraba entera en
// el encuadre.
TEST(MeasurementExport, TheWarningsAreInsideTheFileAndNotJustOnScreen) {
    std::vector<pci::inspection::MeasurementRow> rows;
    pci::inspection::MeasurementRow row;
    row.tool = "Largo total";
    row.value = 42.5;
    row.unit = "mm";
    row.pixels = 425.0;
    row.state = "OK";
    row.group = "cota";
    rows.push_back(row);

    const std::vector<std::string> warnings{
        "La pieza toca el borde derecho del encuadre: esta cortada, asi que sus "
        "medidas son limites inferiores y no medidas."};

    const std::string csv = pci::inspection::measurementsToCsv(rows, warnings);
    std::printf("  [csv] primeras lineas:\n%s",
                csv.substr(0, csv.find("herramienta")).c_str());

    EXPECT_NE(csv.find("limites inferiores"), std::string::npos)
        << "el aviso no viaja en el CSV, que es justo el sitio donde tenia que ir";
    // Y ARRIBA del todo: un aviso al final de una hoja de calculo con cuarenta
    // filas no lo ve nadie.
    EXPECT_LT(csv.find("AVISO"), csv.find("herramienta"))
        << "el aviso sale despues de la cabecera: para entonces ya se estan leyendo "
           "las cifras";
    // Y la cabecera sigue estando, entera, para quien parsee el fichero.
    EXPECT_NE(csv.find("herramienta,valor,unidad,pixeles,estado"), std::string::npos)
        << "meter los avisos rompio la cabecera: el fichero deja de parsearse";
    EXPECT_NE(csv.find("Largo total"), std::string::npos);

    // Sin avisos, el fichero es EXACTAMENTE el de antes: quien ya tuviera una
    // hoja de calculo apuntando a estas columnas no se entera del cambio.
    const std::string plain = pci::inspection::measurementsToCsv(rows);
    EXPECT_EQ(plain.find("herramienta"), 0U)
        << "sin avisos, el CSV tiene que empezar por la cabecera como siempre";
    EXPECT_EQ(plain.find("AVISO"), std::string::npos);

    // Y lo mismo en el texto para pegar en un parte.
    const std::string text = pci::inspection::measurementsToText(rows, warnings);
    EXPECT_NE(text.find("limites inferiores"), std::string::npos);
    EXPECT_EQ(text.find("AVISO"), 0U) << "el aviso no encabeza el parte";
}

// Un aviso con comas o comillas no puede romper el fichero. Los textos de aviso
// llevan comas —«ancho, alto, area, perimetro»— asi que esto no es teorico.
TEST(MeasurementExport, AWarningWithCommasDoesNotBreakTheCsv) {
    std::vector<pci::inspection::MeasurementRow> rows;
    pci::inspection::MeasurementRow row;
    row.tool = "Ancho";
    row.value = 1.0;
    rows.push_back(row);

    const std::vector<std::string> warnings{
        "sus medidas (ancho, alto, area, perimetro) son \"limites inferiores\""};
    const std::string csv = pci::inspection::measurementsToCsv(rows, warnings);

    // La linea del aviso tiene que ser UNA sola linea y tener exactamente dos
    // campos: la etiqueta y el texto entrecomillado.
    const std::string first = csv.substr(0, csv.find('\n'));
    std::printf("  [csv] %s\n", first.c_str());
    EXPECT_EQ(first.rfind("AVISO,", 0), 0U);
    int quotes = 0;
    for (const char c : first) {
        if (c == '"') { ++quotes; }
    }
    EXPECT_EQ(quotes % 2, 0)
        << "comillas sin cerrar: el fichero deja de parsearse a partir de aqui";
    EXPECT_NE(first.find("ancho, alto"), std::string::npos)
        << "el texto del aviso se corto por una coma";
}
