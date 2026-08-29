// UN MENÚ ES UNA LISTA HASTA QUE ALGUIEN LA AGRUPA.
//
// Misma historia que la pestaña Detección, que apilaba diecinueve filas de
// formulario seguidas: nadie lo dejó así a propósito, cada entrada se añadió al
// final el día que nació, y meses después es una lista que hay que leer entera
// para encontrar nada.
//
// Aquí la medida es la RACHA: cuántas entradas seguidas hay sin un separador
// que las corte. Una racha larga no es un defecto de estilo — es que el
// operador tiene que recorrer con la vista diez opciones sin ningún punto de
// apoyo, y las opciones de un menú no son diez cosas del mismo tipo.
//
// El número no sale de un ideal de diseño sino del que ya se usó en este
// proyecto para el mismo problema: la pestaña Detección quedó en grupos de 8,
// 4, 4 y 3 filas. Ocho es lo más largo que allí se aceptó, así que ocho es el
// tope aquí.
//
// Esto es un TRINQUETE, como el de los colores escritos a mano: fija la racha
// más larga de hoy y no deja que suba. Bajarla es trabajo, subirla es
// descuido, y sin el trinquete solo pasa lo segundo.

#include <gtest/gtest.h>

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QMenuBar>
#include <QString>

#include <algorithm>
#include <cstdio>
#include <vector>

#include "ui/main_window.h"

using namespace pci;

namespace {

struct MenuRun {
    QString menu;
    int longest = 0;
    int entries = 0;
    int separators = 0;
};

// La racha más larga de entradas seguidas sin separador. Los submenús cuentan
// como UNA entrada del menú padre —que es como se leen— y se miden aparte.
MenuRun measure(QMenu* menu) {
    MenuRun run;
    run.menu = menu->title();
    int current = 0;
    for (auto* action : menu->actions()) {
        if (action->isSeparator()) {
            ++run.separators;
            run.longest = std::max(run.longest, current);
            current = 0;
            continue;
        }
        ++run.entries;
        ++current;
    }
    run.longest = std::max(run.longest, current);
    return run;
}

}  // namespace

TEST(MenuGrouping, NoMenuMakesYouReadTenOptionsWithoutABreak) {
    ui::MainWindow window;
    auto* bar = window.menuBar();
    ASSERT_NE(bar, nullptr);

    std::vector<MenuRun> runs;
    for (auto* action : bar->actions()) {
        if (action->menu() != nullptr) {
            runs.push_back(measure(action->menu()));
            // Y los submenús, que también se leen de una vez.
            for (auto* sub : action->menu()->actions()) {
                if (sub->menu() != nullptr) {
                    MenuRun inner = measure(sub->menu());
                    inner.menu = action->menu()->title() + QStringLiteral(" ▸ ") +
                                 sub->menu()->title();
                    runs.push_back(inner);
                }
            }
        }
    }
    ASSERT_FALSE(runs.empty()) << "no se encuentra ningún menú: esta prueba no está "
                                 "comprobando nada";

    int worst = 0;
    QString worstMenu;
    for (const auto& run : runs) {
        std::printf("  [menú] %-28s %2d entradas, %d separadores, racha %2d\n",
                    run.menu.toStdString().c_str(), run.entries, run.separators,
                    run.longest);
        if (run.longest > worst) {
            worst = run.longest;
            worstMenu = run.menu;
        }
    }
    std::printf("  [menú] la racha más larga: %d en «%s»\n", worst,
                worstMenu.toStdString().c_str());

    // EL TOPE ES EL DE HOY, y hoy es 5. Nació en 8 —el mismo que se aceptó en la
    // pestaña Detección— y bajó al reordenar la barra: «Archivo» y «Fuente» se
    // fundieron en «Configurar», las calibraciones se fueron con ellas, y «Ver»
    // separó los paneles de lo que se pinta sobre la imagen.
    //
    // Las dos rachas de 5 que quedan son listas de una sola cosa —las cinco
    // unidades de medida, los cinco orígenes del tablero— y ahí un separador no
    // agruparía nada: son cinco opciones excluyentes de la misma pregunta.
    constexpr int kToday = 5;
    EXPECT_LE(worst, kToday)
        << "«" << worstMenu.toStdString() << "» hace leer " << worst
        << " opciones seguidas sin un separador. No es estilo: son cosas de tipos "
           "distintos puestas en fila, y el operador tiene que recorrerlas todas "
           "para encontrar una.";
    if (worst < kToday) {
        ADD_FAILURE() << "la racha más larga ya es " << worst << " y el tope sigue en "
                      << kToday << ": bájalo a " << worst
                      << " para que no pueda volver a subir. Un trinquete que no se "
                         "aprieta deja de serlo.";
    }
}
