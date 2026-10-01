// EL TAMAÑO Y LAS AYUDAS DEL PINCEL, EN UN SOLO SUBMENÚ «PINCEL».
//
// Estaban sueltos en el desplegable de «Corregir borde», mezclados con las
// órdenes: catorce entradas seguidas, una de ellas un rótulo falso («Ayuda del
// pincel», una acción apagada haciendo de título). Pintar, rodear y deshacer se
// usan en cada corrección; el tamaño y las ayudas se ajustan una vez. Y como
// ese botón está apagado sin imagen quieta, con la cámara en marcha no había
// forma de dejarlos preparados ni de llegar a ellos desde la barra de menús.
//
// Ahora son un submenú «Pincel», el MISMO objeto colgado del botón y del menú
// «Medida». La prueba vigila las dos mitades del encargo: que esté en los dos
// sitios, y que no haya copias — dos acciones «Trazo recto» acabarían diciendo
// cosas distintas del mismo ajuste.

#include <gtest/gtest.h>

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QMenuBar>
#include <QSlider>
#include <QToolButton>

#include <cstdio>

#include "ui/main_window.h"

using namespace pci;

namespace {

QMenu* brushSubmenuIn(QMenu* menu) {
    for (auto* action : menu->actions()) {
        if (action->menu() != nullptr &&
            action->menu()->objectName() == QStringLiteral("brushOptionsMenu")) {
            return action->menu();
        }
    }
    return nullptr;
}

int entriesOf(const QMenu* menu) {
    int entries = 0;
    for (auto* action : menu->actions()) {
        if (!action->isSeparator()) {
            ++entries;
        }
    }
    return entries;
}

}  // namespace

TEST(BrushMenu, TheSameSubmenuHangsFromTheButtonAndFromTheMenuBar) {
    ui::MainWindow window;
    window.resize(1200, 800);

    auto* button = window.findChild<QToolButton*>(QStringLiteral("edgeBrushButton"));
    ASSERT_NE(button, nullptr);
    ASSERT_NE(button->menu(), nullptr);
    QMenu* fromButton = brushSubmenuIn(button->menu());
    ASSERT_NE(fromButton, nullptr) << "«Corregir borde» no tiene el submenú «Pincel»";

    QMenu* fromBar = nullptr;
    QString where;
    for (auto* top : window.menuBar()->actions()) {
        if (top->menu() != nullptr) {
            if (QMenu* found = brushSubmenuIn(top->menu())) {
                fromBar = found;
                where = top->menu()->title();
            }
        }
    }
    ASSERT_NE(fromBar, nullptr) << "la barra de menús no llega a las opciones del pincel";
    std::printf("  [pincel] en la barra: «%s ▸ %s»\n", where.toStdString().c_str(),
                fromBar->title().toStdString().c_str());
    EXPECT_EQ(fromBar, fromButton) << "el submenú de la barra es una copia del del botón";

    // Dentro, las mismas acciones que ya recordaban su estado, y el deslizador.
    for (const char* name : {"brushSteadyAction", "brushStraightAction", "brushSnapAction"}) {
        auto* action = window.findChild<QAction*>(QString::fromLatin1(name));
        ASSERT_NE(action, nullptr) << name;
        EXPECT_TRUE(fromBar->actions().contains(action))
            << name << " no está en el submenú «Pincel»";
    }
    auto* slider = window.findChild<QSlider*>(QStringLiteral("brushSizeSlider"));
    ASSERT_NE(slider, nullptr);
    EXPECT_TRUE(fromBar->isAncestorOf(slider)) << "el tamaño no está en «Pincel»";
}

TEST(BrushMenu, NoOptionIsDuplicatedAndTheButtonMenuGotShorter) {
    ui::MainWindow window;
    int straight = 0;
    for (auto* action : window.findChildren<QAction*>()) {
        if (action->text() == QStringLiteral("Trazo recto")) {
            ++straight;
        }
    }
    EXPECT_EQ(straight, 1) << "hay " << straight << " acciones «Trazo recto»";
    EXPECT_EQ(window.findChildren<QSlider*>(QStringLiteral("brushSizeSlider")).size(), 1);

    auto* button = window.findChild<QToolButton*>(QStringLiteral("edgeBrushButton"));
    ASSERT_NE(button, nullptr);
    const int entries = entriesOf(button->menu());
    std::printf("  [pincel] «Corregir borde»: %d entradas (antes 14)\n", entries);
    EXPECT_LE(entries, 9) << "las opciones del pincel vuelven a estar sueltas en el botón";
    for (auto* action : button->menu()->actions()) {
        EXPECT_FALSE(!action->isSeparator() && action->menu() == nullptr &&
                     !action->isEnabled() && action->text().startsWith(QStringLiteral("Ayuda")))
            << "vuelve el rótulo falso hecho con una acción apagada";
    }
}
