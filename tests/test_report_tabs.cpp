// DOS PESTAÑAS AL MEDIR: LO QUE MIDE EL PROGRAMA Y LO QUE MIDES TÚ.
//
// Petición de uso: «al momento de darle al botón de medir debería de haber dos
// pestañas: una donde se vean todas las medidas automáticamente, y otra donde
// se pueda elegir qué herramientas se utilizan, cuáles no».
//
// El hueco era real: hasta ahora el botón de medir enseñaba hechos del contorno
// y propuestas automáticas, pero NINGUNA de las cotas que el operador había
// dibujado. Para verlas había que inspeccionar — que además guarda en el
// historial, o sea dos decisiones distintas metidas en un botón.
//
// Y la tercera parte de la petición: «si no cumple, simplemente diga que no
// cumple en su descripción de medida, o de dónde sale». Un «NG» a secas obliga
// a ir a buscar la tolerancia a otra pantalla para saber por qué.

#include <gtest/gtest.h>

#include <QApplication>
#include <QCheckBox>
#include <QLabel>
#include <QTabWidget>
#include <QTableWidget>
#include <QTreeWidget>

#include <cstdio>

#include "ui/piece_report_dialog.h"

using namespace pci;

namespace {

inspection::PieceReport plainReport() {
    inspection::PieceReport report;
    report.ok = true;
    report.headline = "Rectángulo";
    inspection::MeasurementRow row;
    row.tool = "Perímetro";
    row.value = 100.0;
    row.unit = "px";
    row.state = "—";
    row.group = "contorno";
    report.rows.push_back(row);
    return report;
}

ui::PieceReportDialog::DrawnTool toolThat(const char* name, bool ok, bool enabled) {
    ui::PieceReportDialog::DrawnTool tool;
    tool.config.id = 7;
    tool.config.name = name;
    tool.config.toleranceMin = 90.0;
    tool.config.toleranceMax = 110.0;
    tool.config.enabled = enabled;
    tool.result.toolId = 7;
    tool.result.name = name;
    tool.result.ok = ok;
    tool.result.measured = ok ? 100.0 : 140.0;
    tool.text = ok ? "100.00 px" : "140.00 px";
    return tool;
}

// El texto visible que contiene `needle`, lo enseñe una tabla o un árbol.
QString visibleTextWith(const ui::PieceReportDialog& dialog, const QString& needle) {
    QString found;
    for (auto* table : dialog.findChildren<QTableWidget*>()) {
        for (int row = 0; row < table->rowCount(); ++row) {
            for (int column = 0; column < table->columnCount(); ++column) {
                if (auto* item = table->item(row, column);
                    item != nullptr && item->text().contains(needle, Qt::CaseInsensitive)) {
                    found = item->text();
                }
            }
        }
    }
    for (auto* tree : dialog.findChildren<QTreeWidget*>()) {
        QTreeWidgetItemIterator it(tree);
        for (; *it != nullptr; ++it) {
            for (int column = 0; column < tree->columnCount(); ++column) {
                if ((*it)->text(column).contains(needle, Qt::CaseInsensitive)) {
                    found = (*it)->text(column);
                }
            }
        }
    }
    return found;
}

// La fila de una herramienta, la busque donde la busque.
//
// Antes esto era `tree->topLevelItem(0)`, y al agrupar por clase el primer nivel
// pasó a ser la CLASE: tres pruebas se rompieron sin que nada de lo que
// afirmaban hubiera dejado de ser cierto. Buscar por nombre las ata a la
// promesa —«esta cota se abre en lo que su figura mide»— y no a en qué renglón
// acabó dibujada.
QTreeWidgetItem* rowNamed(QTreeWidget* tree, const QString& name) {
    QTreeWidgetItemIterator it(tree);
    for (; *it != nullptr; ++it) {
        if ((*it)->text(1) == name) {
            return *it;
        }
    }
    return nullptr;
}

QTabWidget* tabsOf(const ui::PieceReportDialog& dialog) {
    const auto found = dialog.findChildren<QTabWidget*>();
    return found.isEmpty() ? nullptr : found.first();
}

}  // namespace

