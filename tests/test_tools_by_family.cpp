// «MIS HERRAMIENTAS», AGRUPADAS POR LA CLASE QUE MIDE.
//
// Petición de uso: «que las separases por la herramienta en cuestión que se está
// usando, y luego que se desglose todas las veces que se usó esa herramienta».
//
// El hueco era real: doce cotas seguidas con los nombres que genera el
// proponedor —«Lado 1», «Lado 2», «Radio 3»— se leen como una lista plana donde
// no se ve QUÉ clase de medida domina la pieza. Agrupadas, «Calibre (7)» lo dice
// de un vistazo.
//
// La pestaña queda en tres niveles: la clase, cada uso de esa clase, y todo lo
// que la figura de ese uso puede medir.

#include <gtest/gtest.h>

#include <QApplication>
#include <QCheckBox>
#include <QTreeWidget>

#include <cstdio>
#include <vector>

#include "ui/piece_report_dialog.h"

using namespace pci;

namespace {

inspection::PieceReport plainReport() {
    inspection::PieceReport report;
    report.ok = true;
    report.headline = "Rectángulo";
    return report;
}

ui::PieceReportDialog::DrawnTool toolOfType(inspection::ToolType type, const char* name,
                                            bool ok) {
    ui::PieceReportDialog::DrawnTool tool;
    tool.config.id = 1;
    tool.config.type = type;
    tool.config.name = name;
    tool.config.toleranceMin = 90.0;
    tool.config.toleranceMax = 110.0;
    tool.config.enabled = true;
    tool.result.name = name;
    tool.result.ok = ok;
    tool.result.measured = ok ? 100.0 : 140.0;
    tool.text = ok ? "100.00 px" : "140.00 px";
    return tool;
}

QTreeWidget* treeOf(const ui::PieceReportDialog& dialog) {
    const auto found = dialog.findChildren<QTreeWidget*>();
    return found.isEmpty() ? nullptr : found.first();
}

}  // namespace

TEST(ToolsByFamily, TheSameToolUsedSeveralTimesHangsFromOneHeading) {
    std::vector<ui::PieceReportDialog::DrawnTool> drawn{
        toolOfType(inspection::ToolType::Caliper, "ancho", true),
        toolOfType(inspection::ToolType::Circle, "Ø", true),
        toolOfType(inspection::ToolType::Caliper, "alto", true),
        toolOfType(inspection::ToolType::Caliper, "fondo", true),
    };
    ui::PieceReportDialog dialog(plainReport(), QStringLiteral("una imagen"), nullptr,
                                 nullptr, drawn);
    auto* tree = treeOf(dialog);
    ASSERT_NE(tree, nullptr) << "la pestaña de herramientas no tiene árbol";

    // DOS clases, no cuatro filas sueltas.
    ASSERT_EQ(tree->topLevelItemCount(), 2)
        << "las cuatro cotas siguen en una lista plana en vez de agrupadas por clase";

    QTreeWidgetItem* first = tree->topLevelItem(0);
    std::printf("  [familias] «%s» con %d usos; «%s» con %d\n",
                first->text(1).toStdString().c_str(), first->childCount(),
                tree->topLevelItem(1)->text(1).toStdString().c_str(),
                tree->topLevelItem(1)->childCount());

    // El orden es el de la PRIMERA aparición: el calibre se dibujó antes.
    EXPECT_EQ(first->childCount(), 3) << "los tres calibres no cuelgan de la misma clase";
    EXPECT_TRUE(first->text(1).contains(QStringLiteral("(3)")))
        << "la fila de la clase no dice cuántas veces se ha usado: " << first->text(1).toStdString();
    EXPECT_EQ(tree->topLevelItem(1)->childCount(), 1);
}

