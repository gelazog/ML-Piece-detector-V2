// EL PANEL DE MEDIDAS ESTABA Y ERA CONFUSO.
//
// Primera petición: «falta la parte en donde te resume las medidas, la
// ventana/pestaña para verlas». Los números existían: se pintan sobre el
// vídeo, encima de cada herramienta. Eso funciona con tres cotas y se rompe
// con catorce —las etiquetas se pisan— y con varias piezas se rompe del todo:
// el lienzo escribe los números de UNA sola pieza.
//
// Segunda petición, ya con la tabla delante: «el panel de mediciones está
// confuso». Cinco averías concretas:
//
//   1. Con «Todas» la misma cota se repetía por cada pieza: 15 filas para 5
//      cotas y 3 piezas, y comparar piezas exigía leer la columna «Pieza»
//      fila a fila.
//   2. El ojo y la ✕ vivían en CADA fila pero actuaban sobre la HERRAMIENTA
//      entera: borrar desde la fila de la pieza 3 borraba la cota en las
//      tres, que es engañoso.
//   3. La banda ocupaba media fila («14.75mm … 15.25mm») y el estado era una
//      frase («Cumple, margen 0.25mm»); nada se leía de un vistazo.
//   4. El resumen iba abajo y pequeño («11 cumplen, 1 no.») y no decía QUÉ
//      pieza fallaba ni POR QUÉ cota.
//   5. Las filas informativas (sin tolerancia) llenaban columnas de «—».
//
// Estas pruebas vigilan el rediseño: una fila por cota (nunca por
// pieza×cota), un ojo y una ✕ por cota, una tolerancia compacta, un estado
// compacto con la frase completa en el tooltip, y un veredicto arriba que
// nombra la pieza y la cota que falla en vez de contar OK/NG a secas.
//
// Las celdas llevan `objectName` a propósito: con las informativas al final,
// el número de fila de una cota ya no es fijo, así que las pruebas las buscan
// por nombre —`valueCell_<id>`, `stateCell_<id>`, `pieceCell_<id>_<pieza>`—,
// nunca por su posición ni por su texto.

#include <gtest/gtest.h>

#include <QAction>
#include <QDockWidget>
#include <QComboBox>
#include <QLabel>
#include <QTest>
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

// Busca una celda por su nombre, no por su posición ni por su texto: con las
// filas informativas al final, la posición de una cota ya no es constante.
QLabel* cellByName(const ui::MeasurementsPanel& panel, const QString& objectName) {
    return panel.findChild<QLabel*>(objectName);
}

QString cellText(const ui::MeasurementsPanel& panel, const QString& objectName) {
    auto* label = cellByName(panel, objectName);
    return label != nullptr ? label->text() : QString();
}

QString cellTooltip(const ui::MeasurementsPanel& panel, const QString& objectName) {
    auto* label = cellByName(panel, objectName);
    return label != nullptr ? label->toolTip() : QString();
}

}  // namespace

TEST(MeasurementsPanel, EveryCotaGetsOneRowNotOnePerPiece) {
    // La avería nº1 del taller: «con Todas la misma cota se repite por cada
    // pieza». Con 3 piezas y 2 cotas la tabla vieja daba 6 filas —una por
    // pieza×cota—; comparar piezas exigía leer la columna «Pieza» fila a fila.
    // Ahora una cota es UNA fila, con una celda por pieza dentro de ella.
    ui::MeasurementsPanel panel;
    const std::vector<ToolRunResult> results{
        measuring(1, "Ancho", 42.0, true, 0), measuring(2, "Alto", 18.0, false, 0),
        measuring(1, "Ancho", 41.5, true, 1), measuring(2, "Alto", 17.9, true, 1),
        measuring(1, "Ancho", 43.2, true, 2), measuring(2, "Alto", 18.1, true, 2)};
    const std::vector<ToolConfig> configs{banded(1, "Ancho", 40.0, 44.0),
                                          banded(2, "Alto", 17.5, 18.5)};
    panel.setResults(results, configs, 0.0, LengthUnit::Pixels);

    EXPECT_EQ(panel.rowCount(), 2)
        << "hay 2 cotas y 3 piezas: la tabla vieja daba 6 filas —una por "
           "pieza×cota—, que es justo la avería que este rediseño arregla";

    // Y cada cota tiene una celda por pieza, no una fila por pieza.
    EXPECT_NE(cellByName(panel, QStringLiteral("pieceCell_1_0")), nullptr)
        << "falta el Ancho de la pieza 1";
    EXPECT_NE(cellByName(panel, QStringLiteral("pieceCell_1_1")), nullptr)
        << "falta el Ancho de la pieza 2";
    EXPECT_NE(cellByName(panel, QStringLiteral("pieceCell_1_2")), nullptr)
        << "falta el Ancho de la pieza 3";
    EXPECT_NE(cellByName(panel, QStringLiteral("pieceCell_2_1")), nullptr)
        << "falta el Alto de la pieza 2";
}