TEST(ReportTabs, MeasuringOffersBothTheAutomaticMeasuresAndYourOwnTools) {
    std::vector<ui::PieceReportDialog::DrawnTool> drawn = {
        toolThat("ancho", true, true), toolThat("alto", false, true)};
    ui::PieceReportDialog dialog(plainReport(), QStringLiteral("una imagen"), nullptr,
                                 nullptr, drawn);
    dialog.resize(900, 640);

    auto* tabs = tabsOf(dialog);
    ASSERT_NE(tabs, nullptr) << "el diálogo de medir no tiene pestañas";
    ASSERT_EQ(tabs->count(), 2) << "faltan pestañas: se esperaban las medidas y las "
                                   "herramientas del operador";
    std::printf("  [medir] pestañas: «%s» y «%s»\n",
                tabs->tabText(0).toStdString().c_str(),
                tabs->tabText(1).toStdString().c_str());
    // La segunda dice cuántas cotas tuyas hay, para no tener que abrirla para
    // saber si hay alguna.
    EXPECT_TRUE(tabs->tabText(1).contains(QStringLiteral("2")))
        << "la pestaña no dice cuántas herramientas hay: " << tabs->tabText(1).toStdString();
}

TEST(ReportTabs, EachToolHasASwitchThatDoesNotDeleteIt) {
    std::vector<ui::PieceReportDialog::DrawnTool> drawn = {
        toolThat("ancho", true, true), toolThat("alto", true, false)};
    ui::PieceReportDialog dialog(plainReport(), QStringLiteral("una imagen"), nullptr,
                                 nullptr, drawn);

    const auto boxes = dialog.findChildren<QCheckBox*>();
    ASSERT_EQ(boxes.size(), 2) << "no hay un interruptor por herramienta";
    // Cada uno arranca como estaba la herramienta.
    EXPECT_TRUE(boxes[0]->isChecked());
    EXPECT_FALSE(boxes[1]->isChecked()) << "una cota apagada aparece encendida";

    // Sin tocar nada, no hay cambios que guardar: abrir y cerrar el diálogo no
    // puede reescribir la plantilla.
    EXPECT_TRUE(dialog.toolsWithChangedState().empty())
        << "dice que hay cambios sin haber tocado ningún interruptor";

    // Y al cambiar uno, sale ESE y sólo ese.
    boxes[0]->setChecked(false);
    const auto changed = dialog.toolsWithChangedState();
    ASSERT_EQ(changed.size(), 1U) << "no devuelve exactamente la que cambió";
    EXPECT_EQ(changed.front().name, "ancho");
    EXPECT_FALSE(changed.front().enabled);
}

TEST(ReportTabs, AToolThatFailsSaysSoAndSaysBetweenWhatValues) {
    // «Si no cumple, simplemente diga que no cumple en su descripción de
    // medida, o de dónde sale». Un «NG» pelado obliga a buscar la tolerancia en
    // otra pantalla.
    std::vector<ui::PieceReportDialog::DrawnTool> drawn = {toolThat("alto", false, true)};
    ui::PieceReportDialog dialog(plainReport(), QStringLiteral("una imagen"), nullptr,
                                 nullptr, drawn);

    const QString verdict = visibleTextWith(dialog, QStringLiteral("CUMPLE"));
    std::printf("  [medir] veredicto: «%s»\n", verdict.toStdString().c_str());
    ASSERT_FALSE(verdict.isEmpty()) << "una cota fuera de tolerancia no dice que no cumple";
    EXPECT_TRUE(verdict.contains(QStringLiteral("NO CUMPLE")));
    // Y DE DÓNDE SALE: los límites que no cumplió.
    EXPECT_TRUE(verdict.contains(QStringLiteral("90")) &&
                verdict.contains(QStringLiteral("110")))
        << "dice que no cumple pero no entre qué valores se admite: " << verdict.toStdString();
}

TEST(ReportTabs, WithoutToolsTheTabExplainsItselfInsteadOfBeingEmpty) {
    // Una pestaña vacía deja al operador pensando que algo falló. Aquí dice qué
    // hacer para que haya algo.
    ui::PieceReportDialog dialog(plainReport(), QStringLiteral("una imagen"), nullptr,
                                 nullptr, {});
    auto* tabs = tabsOf(dialog);
    ASSERT_NE(tabs, nullptr);
    ASSERT_EQ(tabs->count(), 2) << "la pestaña desaparece cuando no hay herramientas: "
                                   "entonces nadie sabe que existe";

    bool explains = false;
    for (auto* label : tabs->widget(1)->findChildren<QLabel*>()) {
        if (label->text().contains(QStringLiteral("No hay ninguna herramienta"))) {
            explains = true;
            std::printf("  [medir] sin herramientas: «%s»\n",
                        label->text().left(60).toStdString().c_str());
        }
    }
    EXPECT_TRUE(explains) << "la pestaña vacía no dice por qué está vacía";
}

