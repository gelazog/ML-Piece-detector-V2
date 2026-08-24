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
#include <QDialog>
#include <QDoubleSpinBox>
#include <QMenu>
#include <QMenuBar>
#include <QSpinBox>

#include <cstdio>

#include "ui/configure_dialog.h"
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

// LA VENTANA DE CONFIGURAR, QUE ES DONDE MÁS DUELE.
//
// El usuario la nombró aparte: «el menú de configuración tiene bastantes fallos
// en sus características y configuraciones». Es la ventana donde hay más
// controles por centímetro y donde equivocarse cuesta más caro —un umbral mal
// puesto no da un error, da inspecciones mal juzgadas—, así que es donde menos
// se puede permitir un control que no dice qué hace.
//
// Se cuentan casillas, botones, listas y campos numéricos. Un control sin ayuda
// se lista con su nombre para poder ir a arreglarlo.
TEST(SelfExplaining, TheConfigureWindowExplainsItsControls) {
    // Se construye la ventana directamente, SIN `exec()`. Abrirla por su acción
    // de menú la pondría modal, y una modal sin pantalla bloquea para siempre:
    // ya costó una vez cinco minutos de banco colgado hasta matar el proceso.
    // Construir un QDialog no bloquea; solo `exec()` lo hace.
    pci::ui::ConfigureDialog::Inputs inputs;
    pci::ui::ConfigureDialog built(inputs, nullptr);
    built.resize(900, 700);
    QDialog* dialog = &built;

    int total = 0;
    int mute = 0;
    QStringList worst;
    // SE CUENTAN TODOS, TENGAN NOMBRE O NO.
    //
    // La primera versión se saltaba los controles sin nombre —y los campos
    // numéricos casi nunca tienen `accessibleName`, su rótulo lo pone el
    // formulario al lado—. O sea que se saltaba justo los controles donde
    // equivocarse cuesta más caro: un umbral, un área mínima, una tolerancia.
    // Decía «18 controles, 0 sin explicar» sin haber mirado ninguno de ellos.
    const auto check = [&](const QWidget* widget, const QString& name) {
        ++total;
        const bool named = !name.trimmed().isEmpty();
        const bool ok = named ? saysSomething(name, widget->toolTip())
                              : !widget->toolTip().trimmed().isEmpty();
        if (!ok) {
            ++mute;
            if (worst.size() < 60) {
                const QWidget* page = widget->parentWidget();
                worst << (named ? name
                                : QStringLiteral("(sin rótulo) ") +
                                      QString::fromLatin1(widget->metaObject()->className()) +
                                      QStringLiteral(" en ") +
                                      (page != nullptr
                                           ? QString::fromLatin1(page->metaObject()->className())
                                           : QStringLiteral("?")));
            }
        }
    };
    for (auto* button : dialog->findChildren<QAbstractButton*>()) {
        // Los rotulados vacíos son separadores y adornos, no controles.
        if (!button->text().trimmed().isEmpty()) {
            check(button, button->text());
        }
    }
    for (auto* box : dialog->findChildren<QSpinBox*>()) {
        check(box, box->accessibleName());
    }
    for (auto* box : dialog->findChildren<QDoubleSpinBox*>()) {
        check(box, box->accessibleName());
    }
    for (auto* combo : dialog->findChildren<QComboBox*>()) {
        check(combo, combo->accessibleName());
    }

    std::printf("  [ayuda] Configurar: %d controles, %d sin explicar\n", total, mute);
    for (const auto& one : worst) {
        std::printf("  [ayuda]    muda: %s\n", one.toStdString().c_str());
    }
    ASSERT_GT(total, 20) << "no se recogió casi ningún control: la comprobación no "
                            "estaría comprobando nada";
    EXPECT_EQ(mute, 0)
        << "hay controles de Configurar que no dicen qué hacen. Aquí equivocarse "
           "no da un error: da inspecciones mal juzgadas, que es peor";
}