TEST(MeasurementsPanel, AToolThatDoesNotMeasureShowsItsReasonInstead) {
    // LA OTRA MITAD DE «VARIAS HERRAMIENTAS NO MUESTRAN MEDIDAS». Medido sobre
    // las 32: ninguna se queda callada, todas explican por qué no dan número.
    ui::MeasurementsPanel panel;
    ToolRunResult failed;
    failed.toolId = 7;
    failed.name = "Calibre 1";
    failed.ok = false;
    failed.measured = 0.0;
    failed.detail = "Se necesitan 2 bordes y se detectaron 0";
    panel.setResults({failed}, {banded(7, "Calibre 1", 10.0, 20.0)}, 0.0,
                     LengthUnit::Pixels);

    const QString value = cellText(panel, QStringLiteral("valueCell_7"));
    std::printf("  [medidas] sin medir, la celda dice «%s»\n", value.toStdString().c_str());
    EXPECT_TRUE(value.contains(QStringLiteral("2 bordes")))
        << "la celda del valor enseña «" << value.toStdString()
        << "» en vez del motivo: el operador ve un hueco y no sabe si el trazo estaba mal "
           "puesto o si la pieza no tiene ese rasgo";

    // El estado, compacto, dice que no mide — no puede dar un veredicto de
    // cumple/no cumple sobre un número que no existe.
    EXPECT_EQ(cellText(panel, QStringLiteral("stateCell_7")), QStringLiteral("No mide"));

    // Y el veredicto de arriba lo nombra: una sola pieza, así que dice
    // directamente cuál es la cota y por qué, sin inventarle un «se pasa».
    auto* verdict = panel.findChild<QLabel*>(QStringLiteral("measurementsVerdict"));
    ASSERT_NE(verdict, nullptr);
    std::printf("  [medidas] veredicto: «%s»\n", verdict->text().toStdString().c_str());
    EXPECT_TRUE(verdict->text().contains(QStringLiteral("Calibre 1")))
        << "el veredicto no nombra la cota que no mide: " << verdict->text().toStdString();
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

    for (const auto& result : results) {
        const QString objectName = QStringLiteral("valueCell_%1").arg(result.toolId);
        const QString shown = cellText(panel, objectName);
        const QString expected = QString::fromStdString(
            formatMeasure(result, mmPerPixel, LengthUnit::Millimeters, true));
        std::printf("  [medidas] %-8s -> «%s»\n", result.name.c_str(),
                    shown.toStdString().c_str());
        EXPECT_EQ(shown, expected)
            << "la tabla rotula la medida por su cuenta: es la quinta copia de la regla de "
               "unidades, y las otras cuatro se equivocaron igual";
    }
    // Y el recuento no puede llevar milímetros, que es el fallo concreto que
    // aquella regla producía.
    EXPECT_FALSE(cellText(panel, QStringLiteral("valueCell_2")).contains(QStringLiteral("mm")))
        << "un recuento de lados sale en milímetros";
}