// «SE DUPLICARON LAS HERRAMIENTAS».
//
// Queja de uso, y era exacta. El botón «Vigilar las marcadas» se llevaba TODAS
// las propuestas sin mirar nada — y los nombres que genera el proponedor son
// deterministas: «Ø», «Largo total», «Lado 1», «Espesor 2»… Así que pulsarlo
// dos veces sobre la misma pieza añadía una segunda copia de cada cota.
//
// Encima el botón decía «vigilar las MARCADAS» cuando no había nada que marcar:
// prometía una elección que no existía.
TEST(ReportTabs, WatchingDoesNotAddACotaYouAlreadyHave) {
    inspection::PieceReport report = plainReport();
    // Dos propuestas, una de ellas con el mismo nombre que una herramienta que
    // el operador ya tiene dibujada.
    inspection::AutoProposal already;
    already.config.name = "Largo total";
    already.config.type = inspection::ToolType::Ruler;
    inspection::AutoProposal fresh;
    fresh.config.name = "Ø";
    fresh.config.type = inspection::ToolType::Circle;
    report.watchable = {already, fresh};

    auto existing = toolThat("Largo total", true, true);
    ui::PieceReportDialog dialog(report, QStringLiteral("una imagen"), nullptr, nullptr,
                                 {existing});

    auto* watch = dialog.findChild<QAbstractButton*>(QStringLiteral("watchButton"));
    ASSERT_NE(watch, nullptr) << "no está el botón de vigilar";
    watch->click();

    const auto added = dialog.toWatch();
    std::printf("  [medir] propuestas 2, ya tenía 1 -> se añaden %zu\n", added.size());
    ASSERT_EQ(added.size(), 1U)
        << "vuelve a añadir una cota que el operador ya tiene: eso es lo que duplicaba "
           "las herramientas al pulsar dos veces";
    EXPECT_EQ(added.front().config.name, "Ø")
        << "añade la que ya estaba en vez de la que faltaba";
}

TEST(ReportTabs, WhenEverythingIsAlreadyThereItSaysSoInsteadOfClosing) {
    // Cerrar sin añadir nada y sin decir por qué se lee como que sí se añadió.
    inspection::PieceReport report = plainReport();
    inspection::AutoProposal already;
    already.config.name = "Largo total";
    report.watchable = {already};

    ui::PieceReportDialog dialog(report, QStringLiteral("una imagen"), nullptr, nullptr,
                                 {toolThat("Largo total", true, true)});
    auto* watch = dialog.findChild<QAbstractButton*>(QStringLiteral("watchButton"));
    ASSERT_NE(watch, nullptr);
    watch->click();

    EXPECT_TRUE(dialog.toWatch().empty());
    // Y sigue abierto, con el motivo a la vista. La línea de estado se localiza
    // por su nombre y se comprueba lo que DICE: buscarla por «ya tienes» hacía
    // que, el día que la frase se reescriba, el test dijera «no dice por qué»
    // justo cuando sí lo dice.
    auto* said = dialog.findChild<QLabel*>(QStringLiteral("watchStatus"));
    ASSERT_NE(said, nullptr) << "el diálogo no tiene línea de estado";
    std::printf("  [medir] todo repetido: «%s»\n", said->text().toStdString().c_str());
    EXPECT_TRUE(said->text().contains(QStringLiteral("ya tienes")))
        << "no añade nada y no dice por qué: se lee como que sí lo hizo. Dice: «"
        << said->text().toStdString() << "»";
}

// LOS DOS NIVELES: LA HERRAMIENTA Y TODO LO QUE SU FIGURA PUEDE MEDIR.
//
// Petición de uso: «que hubiera como dos partes en lo de herramientas, una de
// la herramienta en general y otra de todas las secciones-medidas de esa
// herramienta».
//
// El hueco: cinco clases eligen UNA medida al dibujarse —la Región entre seis—
// y las otras cinco quedaban invisibles aunque salen de la misma figura. Para
// ver el perímetro de la región que ya tenías había que dibujar otra encima.
namespace {

ui::PieceReportDialog::DrawnTool regionThatAlsoMeasures() {
    auto tool = toolThat("Zona del taladro", true, true);
    tool.alsoMeasures = {
        {"Área", 0, "1200.00 px²", true},
        {"Perímetro", 1, "140.00 px", false},
        {"Solidez", 2, "0.97", false},
        {"Circularidad", 3, "0.77", false},
    };
    return tool;
}

}  // namespace

