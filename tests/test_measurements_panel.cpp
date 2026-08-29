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

#include <QAction>
#include <QDockWidget>
#include <QComboBox>
#include <QLabel>
#include <QToolButton>
#include <QTableWidget>

#include <cstdio>
#include <vector>

#include "inspection_editor/execution/tool_executor.h"
#include "ui/main_window.h"
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

// Las columnas de la tabla, por nombre y no por número: al añadir el ojo y la
// papelera todo se corrió una posición, y una prueba que dijera «columna 2»
// seguiría leyendo algo —otra cosa— sin fallar.
constexpr int kEye = 0;
constexpr int kPiece = 1;
constexpr int kName = 2;
constexpr int kValue = 3;
constexpr int kBand = 4;
constexpr int kState = 5;
constexpr int kDelete = 6;

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
    EXPECT_EQ(cell(panel, 0, kPiece), QStringLiteral("1"));
    EXPECT_EQ(cell(panel, 2, kPiece), QStringLiteral("2"));
    EXPECT_EQ(cell(panel, 4, kPiece), QStringLiteral("3"));
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

    const QString value = cell(panel, 0, kValue);
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
        const QString shown = cell(panel, row, kValue);
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
    EXPECT_FALSE(cell(panel, 1, kValue).contains(QStringLiteral("mm")))
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
    EXPECT_EQ(cell(panel, 0, kState), QStringLiteral("—"))
        << "a una construcción se le pone veredicto: no puede tenerlo, y un OK que no "
           "significa nada resta valor a los que sí";
}

// Y QUE SE ENCUENTRE Y ESTÉ DONDE ESTORBA MENOS.
//
// Dos respuestas del taller, en dos entregas seguidas:
//
//   1. «No agregaste el apartado de mediciones, como los de herramientas o
//      comparación o capturar.» Existía y tenía su entrada de menú, pero se
//      entregó CERRADO — y un panel que arranca cerrado no se encuentra.
//   2. «Las medidas en vivo quedan mejor del lado izquierdo, porque estás
//      saturando de opciones.» Y se cuenta: a la derecha ya vivían la paleta, la
//      comparación y el mosaico; a la izquierda sólo la tira de capturas.
TEST(MeasurementsPanel, ThePanelIsWhereTheOtherPanelsAre) {
    pci::ui::MainWindow window;
    auto* dock = window.findChild<QDockWidget*>(QStringLiteral("measurementsDock"));
    ASSERT_NE(dock, nullptr) << "no existe el panel de medidas";

    EXPECT_EQ(window.dockWidgetArea(dock), Qt::LeftDockWidgetArea)
        << "la tabla de medidas ha vuelto a la derecha, que es el lado que ya tiene tres "
           "paneles";

    // Y comparte pestaña con las capturas: partir la columna izquierda en dos
    // mitades estrechas dejaría las dos sin poder leerse.
    auto* captures = window.findChild<QDockWidget*>(QStringLiteral("captureDock"));
    ASSERT_NE(captures, nullptr);
    const auto tabbed = window.tabifiedDockWidgets(captures);
    EXPECT_TRUE(tabbed.contains(dock))
        << "la tabla de medidas no comparte pestaña con las capturas: o se ha quedado "
           "suelta ocupando sitio, o ha vuelto a arrancar cerrada y no se encuentra";

    // Y sigue teniendo su entrada en Ver, que es lo que permite recuperarla si
    // el operador la cierra: sin ella, cerrarla una vez sería cerrarla para
    // siempre.
    bool inTheMenu = false;
    for (auto* action : window.findChildren<QAction*>()) {
        if (action->objectName() == QStringLiteral("measurementsToggle")) {
            inTheMenu = true;
        }
    }
    EXPECT_TRUE(inTheMenu) << "no hay forma de volver a abrirla desde el menú Ver";
}

// EL OJO, LA PAPELERA Y LA FILA QUE SE PULSA.
//
// Segunda tanda de peticiones, ya con el panel delante: «si presiona alguna
// medida, que la remarque más», «un ojo para hacer visible o invisible las
// medidas, para saturar menos», «que puedas borrar si quieres la medida».
//
// Los tres son lo mismo desde el punto de vista del panel: **avisa y no toca**.
// Quien selecciona, oculta o borra es la ventana, que es la que tiene el
// deshacer y la que sabe qué herramientas hay. Un panel que borrara por su
// cuenta se saltaría el Ctrl+Z que ya existe.
TEST(MeasurementsPanel, TheEyeAndTheBinAndTheRowAskInsteadOfDoing) {
    ui::MeasurementsPanel panel;
    const std::vector<ToolRunResult> results{measuring(7, "Ancho", 42.0, true, 0),
                                             measuring(9, "Alto", 18.0, true, 0)};
    panel.setResults(results, {banded(7, "Ancho", 40.0, 44.0), banded(9, "Alto", 17.0, 19.0)},
                     0.0, LengthUnit::Pixels);

    std::int64_t hiddenTool = -1;
    bool hiddenVisible = true;
    std::int64_t deleted = -1;
    std::int64_t chosen = -1;
    QObject::connect(&panel, &ui::MeasurementsPanel::overlayVisibilityChanged,
                     [&](std::int64_t id, bool visible) {
                         hiddenTool = id;
                         hiddenVisible = visible;
                     });
    QObject::connect(&panel, &ui::MeasurementsPanel::deleteRequested,
                     [&](std::int64_t id) { deleted = id; });
    QObject::connect(&panel, &ui::MeasurementsPanel::toolChosen,
                     [&](std::int64_t id) { chosen = id; });

    auto* table = panel.findChild<QTableWidget*>(QStringLiteral("measurementsTable"));
    ASSERT_NE(table, nullptr);

    // El ojo de la SEGUNDA fila: se comprueba que el aviso lleva el id de ESA
    // herramienta y no el de la primera. Emparejar por posición ya se rompió una
    // vez en el informe de pieza —«Ø» apagaba «alto»— al reordenar la tabla.
    auto* eye = qobject_cast<QToolButton*>(table->cellWidget(1, kEye));
    ASSERT_NE(eye, nullptr) << "las filas no tienen ojo: no hay forma de dejar de dibujar "
                               "una cota que tapa la pieza";
    eye->click();
    EXPECT_EQ(hiddenTool, 9) << "el ojo apaga la cota equivocada";
    EXPECT_FALSE(hiddenVisible);
    // Y la cota sigue EN LA TABLA: apagar el dibujo no es apagar la medida. Son
    // dos cosas distintas y la segunda ya existe —el interruptor del informe—.
    EXPECT_EQ(panel.rowCount(), 2)
        << "ocultar el dibujo de una cota la ha quitado de la tabla: entonces el ojo no "
           "apaga el dibujo, apaga la medida";

    auto* bin = qobject_cast<QToolButton*>(table->cellWidget(0, kDelete));
    ASSERT_NE(bin, nullptr) << "las filas no se pueden borrar desde el panel";
    bin->click();
    EXPECT_EQ(deleted, 7) << "se pide borrar la cota equivocada";
    EXPECT_EQ(panel.rowCount(), 2)
        << "el panel ha borrado por su cuenta: quien borra es la ventana, que tiene el "
           "deshacer";

    table->selectRow(1);
    EXPECT_EQ(chosen, 9)
        << "pulsar una fila no señala esa cota sobre la imagen, que es lo único que "
           "permite saber cuál es cuál con catorce encima de la pieza";
}