TEST(MeasurementsPanel, AConstructionIsNotGivenAVerdictItCannotHave) {
    // Las construcciones geométricas no miden nada que pueda estar dentro o
    // fuera de tolerancia: sólo calculan un elemento para que otras lo
    // referencien. Antes la celda de estado ponía «—»; eso es la avería nº5
    // —columnas llenas de «—» en las filas informativas— y además invita a
    // leer una construcción como si tuviera veredicto.
    ui::MeasurementsPanel panel;
    ToolRunResult construction;
    construction.toolId = 3;
    construction.name = "Punto medio";
    construction.ok = true;
    construction.informative = true;
    construction.measured = 0.0;
    construction.detail = "punto (120,80)";
    panel.setResults({construction}, {}, 0.0, LengthUnit::Pixels);

    EXPECT_EQ(cellText(panel, QStringLiteral("stateCell_3")), QStringLiteral("referencia"))
        << "a una construcción se le pone veredicto: no puede tenerlo, y un OK que no "
           "significa nada resta valor a los que sí";
    // Y la tolerancia se deja en blanco, no en «—»: con una sola construcción
    // no hace falta poner DOS guiones para decir lo mismo una vez.
    EXPECT_TRUE(cellText(panel, QStringLiteral("toleranceCell_3")).isEmpty())
        << "la tolerancia de una construcción no es «—», es que no aplica";
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

// EL OJO Y LA PAPELERA, UNA VEZ POR COTA.
//
// Avería nº2 del taller: «el ojo y la ✕ están en CADA fila, pero actúan sobre
// la HERRAMIENTA (todas las piezas). Borrar desde la fila de la pieza 3
// borra la cota en las tres: engañoso.» Con el rediseño solo hay una fila por
// cota, así que solo puede haber un ojo y una papelera por cota, sin
// importar cuántas piezas haya.
TEST(MeasurementsPanel, TheEyeAndTheBinActOncePerCotaNotOncePerPieceRow) {
    ui::MeasurementsPanel panel;
    const std::vector<ToolRunResult> results{
        measuring(7, "Ancho", 42.0, true, 0), measuring(9, "Alto", 18.0, true, 0),
        measuring(7, "Ancho", 41.0, true, 1), measuring(9, "Alto", 17.5, true, 1),
        measuring(7, "Ancho", 43.0, true, 2), measuring(9, "Alto", 18.2, true, 2)};
    panel.setResults(results, {banded(7, "Ancho", 40.0, 44.0), banded(9, "Alto", 17.0, 19.0)},
                     0.0, LengthUnit::Pixels);

    auto* table = panel.findChild<QTableWidget*>(QStringLiteral("measurementsTable"));
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->findChildren<QToolButton*>().size(), 4)
        << "con 2 cotas y 3 piezas debería haber 2 ojos y 2 papeleras —uno por cota—, no "
           "seis de cada, uno por pieza";

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

    // El ojo de la cota 9, encontrado por nombre y no por posición: emparejar
    // por fila ya se rompió una vez en el informe de pieza —«Ø» apagaba
    // «alto»— al reordenar la tabla.
    auto* eye = panel.findChild<QToolButton*>(QStringLiteral("eyeButton_9"));
    ASSERT_NE(eye, nullptr) << "la cota 9 no tiene ojo";
    eye->click();
    EXPECT_EQ(hiddenTool, 9) << "el ojo apaga la cota equivocada";
    EXPECT_FALSE(hiddenVisible);
    // Y la cota sigue en la tabla: apagar el dibujo no es apagar la medida.
    EXPECT_EQ(panel.rowCount(), 2)
        << "ocultar el dibujo de una cota la ha quitado de la tabla: entonces el ojo no "
           "apaga el dibujo, apaga la medida";

    auto* bin = panel.findChild<QToolButton*>(QStringLiteral("deleteButton_7"));
    ASSERT_NE(bin, nullptr) << "la cota 7 no tiene papelera";
    bin->click();
    EXPECT_EQ(deleted, 7) << "se pide borrar la cota equivocada";
    EXPECT_EQ(panel.rowCount(), 2)
        << "el panel ha borrado por su cuenta: quien borra es la ventana, que tiene el "
           "deshacer";

    // Pulsar cualquier celda de la fila de la cota 9 la señala sobre la
    // imagen, sin necesidad de pulsar justo su nombre.
    auto* cota9 = panel.findChild<QLabel*>(QStringLiteral("cotaCell_9"));
    ASSERT_NE(cota9, nullptr) << "la cota 9 no tiene celda de nombre";
    QTest::mouseClick(cota9, Qt::LeftButton);
    EXPECT_EQ(chosen, 9)
        << "pulsar la fila no señala esa cota sobre la imagen, que es lo único que "
           "permite saber cuál es cuál con catorce encima de la pieza";
}