TEST(ReportTabs, AToolOpensIntoEverythingItsFigureCanMeasure) {
    ui::PieceReportDialog dialog(plainReport(), QStringLiteral("una imagen"), nullptr,
                                 nullptr, {regionThatAlsoMeasures()});

    auto trees = dialog.findChildren<QTreeWidget*>();
    ASSERT_FALSE(trees.isEmpty()) << "la pestaña de herramientas no tiene dos niveles";
    QTreeWidget* tree = trees.first();
    QTreeWidgetItem* tool = rowNamed(tree, QStringLiteral("Zona del taladro"));
    ASSERT_NE(tool, nullptr) << "no aparece la herramienta en la pestaña";
    ASSERT_EQ(tool->childCount(), 4)
        << "la herramienta no se abre en todo lo que su figura mide";

    // CADA MEDIDA CON SU VALOR, no una lista de nombres: sin el número no hay
    // con qué decidir cuál merece vigilarse, que es para lo que está.
    for (int i = 0; i < tool->childCount(); ++i) {
        EXPECT_FALSE(tool->child(i)->text(2).isEmpty())
            << "la medida «" << tool->child(i)->text(1).toStdString() << "» no dice cuánto";
    }
    std::printf("  [medir] «%s» se abre en %d medidas; la 2ª es «%s» = %s\n",
                tool->text(1).toStdString().c_str(), tool->childCount(),
                tool->child(1)->text(1).toStdString().c_str(),
                tool->child(1)->text(2).toStdString().c_str());

    // LA QUE YA MIDE SE DISTINGUE Y NO LLEVA INTERRUPTOR PROPIO: su interruptor
    // es el de la herramienta. Dos casillas para el mismo hecho obligarían a que
    // una de las dos mintiera.
    EXPECT_EQ(tree->itemWidget(tool->child(0), 0), nullptr)
        << "la medida que la herramienta ya hace tiene un segundo interruptor";
    EXPECT_NE(tree->itemWidget(tool->child(1), 0), nullptr)
        << "una medida hermana no se puede marcar para vigilarla";
    EXPECT_TRUE(tool->child(0)->text(3).contains(QStringLiteral("mide esta herramienta")))
        << "no se distingue cuál de las cuatro es la que la herramienta mide";
}

TEST(ReportTabs, MarkingASiblingMeasureAsksForItWithoutTouchingTheTool) {
    ui::PieceReportDialog dialog(plainReport(), QStringLiteral("una imagen"), nullptr,
                                 nullptr, {regionThatAlsoMeasures()});
    auto* tree = dialog.findChildren<QTreeWidget*>().first();
    QTreeWidgetItem* tool = rowNamed(tree, QStringLiteral("Zona del taladro"));
    ASSERT_NE(tool, nullptr);

    EXPECT_TRUE(dialog.measuresToAdd().empty()) << "pide cotas que nadie marcó";

    // Se marca «Solidez», la tercera de la lista y la segunda con interruptor.
    auto* box = qobject_cast<QCheckBox*>(tree->itemWidget(tool->child(2), 0));
    ASSERT_NE(box, nullptr);
    box->setChecked(true);

    const auto wanted = dialog.measuresToAdd();
    ASSERT_EQ(wanted.size(), 1U);
    EXPECT_EQ(wanted.front().label, "Solidez");
    EXPECT_EQ(wanted.front().measureValue, 2) << "pediría otra medida distinta de la marcada";
    EXPECT_EQ(wanted.front().fromTool, 0);
    std::printf("  [medir] marcada «%s» (valor %d) sobre la herramienta %d\n",
                wanted.front().label.c_str(), wanted.front().measureValue,
                wanted.front().fromTool);

    // Y NO HA TOCADO LA HERRAMIENTA: añadir una medida hermana no es apagar ni
    // cambiar la cota que ya existía.
    EXPECT_TRUE(dialog.toolsWithChangedState().empty())
        << "marcar una medida nueva cambia de paso el estado de la herramienta";
}

TEST(ReportTabs, AToolWithASingleMeasureDoesNotPretendToHaveMore) {
    // Un calibre mide una distancia y nada más. Abrirlo en un solo hijo que
    // repite al padre es ruido que hace dudar de si falta algo.
    ui::PieceReportDialog dialog(plainReport(), QStringLiteral("una imagen"), nullptr,
                                 nullptr, {toolThat("ancho", true, true)});
    auto* tree = dialog.findChildren<QTreeWidget*>().first();
    QTreeWidgetItem* tool = rowNamed(tree, QStringLiteral("ancho"));
    ASSERT_NE(tool, nullptr);
    EXPECT_EQ(tool->childCount(), 0);
    EXPECT_FALSE(tool->isExpanded());
}
