// LAS MEDIDAS ESTABAN Y NO SE PODÍAN LEER.
//
// Petición de uso: «falta la parte en donde te resume las medidas, la
// ventana/pestaña para verlas».
//
// Los números existían: se pintan sobre el vídeo, encima de cada herramienta.
// Eso funciona con tres cotas y se rompe con catorce —las etiquetas se pisan, y
// el lienzo tiene un buscador de huecos justamente por eso— y con varias piezas
// se rompe del todo: el lienzo escribe los números de UNA sola pieza, porque
// catorce por seis serían ochenta y cuatro encima del vídeo. Esa decisión es la
// correcta para el vídeo, y deja al operador sin poder leer las demás en ningún
// sitio.
//
// Lo que estas pruebas vigilan no es que la tabla exista, sino las tres cosas
// que la hacen útil y que son fáciles de perder:
//
//   1. que estén TODAS, de todas las piezas —si sólo saliera la pieza medida,
//      la tabla tendría el mismo problema que el vídeo—;
//   2. que la que NO mide enseñe su motivo en la fila, que es la otra mitad de
//      «varias herramientas no muestran medidas»;
//   3. que rotule la medida EXACTAMENTE igual que el vídeo. Cuatro pantallas
//      tuvieron su propia regla de unidades una vez y las cuatro se
//      equivocaban igual: convertían a milímetros todo lo que no fuera un
//      ángulo, y un recuento de lados salía como «6,00 mm».

#include <gtest/gtest.h>

#include <QLabel>
#include <QTableWidget>

#include <cstdio>
#include <vector>

#include "inspection_editor/execution/tool_executor.h"
#include "ui/measurements_panel.h"

using namespace pci;
using namespace pci::inspection;

namespace {

ToolRunResult measuring(std::int64_t id, const char* name, double value, bool ok,
                        int pieceIndex = 0, MeasuredKind kind = MeasuredKind::Length) {
    ToolRunResult result;
    result.toolId = id;
    result.name = name;
    result.measured = value;
    result.ok = ok;
    result.kind = kind;
    result.pieceIndex = pieceIndex;
    result.detail = "L=" + std::to_string(value) + "px";
    return result;
}

ToolConfig banded(std::int64_t id, const char* name, double lo, double hi) {
    ToolConfig config;
    config.id = id;
    config.name = name;
    config.toleranceMin = lo;
    config.toleranceMax = hi;
    return config;
}

QString cell(const ui::MeasurementsPanel& panel, int row, int column) {
    auto* table = panel.findChild<QTableWidget*>(QStringLiteral("measurementsTable"));
    if (table == nullptr || table->item(row, column) == nullptr) {
        return {};
    }
    return table->item(row, column)->text();
}

}  // namespace

TEST(MeasurementsPanel, EveryToolOfEveryPieceGetsItsRow) {
    // Lo primero: que estén TODAS. Si la tabla enseñara solo las de la pieza
    // medida, tendría el mismo problema que el vídeo y no habría hecho falta.
    ui::MeasurementsPanel panel;
    const std::vector<ToolRunResult> results{
        measuring(1, "Ancho", 42.0, true, 0), measuring(2, "Alto", 18.0, false, 0),
        measuring(1, "Ancho", 41.5, true, 1), measuring(2, "Alto", 17.9, true, 1),
        measuring(1, "Ancho", 43.2, true, 2)};
    const std::vector<ToolConfig> configs{banded(1, "Ancho", 40.0, 44.0),
                                          banded(2, "Alto", 17.5, 18.5)};
    panel.setResults(results, configs, 0.0, LengthUnit::Pixels);

    EXPECT_EQ(panel.rowCount(), 5)
        << "faltan filas: con varias piezas, la tabla tiene que enseñar las cotas de "
           "todas — sobre el vídeo sólo caben las de una, y por eso existe esto";
    // Y cada fila dice DE QUÉ PIEZA es, numerada como en el mosaico y en el
    // vídeo: con seis piezas, una tabla sin ese número no se puede interpretar.
    EXPECT_EQ(cell(panel, 0, 0), QStringLiteral("1"));
    EXPECT_EQ(cell(panel, 2, 0), QStringLiteral("2"));
    EXPECT_EQ(cell(panel, 4, 0), QStringLiteral("3"));
}