TEST(MeasurementsPanel, TheStateCellIsCompactWithTheFullSentenceInTheTooltip) {
    // Pregunta literal del taller: «¿qué es OK a secas?». Tenía razón: «OK» dice
    // que cumple y no dice por cuánto. La avería nº3 fue la respuesta anterior
    // —«Cumple, margen 0.25mm» ocupando media fila—; ahora la celda dice «✓
    // 0.4» de un vistazo y la frase completa vive en el tooltip, para quien no
    // se fía del símbolo.
    ui::MeasurementsPanel panel;
    const std::vector<ToolRunResult> results{
        measuring(1, "Justo", 43.6, true, 0),    // banda 40…44 -> margen 0,4
        measuring(2, "Fuera", 45.2, false, 0)};  // banda 40…44 -> se pasa 1,2
    panel.setResults(results, {banded(1, "Justo", 40.0, 44.0), banded(2, "Fuera", 40.0, 44.0)},
                     0.0, LengthUnit::Pixels);

    const QString ok = cellText(panel, QStringLiteral("stateCell_1"));
    const QString bad = cellText(panel, QStringLiteral("stateCell_2"));
    const QString okTip = cellTooltip(panel, QStringLiteral("stateCell_1"));
    const QString badTip = cellTooltip(panel, QStringLiteral("stateCell_2"));
    std::printf("  [medidas] estado: «%s» (%s) / «%s» (%s)\n", ok.toStdString().c_str(),
                okTip.toStdString().c_str(), bad.toStdString().c_str(),
                badTip.toStdString().c_str());

    EXPECT_TRUE(ok.startsWith(QStringLiteral("✓")))
        << "la cota que cumple no lleva su marca: " << ok.toStdString();
    EXPECT_TRUE(ok.contains(QStringLiteral("0.4")) || ok.contains(QStringLiteral("0,4")))
        << "«cumple» sin decir por cuánto, compacto: con 0,4 px de margen la siguiente pieza "
           "puede salirse, y eso no se ve. Dice: " << ok.toStdString();
    EXPECT_TRUE(bad.startsWith(QStringLiteral("✕")))
        << "la cota que no cumple no lleva su marca: " << bad.toStdString();
    EXPECT_TRUE(bad.contains(QStringLiteral("1.2")) || bad.contains(QStringLiteral("1,2")))
        << "«no cumple» sin decir cuánto se pasa: " << bad.toStdString();

    // La frase completa —la que antes ocupaba la celda entera— ahora vive en
    // el tooltip, para quien pasa el ratón y quiere la respuesta sin abreviar.
    EXPECT_TRUE(okTip.contains(QStringLiteral("Cumple")) && okTip.contains(QStringLiteral("margen")))
        << "el tooltip no explica el margen en palabras: " << okTip.toStdString();
    EXPECT_TRUE(badTip.contains(QStringLiteral("No cumple")) && badTip.contains(QStringLiteral("pasa")))
        << "el tooltip no explica cuánto se pasa en palabras: " << badTip.toStdString();
}

TEST(MeasurementsPanel, WithSeveralPiecesThereIsAPickerAndItStartsShowingAll) {
    // «Si hay más piezas arriba debería de estar la opción de supervisar por
    // piezas y que sea un selectbox.»
    //
    // Empieza en «Todas» a propósito: abrir mostrando sólo la pieza 1 escondería
    // las otras dos sin que nadie lo hubiera pedido, que es el mismo fallo que
    // el vídeo tenía y que este panel vino a arreglar. Con «Todas» y más de una
    // pieza, la tabla enseña una columna por pieza; al elegir una sola, cambia
    // al modo compacto de valor único.
    ui::MeasurementsPanel panel;
    panel.setResults({measuring(1, "Ancho", 42.0, true, 0), measuring(1, "Ancho", 41.0, true, 1),
                      measuring(1, "Ancho", 43.0, true, 2)},
                     {}, 0.0, LengthUnit::Pixels);

    auto* picker = panel.findChild<QComboBox*>(QStringLiteral("piecePicker"));
    ASSERT_NE(picker, nullptr) << "no hay selector de pieza";
    EXPECT_TRUE(picker->isEnabled()) << "con tres piezas el selector está apagado";
    EXPECT_EQ(picker->count(), 4) << "faltan piezas en el selector (o falta «Todas»)";
    EXPECT_EQ(panel.rowCount(), 1) << "una sola cota son siempre una fila, con «Todas» o sin";
    EXPECT_NE(cellByName(panel, QStringLiteral("pieceCell_1_2")), nullptr)
        << "con «Todas» no se ven las tres piezas en la misma fila";

    // Elegir una filtra la tabla y avisa a la ventana, que mueve la MISMA
    // elección que las flechas y el mosaico.
    int announced = -99;
    QObject::connect(&panel, &ui::MeasurementsPanel::pieceChosen,
                     [&](int piece) { announced = piece; });
    picker->setCurrentIndex(picker->findData(1));
    EXPECT_EQ(announced, 1);
    // Ahora es el modo de una sola pieza: hay un valor único, no una celda por
    // pieza.
    EXPECT_NE(cellByName(panel, QStringLiteral("valueCell_1")), nullptr)
        << "elegir una pieza no ha cambiado al modo de valor único";
    EXPECT_EQ(cellByName(panel, QStringLiteral("pieceCell_1_2")), nullptr)
        << "elegir una pieza no ha dejado de enseñar las columnas de todas";

    // Y con UNA sola pieza el selector no elige nada: se deja a la vista para
    // que no aparezca y desaparezca, pero apagado.
    ui::MeasurementsPanel alone;
    alone.setResults({measuring(1, "Ancho", 42.0, true, 0)}, {}, 0.0, LengthUnit::Pixels);
    auto* single = alone.findChild<QComboBox*>(QStringLiteral("piecePicker"));
    ASSERT_NE(single, nullptr);
    EXPECT_FALSE(single->isEnabled());
}