TEST(MeasurementsPanel, TheVerdictSaysByHowMuchAndNotJustOK) {
    // Pregunta literal del taller: «¿qué es OK a secas?». Tenía razón: «OK» dice
    // que cumple y no dice por cuánto, que es lo que hace falta para saber si la
    // pieza va justa o sobrada — y para decidir si hay que parar la máquina
    // antes de que la siguiente se salga.
    ui::MeasurementsPanel panel;
    const std::vector<ToolRunResult> results{
        measuring(1, "Justo", 43.6, true, 0),    // banda 40…44 -> margen 0,4
        measuring(2, "Fuera", 45.2, false, 0)};  // banda 40…44 -> se pasa 1,2
    panel.setResults(results, {banded(1, "Justo", 40.0, 44.0), banded(2, "Fuera", 40.0, 44.0)},
                     0.0, LengthUnit::Pixels);

    const QString ok = cell(panel, 0, kState);
    const QString bad = cell(panel, 1, kState);
    std::printf("  [medidas] estado: «%s» / «%s»\n", ok.toStdString().c_str(),
                bad.toStdString().c_str());

    EXPECT_TRUE(ok.contains(QStringLiteral("Cumple")))
        << "el estado no dice en palabras si cumple: " << ok.toStdString();
    EXPECT_TRUE(ok.contains(QStringLiteral("0.4")) || ok.contains(QStringLiteral("0,4")))
        << "«cumple» sin decir por cuánto: con 0,4 px de margen la siguiente pieza puede "
           "salirse, y eso no se ve. Dice: " << ok.toStdString();
    EXPECT_TRUE(bad.contains(QStringLiteral("1.2")) || bad.contains(QStringLiteral("1,2")))
        << "«no cumple» sin decir cuánto se pasa: " << bad.toStdString();
}

TEST(MeasurementsPanel, WithSeveralPiecesThereIsAPickerAndItStartsShowingAll) {
    // «Si hay más piezas arriba debería de estar la opción de supervisar por
    // piezas y que sea un selectbox.»
    //
    // Empieza en «Todas» a propósito: abrir mostrando sólo la pieza 1 escondería
    // las otras cinco sin que nadie lo hubiera pedido, que es el mismo fallo que
    // el vídeo tenía y que este panel vino a arreglar.
    ui::MeasurementsPanel panel;
    panel.setResults({measuring(1, "Ancho", 42.0, true, 0), measuring(1, "Ancho", 41.0, true, 1),
                      measuring(1, "Ancho", 43.0, true, 2)},
                     {}, 0.0, LengthUnit::Pixels);

    auto* picker = panel.findChild<QComboBox*>(QStringLiteral("piecePicker"));
    ASSERT_NE(picker, nullptr) << "no hay selector de pieza";
    EXPECT_TRUE(picker->isEnabled()) << "con tres piezas el selector está apagado";
    EXPECT_EQ(picker->count(), 4) << "faltan piezas en el selector (o falta «Todas»)";
    EXPECT_EQ(panel.rowCount(), 3) << "no arranca enseñándolas todas";

    // Elegir una filtra la tabla y avisa a la ventana, que mueve la MISMA
    // elección que las flechas y el mosaico.
    int announced = -99;
    QObject::connect(&panel, &ui::MeasurementsPanel::pieceChosen,
                     [&](int piece) { announced = piece; });
    picker->setCurrentIndex(picker->findData(1));
    EXPECT_EQ(announced, 1);
    EXPECT_EQ(panel.rowCount(), 1) << "elegir una pieza no filtra la tabla";

    // Y con UNA sola pieza el selector no elige nada: se deja a la vista para
    // que no aparezca y desaparezca, pero apagado.
    ui::MeasurementsPanel alone;
    alone.setResults({measuring(1, "Ancho", 42.0, true, 0)}, {}, 0.0, LengthUnit::Pixels);
    auto* single = alone.findChild<QComboBox*>(QStringLiteral("piecePicker"));
    ASSERT_NE(single, nullptr);
    EXPECT_FALSE(single->isEnabled());
}
