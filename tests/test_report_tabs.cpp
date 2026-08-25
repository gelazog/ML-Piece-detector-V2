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

    QString verdict;
    for (auto* table : dialog.findChildren<QTableWidget*>()) {
        for (int row = 0; row < table->rowCount(); ++row) {
            for (int column = 0; column < table->columnCount(); ++column) {
                if (auto* item = table->item(row, column); item != nullptr) {
                    if (item->text().contains(QStringLiteral("CUMPLE"),
                                              Qt::CaseInsensitive)) {
                        verdict = item->text();
                    }
                }
            }
        }
    }
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

    QAbstractButton* watch = nullptr;
    for (auto* candidate : dialog.findChildren<QAbstractButton*>()) {
        if (candidate->text().contains(QStringLiteral("Vigilar"))) {
            watch = candidate;
        }
    }
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
    QAbstractButton* watch = nullptr;
    for (auto* candidate : dialog.findChildren<QAbstractButton*>()) {
        if (candidate->text().contains(QStringLiteral("Vigilar"))) {
            watch = candidate;
        }
    }
    ASSERT_NE(watch, nullptr);
    watch->click();

    EXPECT_TRUE(dialog.toWatch().empty());
    // Y sigue abierto, con el motivo a la vista.
    QString said;
    for (auto* label : dialog.findChildren<QLabel*>()) {
        if (label->text().contains(QStringLiteral("ya tienes"))) {
            said = label->text();
        }
    }
    std::printf("  [medir] todo repetido: «%s»\n", said.toStdString().c_str());
    EXPECT_FALSE(said.isEmpty())
        << "no añade nada y no dice por qué: se lee como que sí lo hizo";
}