TEST(MeasurementsPanel, TheVerdictNamesTheFailingPieceAndCota) {
    // Avería nº4 del taller: «el resumen va abajo y pequeño y no dice QUÉ
    // pieza falla ni POR QUÉ cota.» Con tres piezas y una que falla, el
    // veredicto tiene que decir el número de la pieza y el nombre de la cota,
    // no solo un recuento.
    ui::MeasurementsPanel panel;
    const std::vector<ToolRunResult> results{
        measuring(1, "Ø exterior", 15.0, true, 0),
        measuring(2, "Ø interior", 6.0, true, 0),
        measuring(1, "Ø exterior", 15.1, true, 1),
        measuring(2, "Ø interior", 6.2, true, 1),
        measuring(1, "Ø exterior", 15.2, true, 2),
        measuring(2, "Ø interior", 6.4, false, 2)};  // esta es la que falla
    panel.setResults(results,
                     {banded(1, "Ø exterior", 14.75, 15.25),
                      banded(2, "Ø interior", 5.9, 6.25)},
                     1.0, LengthUnit::Millimeters);

    auto* verdict = panel.findChild<QLabel*>(QStringLiteral("measurementsVerdict"));
    ASSERT_NE(verdict, nullptr);
    const QString text = verdict->text();
    std::printf("  [medidas] veredicto: «%s»\n", text.toStdString().c_str());

    EXPECT_TRUE(text.contains(QStringLiteral("1 de 3")))
        << "el veredicto no dice cuántas piezas fallan: " << text.toStdString();
    EXPECT_TRUE(text.contains(QStringLiteral("pieza 3")))
        << "el veredicto no dice QUÉ pieza falla: " << text.toStdString();
    EXPECT_TRUE(text.contains(QStringLiteral("Ø interior")))
        << "el veredicto no dice POR QUÉ cota falla: " << text.toStdString();
    EXPECT_TRUE(text.contains(QStringLiteral("0.15")) || text.contains(QStringLiteral("0,15")))
        << "el veredicto no dice cuánto se pasa: " << text.toStdString();

    // Y cuando todas cumplen, lo dice igual de claro y en positivo.
    ui::MeasurementsPanel allGood;
    allGood.setResults(
        {measuring(1, "Ancho", 42.0, true, 0), measuring(1, "Ancho", 41.5, true, 1)}, {}, 0.0,
        LengthUnit::Pixels);
    auto* goodVerdict = allGood.findChild<QLabel*>(QStringLiteral("measurementsVerdict"));
    ASSERT_NE(goodVerdict, nullptr);
    EXPECT_TRUE(goodVerdict->text().contains(QStringLiteral("2")))
        << "el veredicto de todo bien no dice cuántas piezas son: "
        << goodVerdict->text().toStdString();
    EXPECT_TRUE(goodVerdict->text().contains(QStringLiteral("✓")))
        << "el veredicto de todo bien no lleva su marca: " << goodVerdict->text().toStdString();
}

TEST(MeasurementsPanel, ToleranceIsCompactWithPlusMinusWhenSymmetric) {
    // Avería nº3, la mitad de la tolerancia: «Banda» ocupaba media fila
    // repitiendo la unidad dos veces —«14.75mm … 15.25mm»—. Declarada como
    // 14,75…15,25, esa banda es exactamente 15,00 ± 0,25: se escribe así, con
    // la unidad una sola vez.
    ui::MeasurementsPanel panel;
    panel.setResults({measuring(1, "Ø exterior", 15.0, true, 0)},
                     {banded(1, "Ø exterior", 14.75, 15.25)}, 1.0, LengthUnit::Millimeters);

    const QString tolerance = cellText(panel, QStringLiteral("toleranceCell_1"));
    std::printf("  [medidas] tolerancia: «%s»\n", tolerance.toStdString().c_str());
    EXPECT_TRUE(tolerance.contains(QStringLiteral("±")))
        << "una banda simétrica no se escribe como centro ± mitad: " << tolerance.toStdString();
    EXPECT_FALSE(tolerance.contains(QStringLiteral("–")))
        << "una banda simétrica no necesita el rango completo: " << tolerance.toStdString();
    // Y la unidad no está repetida: solo debe aparecer una vez en la celda.
    EXPECT_EQ(tolerance.count(QStringLiteral("mm")), 1)
        << "la unidad de la tolerancia está repetida: " << tolerance.toStdString();
}
