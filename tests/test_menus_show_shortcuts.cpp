// UN MENÚ QUE NO ENSEÑA SU ATAJO NO ENSEÑA QUE EXISTE.
//
// Ninguna de las entradas de menú mostraba su tecla, así que la única forma de
// descubrir un atajo era abrir la guía — y a la guía no va quien no sabe que hay
// atajos. El menú es donde se aprenden, en cualquier programa.
//
// EL ARREGLO OBVIO ES EL EQUIVOCADO, y por eso hay prueba. Los 19 atajos ya son
// `QAction` invisibles colgadas de la ventana; las entradas de menú eran
// `QAction` DISTINTAS. Poner la misma tecla en las dos deja dos acciones con la
// misma secuencia en la misma ventana, que es `ambiguousActivate`: Qt no
// dispara ninguna de forma fiable. Este proyecto ya se comió ese fallo con
// Ctrl+1 y Ctrl+2, y desde fuera se vive como «a veces no responde».
//
// Así que la entrada no se crea: se cuelga la que ya existe. Una sola acción, un
// solo atajo, y el menú lo pinta solo.
//
// Las dos mitades se comprueban, y la segunda es la que impide el arreglo malo:
// si alguien «mejora» esto duplicando acciones, la primera seguiría pasando
// —el menú enseñaría la tecla— y la segunda lo cazaría.

#include <gtest/gtest.h>

#include <QAction>
#include <QApplication>
#include <QKeySequence>
#include <QList>
#include <QMenu>
#include <QMenuBar>
#include <QString>

#include <cstdio>
#include <map>

#include "ui/main_window.h"

using namespace pci;

namespace {

// Todas las acciones de la ventana: las de los menús —recorriendo submenús— y
// las que cuelgan de la propia ventana, que es donde viven los atajos.
QList<QAction*> everyAction(QWidget* window) {
    QList<QAction*> all = window->actions();
    for (auto* menu : window->findChildren<QMenu*>()) {
        for (auto* action : menu->actions()) {
            if (!all.contains(action)) {
                all.append(action);
            }
        }
    }
    return all;
}

}  // namespace

TEST(MenusShowShortcuts, TheEntriesThatHaveAKeyShowIt) {
    ui::MainWindow window;

    // Las cinco entradas que hacen exactamente lo mismo que un atajo. No son
    // todas las del menú: la mayoría no tiene tecla y no puede enseñar ninguna.
    // Lo que no puede pasar es que la tenga y no la enseñe.
    const QStringList shouldShowAKey{
        QStringLiteral("Calibrar escala (mm)…"), QStringLiteral("Guardar plantilla"),
        QStringLiteral("Inspeccionar"),          QStringLiteral("Editor de plantilla…"),
        QStringLiteral("Atajos de teclado…"),
    };

    int found = 0;
    for (auto* menu : window.findChildren<QMenu*>()) {
        for (auto* action : menu->actions()) {
            if (!shouldShowAKey.contains(action->text())) {
                continue;
            }
            ++found;
            std::printf("  [menú] %-26s %s\n", action->text().toStdString().c_str(),
                        action->shortcut().toString().toStdString().c_str());
            EXPECT_FALSE(action->shortcut().isEmpty())
                << "«" << action->text().toStdString()
                << "» tiene atajo y el menú no lo enseña, así que la única forma de "
                   "descubrirlo es abrir la guía — y a la guía no va quien no sabe que "
                   "hay atajos";
        }
    }
    EXPECT_EQ(found, shouldShowAKey.size())
        << "no se encuentran las cinco entradas: si se renombraron, hay que actualizar "
           "esta lista — no borrar la prueba";
}

TEST(MenusShowShortcuts, NoTwoActionsClaimTheSameKeyInTheSameWindow) {
    // LA MITAD QUE IMPIDE EL ARREGLO MALO.
    //
    // Con dos acciones que reclaman la misma secuencia, Qt emite
    // `ambiguousActivate` y no dispara ninguna de forma fiable. No da error ni
    // deja rastro: el operador pulsa la tecla y a veces no pasa nada.
    ui::MainWindow window;

    std::map<QString, QStringList> byKey;
    for (auto* action : everyAction(&window)) {
        for (const auto& sequence : action->shortcuts()) {
            if (sequence.isEmpty()) {
                continue;
            }
            byKey[sequence.toString()] << (action->text().isEmpty()
                                               ? QStringLiteral("(sin texto)")
                                               : action->text());
        }
    }

    int shared = 0;
    for (const auto& [key, owners] : byKey) {
        if (owners.size() > 1) {
            ++shared;
            ADD_FAILURE() << "la tecla " << key.toStdString() << " la reclaman "
                          << owners.size() << " acciones ("
                          << owners.join(QStringLiteral(", ")).toStdString()
                          << "): Qt no dispara ninguna de forma fiable";
        }
    }
    std::printf("  [menú] %d secuencias distintas, %d compartidas\n",
                static_cast<int>(byKey.size()), shared);
    // Y que de verdad haya teclas que mirar: sin esto, la prueba pasaría en
    // verde el día que los atajos dejaran de construirse.
    EXPECT_GT(byKey.size(), 10U)
        << "apenas hay atajos registrados: esta prueba no está comprobando nada";
}
