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
    // LOS PUNTOS SUSPENSIVOS SE QUITAN DE LOS DOS LADOS.
    //
    // El menú dice «Configurar…» y el manual a veces «Configurar…» y a veces
    // «Configurar». Si solo se limpiaba el manual, diez rutas correctas salían
    // como rotas y había que aflojar la comparación para que pasaran — que es
    // justo como se llegó a la versión que no comprobaba nada.
    for (const auto& raw : names) {
        QString name = raw;
        name.remove(QStringLiteral("…"));
        name = name.trimmed();
        // LA COMPARACIÓN VA EN UN SOLO SENTIDO, y esto no es un detalle.
        //
        // Antes valía también `target.startsWith(name)`: que el nombre de una
        // acción fuera prefijo de la ruta del manual. Con nombres cortos como
        // «Piezas» o «Ver» en el menú, ESO DA POR BUENA CUALQUIER RUTA que
        // empiece por esa palabra. Se destapó mutando: se cambió el nombre de
        // una entrada recién añadida a algo completamente distinto y la
        // comprobación siguió en verde — 21 rutas «comprobadas» y ninguna
        // comprobada de verdad. Con ese cambio se destaparon además una ruta
        // rota de verdad en el manual y una línea suelta que llevaba ahí tiempo.
        //
        // El sentido que queda —el nombre real EMPIEZA por lo que dice el
        // manual— es el que hace falta para las abreviaturas legítimas:
        // «Tablero de referencia» por «Tablero de referencia (centro = 0)».
        if (name.startsWith(target, Qt::CaseInsensitive)) {
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
        // Una ruta puede venir PARTIDA por un salto de linea: el manual se reajusta
        // al editarlo, y nada impide que «Pieza ▸ Registrar otro acabado» acabe con
        // el salto en medio. La primera version excluia el salto, asi que una ruta
        // partida NO se comprobaba — y no fallaba: pasaba desapercibida, que es
        // peor. Se destapo al añadir una ruta nueva y ver que el recuento no subia.
        QStringLiteral("\\*\\*?([^*\\n]{1,80}?\\x{25b8}[^*]{2,80}?)\\*\\*?"));
    auto matches = pattern.globalMatch(readme);

    int checked = 0;
    QStringList broken;
    while (matches.hasNext()) {
        // Se junta lo que el salto de linea habia partido. Una ruta escrita en
        // dos renglones es la misma ruta, y sin esto se comprobaria un trozo
        // —«Medida ▸ Corregir la distorsion»— que no existe como tal.
        QString path = matches.next().captured(1);
        path.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
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

namespace {

// El directorio `src/` DEL PROYECTO, esté el test donde esté al ejecutarse.
//
// No vale con que exista un `src`: al correr bajo ctest, el directorio de
// trabajo cae dentro de `build/`, y ahí `../src` es el árbol de ficheros
// generados —que existe y no tiene ni una ruta de menú dentro—. La primera
// versión se quedó con ese: sola pasaba y bajo ctest comprobaba cero rutas.
//
// La salvó el `EXPECT_GT(checked, 3)`, que está puesto justo para eso: una
// comprobación que no comprueba nada tiene que fallar, no aprobar en silencio.
// Aquí se ancla en un fichero que solo está en el árbol de verdad.
std::filesystem::path sourceRoot() {
    for (const auto* candidate : {"src", "../src", "../../src", "../../../src"}) {
        std::error_code ec;
        const std::filesystem::path dir(candidate);
        if (std::filesystem::exists(dir / "ui" / "main_window.cpp", ec)) {
            return dir;
        }
    }
    return {};
}

}  // namespace

// LA APLICACIÓN TAMPOCO PUEDE MANDAR A MENÚS QUE NO EXISTEN.
//
// El manual ya tenía su comprobación. Los mensajes que escribe la propia
// aplicación —barra de estado, guía de puesta a punto, ayudas— no la tenían, y
// se habían quedado atrás: decían «Cámara ▸ Calibrar…» cuando el menú Cámara
// pasó a llamarse Fuente y la calibración se mudó a Medida, y «Cámara ▸
// Configurar ▸ Cámara e imagen» cuando eso es una pestaña, no un submenú.
//
// Es peor que en el manual. El manual se lee antes; estos textos salen JUSTO
// cuando el operador está atascado, y mandarle a un menú inexistente en ese
// momento es lo que acaba de convencerle de que el programa está roto.
//
// Se comprueba lo que va ANTES de cada «▸»: nombres de menú y de submenú. Lo
// que va después del último se deja fuera a propósito, porque muchas veces no
// es una acción sino una pestaña o el resto de la frase.
TEST(SourceMenuPaths, TheMessagesTheApplicationWritesPointAtRealMenus) {
    const auto root = sourceRoot();
    if (root.empty()) {
        GTEST_SKIP() << "no se encontró el directorio src/";
    }
    pci::ui::MainWindow window;
    window.resize(1200, 800);
    const QSet<QString> names = everythingReachable(window);
    ASSERT_GT(names.size(), 20);

    static const QRegularExpression pattern(
        QStringLiteral("([A-Za-zÁÉÍÓÚÜÑáéíóúüñ][^\"\\n]{0,40}?)\\s*\\x{25b8}"));

    int checked = 0;
    QStringList broken;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const auto ext = entry.path().extension().string();
        if (ext != ".cpp" && ext != ".h") {
            continue;
        }
        std::ifstream file(entry.path(), std::ios::binary);
        std::ostringstream all;
        all << file.rdbuf();
        const QString text = QString::fromUtf8(all.str().c_str());

        for (const auto& line : text.split(QLatin1Char('\n'))) {
            // Sólo lo que el operador puede leer: los comentarios hablan de los
            // menús constantemente y no son texto de pantalla.
            const QString trimmed = line.trimmed();
            if (trimmed.startsWith(QStringLiteral("//")) ||
                trimmed.startsWith(QStringLiteral("*"))) {
                continue;
            }
            auto matches = pattern.globalMatch(line);
            while (matches.hasNext()) {
                QString part = matches.next().captured(1).trimmed();
                part.remove(QStringLiteral("…"));
                part = part.trimmed();
                if (part.isEmpty()) {
                    continue;
                }
                // EL NOMBRE DEL MENÚ ES LA COLA DE LA FRASE, no la frase.
                //
                // Lo que hay delante de un «▸» suele venir pegado al resto del
                // texto: «primero calibra la escala (Medida», «Usa el punto que
                // marques con Ver». La primera versión comparó la frase entera y
                // dio cinco rutas «rotas» que estaban perfectamente bien — una
                // comprobación que grita con lo correcto se acaba desactivando.
                //
                // Se prueban las colas de una, dos y tres palabras porque hay
                // menús de más de una («Corregir borde»). Si ninguna es un menú
                // real, la ruta está rota de verdad.
                const QStringList words = part.split(QLatin1Char(' '), Qt::SkipEmptyParts);
                if (words.isEmpty()) {
                    continue;
                }
                ++checked;
                bool found = false;
                QString shown;
                for (int take = 1; take <= 3 && take <= words.size(); ++take) {
                    QString tail = QStringList(words.mid(words.size() - take))
                                       .join(QLatin1Char(' '));
                    // El paréntesis de apertura viaja pegado al nombre cuando la
                    // ruta va entre paréntesis: «escala (Medida ▸ …».
                    while (!tail.isEmpty() && (tail.at(0) == QLatin1Char('(') ||
                                               tail.at(0) == QChar(0x00AB))) {
                        tail = tail.mid(1);
                    }
                    if (take == 1) {
                        shown = tail;
                    }
                    if (exists(names, tail)) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    broken << QString::fromStdString(entry.path().filename().string()) +
                                  QStringLiteral(": «") + shown + QStringLiteral("»");
                }
            }
        }
    }

    std::printf("  [mensajes] %d nombres de menú comprobados en el código, %d rotos\n",
                checked, static_cast<int>(broken.size()));
    for (const auto& one : broken) {
        std::printf("  [mensajes]    %s\n", one.toStdString().c_str());
    }
    EXPECT_GT(checked, 3) << "no se encontró ninguna ruta en el código: o cambió la "
                             "forma de escribirlas, o la expresión dejó de valer";
    EXPECT_TRUE(broken.isEmpty())
        << "la aplicación manda al operador a menús que no existen, y justo cuando "
           "está atascado";
}
