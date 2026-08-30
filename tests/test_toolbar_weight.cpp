// TRECE BOTONES DEL MISMO PESO NO SON TRECE OPCIONES: SON UNA PARED.
//
// Queja de uso: «estás saturando de opciones… son cuestiones de diseño».
//
// La barra de la ventana principal es lo primero que se ve y lo que se usa cien
// veces al día. Si todos sus controles tienen el mismo aspecto, el ojo no puede
// separar «lo que hago siempre» de «lo que hago una vez al mes», y encontrar
// «Inspeccionar» cuesta lo mismo que encontrar «Registrar pieza».
//
// El proyecto ya tiene la mitad de la regla puesta y comprobada: `OnlyOneActionIsEmphasised`
// exige que haya UNO destacado y sólo uno. Lo que faltaba es la otra mitad, que
// es la que se nota con la barra llena: **cuántos controles compiten** por esa
// mirada.
//
// Esta prueba no rediseña nada. Fija el número de HOY como trinquete, con la
// lista impresa: la barra puede adelgazar, no engordar. Cada control nuevo tiene
// que quitar otro o justificar por qué merece estar en la fila que se mira cien
// veces al día en vez de en un menú.

#include <gtest/gtest.h>

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QString>
#include <QStringList>
#include <QToolButton>

#include <cstdio>

#include "ui/main_window.h"

using namespace pci;

TEST(ToolbarWeight, TheTopRowDoesNotGrowWithoutSomeoneDeciding) {
    pci::ui::MainWindow window;
    window.resize(1600, 800);
    window.show();
    QApplication::processEvents();

    // Los controles de la BARRA, no los de los paneles ni los de los diálogos:
    // se distinguen porque están fuera de cualquier dock y por encima del
    // lienzo. Se filtran por su ancestro para no contar la paleta de
    // herramientas, que tiene treinta y dos botones y es otra cosa.
    QStringList visible;
    int withMenu = 0;
    for (auto* button : window.findChildren<QAbstractButton*>()) {
        if (!button->isVisible() || button->text().isEmpty()) {
            continue;
        }
        // La paleta y los paneles acoplables tienen sus propios botones.
        bool insideAPanel = false;
        for (QWidget* parent = button->parentWidget(); parent != nullptr;
             parent = parent->parentWidget()) {
            const QString name = parent->objectName();
            if (name.endsWith(QStringLiteral("Dock")) ||
                name == QStringLiteral("toolPalette")) {
                insideAPanel = true;
                break;
            }
        }
        if (insideAPanel) {
            continue;
        }
        // Un botón de la barra está arriba: por encima del lienzo del vídeo.
        if (button->mapTo(&window, QPoint(0, 0)).y() > 200) {
            continue;
        }
        visible << button->text();
        if (auto* tool = qobject_cast<QToolButton*>(button); tool != nullptr &&
            tool->menu() != nullptr) {
            ++withMenu;
        }
    }

    std::printf("  [barra] %d controles con texto en la fila de arriba (%d con menú):\n",
                static_cast<int>(visible.size()), withMenu);
    for (const QString& text : visible) {
        std::printf("  [barra]    %s\n", text.toStdString().c_str());
    }

    ASSERT_GT(visible.size(), 3)
        << "apenas se ven controles en la barra: esta prueba no está mirando donde cree";

    // EL TOPE ES EL DE HOY. Baja cuando uno se va a un menú; no sube nunca.
    //
    // No es un ideal de diseño: es el número que hay, medido, para que crezca
    // sólo cuando alguien lo decida a propósito. La regla de al lado —uno y sólo
    // uno destacado— no sirve de nada si la fila tiene veinte.
    constexpr int kToday = 13;
    EXPECT_LE(visible.size(), kToday)
        << "la barra ha crecido a " << visible.size()
        << " controles: " << visible.join(QStringLiteral(", ")).toStdString()
        << ". Cada uno compite por la misma mirada que «Inspeccionar»; si el nuevo hace "
           "falta, algo tiene que irse a un menú.";
    if (static_cast<int>(visible.size()) < kToday) {
        ADD_FAILURE() << "la barra ya tiene " << visible.size() << " y el tope sigue en "
                      << kToday << ": bájalo. Un trinquete que no se aprieta deja de serlo.";
    }
}