TEST(MeasurementsPanel, AToolThatDoesNotMeasureShowsItsReasonInstead) {
    // LA OTRA MITAD DE «VARIAS HERRAMIENTAS NO MUESTRAN MEDIDAS». Medido sobre
    // las 32: ninguna se queda callada, todas explican por qué no dan número.
    // Lo que faltaba era un sitio donde leer esa explicación mientras se
    // trabaja, sin abrir la herramienta una por una.
    ui::MeasurementsPanel panel;
    ToolRunResult failed;
    failed.toolId = 7;
    failed.name = "Calibre 1";
    failed.ok = false;
    failed.measured = 0.0;
    failed.detail = "Se necesitan 2 bordes y se detectaron 0";
    panel.setResults({failed}, {banded(7, "Calibre 1", 10.0, 20.0)}, 0.0,
                     LengthUnit::Pixels);

    const QString value = cell(panel, 0, 2);
    std::printf("  [medidas] sin medir, la celda dice «%s»\n", value.toStdString().c_str());
    EXPECT_TRUE(value.contains(QStringLiteral("2 bordes")))
        << "la celda del valor enseña «" << value.toStdString()
        << "» en vez del motivo: el operador ve un hueco y no sabe si el trazo estaba mal "
           "puesto o si la pieza no tiene ese rasgo";

    // Y el resumen las cuenta APARTE de las que no cumplen: una cota fuera de
    // banda es un problema de la pieza; una que no mide es un problema del
    // trazo, del encuadre o de la referencia que le falta, y llevan a hacer
    // cosas distintas.
    auto* summary = panel.findChild<QLabel*>(QStringLiteral("measurementsSummary"));
    ASSERT_NE(summary, nullptr);
    std::printf("  [medidas] resumen: «%s»\n", summary->text().toStdString().c_str());
    EXPECT_TRUE(summary->text().contains(QStringLiteral("no llegan a medir")))
        << "el resumen mete las que no miden en el mismo saco que las que no cumplen: "
           "son dos averías distintas";
}

TEST(MeasurementsPanel, ItLabelsAMeasureTheSameWayTheVideoDoes) {
    // La regla de unidades vive en `formatMeasure` y en un solo sitio, porque
    // cuatro pantallas tuvieron la suya y las cuatro se equivocaban igual: un
    // recuento de lados salía «6,00 mm» y un área en px² se multiplicaba por la
    // escala LINEAL.
    //
    // Esta prueba compara la celda con esa función, no con un texto escrito
    // aquí: si mañana cambia cómo se escribe una medida, la tabla cambia con
    // ella y esto sigue en verde. Lo que no puede pasar es que se separen.
    ui::MeasurementsPanel panel;
    const std::vector<ToolRunResult> results{
        measuring(1, "Ancho", 40.0, true, 0, MeasuredKind::Length),
        measuring(2, "Lados", 6.0, true, 0, MeasuredKind::Count),
        measuring(3, "Ángulo", 90.0, true, 0, MeasuredKind::Angle)};
    const double mmPerPixel = 0.25;
    panel.setResults(results, {}, mmPerPixel, LengthUnit::Millimeters);

    for (int row = 0; row < 3; ++row) {
        const QString shown = cell(panel, row, 2);
        const QString expected = QString::fromStdString(formatMeasure(
            results[static_cast<std::size_t>(row)], mmPerPixel, LengthUnit::Millimeters,
            true));
        std::printf("  [medidas] %-8s -> «%s»\n",
                    results[static_cast<std::size_t>(row)].name.c_str(),
                    shown.toStdString().c_str());
        EXPECT_EQ(shown, expected)
            << "la tabla rotula la medida por su cuenta: es la quinta copia de la regla de "
               "unidades, y las otras cuatro se equivocaron igual";
    }
    // Y el recuento no puede llevar milímetros, que es el fallo concreto que
    // aquella regla producía.
    EXPECT_FALSE(cell(panel, 1, 2).contains(QStringLiteral("mm")))
        << "un recuento de lados sale en milímetros";
}

TEST(MeasurementsPanel, AConstructionIsNotGivenAVerdictItCannotHave) {
    // Las construcciones geométricas no miden nada que pueda estar dentro o
    // fuera de tolerancia: sólo calculan un elemento para que otras lo
    // referencien. Un OK verde encima enseña a no fiarse de los OK.
    ui::MeasurementsPanel panel;
    ToolRunResult construction;
    construction.toolId = 3;
    construction.name = "Punto medio";
    construction.ok = true;
    construction.informative = true;
    construction.measured = 0.0;
    construction.detail = "punto (120,80)";
    panel.setResults({construction}, {}, 0.0, LengthUnit::Pixels);
    EXPECT_EQ(cell(panel, 0, 4), QStringLiteral("—"))
        << "a una construcción se le pone veredicto: no puede tenerlo, y un OK que no "
           "significa nada resta valor a los que sí";
}
