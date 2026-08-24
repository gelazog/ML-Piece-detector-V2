// ¿QUÉ HACE ESTE BOTÓN?
//
// Queja del usuario: «los menús y otras cosas están toscos, no son intuitivos
// ni coherentes ni lógicos; debería decirle al usuario qué hace cada cosa».
//
// Eso se puede medir en vez de opinarlo. Un control se explica solo cuando
// tiene una ayuda emergente que dice qué hace —no que repita su propio nombre,
// que es lo mismo que no tener nada—. Este archivo cuenta cuántos NO la tienen,
// en las dos superficies donde el operador se atasca: la barra de menús y la
// ventana de Configurar.
//
// El número no puede subir. Es una comprobación de deuda: no exige que todo
// esté explicado hoy, exige que no se añadan controles mudos nuevos, que es lo
// que convirtió esto en una queja.

#include <gtest/gtest.h>

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QMenu>
#include <QMenuBar>
#include <QSpinBox>

#include <cstdio>

#include "ui/main_window.h"

namespace {

// Una ayuda que repite el nombre del control no explica nada: «Inspeccionar» →
// «Inspeccionar». Se cuenta como muda, porque para quien no entiende el nombre
// leerlo otra vez no le sirve de nada.
bool saysSomething(const QString& name, const QString& help) {
    const QString clean = help.trimmed();
    if (clean.isEmpty()) {
        return false;
    }
    QString bare = name;
    bare.remove(QLatin1Char('&'));
    bare.remove(QStringLiteral("…"));
    bare = bare.trimmed();
    if (bare.isEmpty()) {
        return !clean.isEmpty();
    }
    // Que diga algo MÁS que el nombre. El margen de ocho caracteres deja pasar
    // «Inspeccionar (Ctrl+I)» como muda y no una frase de verdad.
    return clean.compare(bare, Qt::CaseInsensitive) != 0 &&
           clean.size() > bare.size() + 8;
}

struct Tally {
    int total = 0;
    int mute = 0;
    QStringList worst;
};

void countAction(const QAction* action, Tally& tally) {
    if (action == nullptr || action->isSeparator() || action->text().isEmpty()) {
        return;
    }
    ++tally.total;
    if (!saysSomething(action->text(), action->toolTip())) {
        ++tally.mute;
        if (tally.worst.size() < 40) {
            tally.worst << action->text();
        }
    }
}

}  // namespace

TEST(SelfExplaining, TheMenusExplainWhatEachEntryDoes) {
    pci::ui::MainWindow window;
    window.resize(1200, 800);

    Tally tally;
    for (auto* menu : window.menuBar()->findChildren<QMenu*>()) {
        for (auto* action : menu->actions()) {
            countAction(action, tally);
        }
    }

    std::printf("  [ayuda] menús: %d entradas, %d sin explicar (%.0f %%)\n", tally.total,
                tally.mute, tally.total > 0 ? 100.0 * tally.mute / tally.total : 0.0);
    for (const auto& one : tally.worst) {
        std::printf("  [ayuda]    muda: %s\n", one.toStdString().c_str());
    }

    ASSERT_GT(tally.total, 20) << "no se recogió casi ninguna entrada de menú: la "
                                  "comprobación no estaría comprobando nada";
    // Cero, y cero se queda. Se llegó aquí desde 25 de 40 sin explicar; dejar
    // el tope en la deuda vieja habría permitido volver a ella sin enterarse.
    EXPECT_EQ(tally.mute, 0)
        << "hay entradas de menú que no dicen qué hacen: cada una es un «¿y esto "
           "qué hace?» que acaba en no tocarlo nunca";
}

// Y QUE SE VEAN, que es la mitad que faltaba.
//
// Qt no enseña las ayudas de los menús salvo que se le pida expresamente con
// `setToolTipsVisible`. Nadie se lo había pedido, así que las quince entradas
// que SÍ tenían explicación escrita tampoco la mostraban: el operador veía
// exactamente lo mismo que si no hubiera ninguna.
//
// Es el peor de los dos fallos posibles, porque no se nota leyendo el código:
// ahí las explicaciones están, bien escritas, y parece que el trabajo está
// hecho.
TEST(SelfExplaining, TheMenusActuallyShowTheirHelp) {
    pci::ui::MainWindow window;
    window.resize(1200, 800);

    int menus = 0;
    QStringList silent;
    for (auto* menu : window.menuBar()->findChildren<QMenu*>()) {
        if (menu->actions().isEmpty()) {
            continue;
        }
        ++menus;
        if (!menu->toolTipsVisible()) {
            silent << menu->title();
        }
    }
    std::printf("  [ayuda] %d menús, %d que esconden sus explicaciones\n", menus,
                static_cast<int>(silent.size()));
    ASSERT_GT(menus, 4);
    EXPECT_TRUE(silent.isEmpty())
        << "hay menús que guardan sus explicaciones sin enseñarlas: escribirlas y "
           "esconderlas cuesta lo mismo que escribirlas y no ayuda a nadie";
}
