// Las rutas de menú que el README promete tienen que existir en la aplicación.
//
// Este fichero sale de una comprobación deliberada que encontró SEIS
// referencias rotas de una vez. El menú «Cámara» se renombró a «Fuente» y sus
// entradas de medida se movieron a «Medida»; el README se quedó con los nombres
// viejos y siguió mandando al operador a menús que ya no existían:
//
//   *Cámara ▸ Configurar…*        ->  Fuente ▸ Configurar…
//   *Cámara ▸ Calibrar escala*    ->  Medida ▸ Calibrar escala (mm)…
//   *Cámara ▸ Escala por marcador*->  Medida ▸ Escala por marcador ArUco…
//   *Ver ▸ Unidad*                ->  Medida ▸ Unidad de medida
//
// Una cifra obsoleta en un documento técnico despista a quien programa. Una ruta
// de menú obsoleta en el manual manda a quien está midiendo piezas a buscar algo
// que no está, y le hace pensar que el programa está roto.
//
// Aquí no se comprueba la prosa: se leen las rutas del propio README y se
// resuelven contra los menús de verdad. Si alguien renombra un menú, esto falla
// y dice qué frase del manual quedó mintiendo.

#include <gtest/gtest.h>

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QMenuBar>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "ui/main_window.h"

namespace {

QString readmeText() {
    for (const auto* candidate : {"README.md", "../README.md", "../../README.md",
                                  "../../../README.md"}) {
        std::error_code ec;
        if (!std::filesystem::exists(candidate, ec)) {
            continue;
        }
        std::ifstream file(candidate, std::ios::binary);
        std::ostringstream all;
        all << file.rdbuf();
        return QString::fromUtf8(all.str().c_str());
    }
    return {};
}

// Todo lo que el operador puede leer en un menú: títulos de menú, títulos de
// submenú y textos de acción. Se recogen del árbol entero de la ventana, no
// sólo de la barra: la zona de trabajo, por ejemplo, cuelga de un botón.
QSet<QString> everythingReachable(const pci::ui::MainWindow& window) {
    QSet<QString> names;
    const auto clean = [](QString text) {
        text.remove(QLatin1Char('&'));
        // Los atajos van tras un tabulador y no son parte del nombre.
        const int tab = text.indexOf(QLatin1Char('\t'));
        if (tab >= 0) {
            text = text.left(tab);
        }
        return text.trimmed();
    };
    for (auto* menu : window.findChildren<QMenu*>()) {
        names.insert(clean(menu->title()));
        for (auto* action : menu->actions()) {
            if (!action->isSeparator()) {
                names.insert(clean(action->text()));
            }
        }
    }
    for (auto* action : window.findChildren<QAction*>()) {
        if (!action->isSeparator()) {
            names.insert(clean(action->text()));
        }
    }
    // Y los BOTONES, que el operador lee igual que un menú. La zona de trabajo
    // vive en uno: su menú no tiene título, el nombre lo pone el botón. Sin
    // esto, «Zona ▸ Quitar la zona» salía como rota siendo correcta.
    for (auto* button : window.findChildren<QAbstractButton*>()) {
        names.insert(clean(button->text()));
    }
    names.remove(QString());
    return names;
}

// Un nombre del README «existe» si alguna entrada real empieza por él. Se
// admite el prefijo a propósito: el manual dice «Calibrar escala (mm)…» o
// «Tablero de referencia» y la entrada real puede llevar detrás una aclaración
// entre paréntesis. Lo que NO se admite es que no aparezca en absoluto, que es
// el fallo que esto persigue.
bool exists(const QSet<QString>& names, const QString& wanted) {
    const QString target = wanted.trimmed();
    if (target.isEmpty()) {
        return true;
    }
    for (const auto& name : names) {
        if (name.startsWith(target, Qt::CaseInsensitive) ||
            target.startsWith(name, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST(ReadmePaths, EveryMenuPathInTheManualExistsInTheApplication) {
    const QString readme = readmeText();
    if (readme.isEmpty()) {
        GTEST_SKIP() << "no se encontró README.md junto al proyecto";
    }

    pci::ui::MainWindow window;
    window.resize(1200, 800);
    const QSet<QString> names = everythingReachable(window);
    ASSERT_GT(names.size(), 20) << "no se recogió casi ningún nombre de menú: la "
                                   "comprobación no estaría comprobando nada";

    // Las rutas del manual: *Algo ▸ Algo* entre asteriscos.
    static const QRegularExpression pattern(
        QStringLiteral("\\*\\*?([^*\\n]{2,80}?\\x{25b8}[^*\\n]{2,80}?)\\*\\*?"));
    auto matches = pattern.globalMatch(readme);

    int checked = 0;
    QStringList broken;
    while (matches.hasNext()) {
        const QString path = matches.next().captured(1);
        const QStringList parts = path.split(QStringLiteral("▸"));
        ++checked;
        for (const auto& raw : parts) {
            QString part = raw.trimmed();
            // Se quitan los puntos suspensivos, que en Qt van en la acción pero
            // en el manual a veces no, y viceversa.
            part.remove(QStringLiteral("…"));
            part = part.trimmed();
            if (!exists(names, part)) {
                broken << (path + "   (no existe: «" + part + "»)");
                break;
            }
        }
    }

    std::printf("  [manual] %d rutas de menú comprobadas, %d rotas\n", checked,
                static_cast<int>(broken.size()));
    for (const auto& one : broken) {
        std::printf("  [manual]    %s\n", one.toStdString().c_str());
    }

    EXPECT_GT(checked, 5) << "no se encontró ninguna ruta en el manual: o cambió el "
                             "formato, o la expresión que las busca dejó de valer";
    EXPECT_TRUE(broken.isEmpty())
        << "el manual manda al operador a sitios que no existen. Con una ruta rota, "
           "quien la sigue concluye que el programa está roto.";
}