TEST(ToolsByFamily, TheHeadingSaysHowManyOfItsCotasFail) {
    // Agrupar esconde las filas de dentro. Si la fila de la clase no dijera que
    // hay algo rojo debajo, habría que abrir las siete para saberlo — justo el
    // trabajo que agrupar tenía que ahorrar.
    std::vector<ui::PieceReportDialog::DrawnTool> drawn{
        toolOfType(inspection::ToolType::Caliper, "ancho", true),
        toolOfType(inspection::ToolType::Caliper, "alto", false),
        toolOfType(inspection::ToolType::Caliper, "fondo", false),
    };
    ui::PieceReportDialog dialog(plainReport(), QStringLiteral("una imagen"), nullptr,
                                 nullptr, drawn);
    auto* tree = treeOf(dialog);
    ASSERT_NE(tree, nullptr);
    ASSERT_EQ(tree->topLevelItemCount(), 1);
    const QString summary = tree->topLevelItem(0)->text(3);
    std::printf("  [familias] resumen de la clase: «%s»\n", summary.toStdString().c_str());
    EXPECT_TRUE(summary.contains(QStringLiteral("2")))
        << "la clase no dice cuántas de sus cotas no cumplen: " << summary.toStdString();
    EXPECT_TRUE(summary.contains(QStringLiteral("CUMPLE"), Qt::CaseInsensitive));
    // Y se abre sola cuando hay algo que mirar dentro.
    EXPECT_TRUE(tree->topLevelItem(0)->isExpanded())
        << "hay cotas que no cumplen y la clase sale cerrada: el rojo queda escondido";
}

TEST(ToolsByFamily, EachUseStillOpensIntoWhatItsFigureCanMeasure) {
    // El tercer nivel no se pierde al agrupar: sigue estando bajo cada uso.
    auto region = toolOfType(inspection::ToolType::Region, "Zona del taladro", true);
    region.alsoMeasures = {
        {"Área", 0, "1200.00 px²", true},
        {"Perímetro", 1, "140.00 px", false},
    };
    ui::PieceReportDialog dialog(plainReport(), QStringLiteral("una imagen"), nullptr,
                                 nullptr, {region});
    auto* tree = treeOf(dialog);
    ASSERT_NE(tree, nullptr);
    ASSERT_EQ(tree->topLevelItemCount(), 1);
    QTreeWidgetItem* family = tree->topLevelItem(0);
    ASSERT_EQ(family->childCount(), 1) << "no está el uso bajo su clase";
    QTreeWidgetItem* use = family->child(0);
    ASSERT_EQ(use->childCount(), 2)
        << "el uso ya no se abre en lo que su figura mide: se perdió el tercer nivel";
    std::printf("  [familias] «%s» -> «%s» -> «%s», «%s»\n",
                family->text(1).toStdString().c_str(), use->text(1).toStdString().c_str(),
                use->child(0)->text(1).toStdString().c_str(),
                use->child(1)->text(1).toStdString().c_str());
    // La que ya mide sigue sin interruptor propio; la otra sí lo tiene.
    EXPECT_EQ(tree->itemWidget(use->child(0), 0), nullptr);
    EXPECT_NE(tree->itemWidget(use->child(1), 0), nullptr);
}

TEST(ToolsByFamily, TheSwitchesStillLineUpWithTheirTools) {
    // Al agrupar, las casillas se crean en otro orden que el vector de origen.
    // Si `toolsWithChangedState` siguiera emparejando por posición, apagar una
    // cota apagaría OTRA — y en silencio.
    std::vector<ui::PieceReportDialog::DrawnTool> drawn{
        toolOfType(inspection::ToolType::Caliper, "ancho", true),
        toolOfType(inspection::ToolType::Circle, "Ø", true),
        toolOfType(inspection::ToolType::Caliper, "alto", true),
    };
    ui::PieceReportDialog dialog(plainReport(), QStringLiteral("una imagen"), nullptr,
                                 nullptr, drawn);
    auto* tree = treeOf(dialog);
    ASSERT_NE(tree, nullptr);

    // Se apaga «Ø», que en el árbol está en la segunda clase y en el vector de
    // origen en la posición 1.
    QTreeWidgetItem* circles = tree->topLevelItem(1);
    ASSERT_EQ(circles->childCount(), 1);
    auto* box = qobject_cast<QCheckBox*>(tree->itemWidget(circles->child(0), 0));
    ASSERT_NE(box, nullptr);
    box->setChecked(false);

    const auto changed = dialog.toolsWithChangedState();
    ASSERT_EQ(changed.size(), 1U);
    std::printf("  [familias] apagada «%s» -> se guarda «%s»\n",
                circles->child(0)->text(1).toStdString().c_str(),
                changed.front().name.c_str());
    EXPECT_EQ(changed.front().name, "Ø")
        << "apagar una cota apaga otra distinta: las casillas y las herramientas "
           "dejaron de ir emparejadas al agrupar";
}
