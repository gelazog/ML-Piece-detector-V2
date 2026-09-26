// ABRIR UNA IMAGEN COSTABA TRES GESTOS Y NINGUNO ERA EL DE SIEMPRE.
//
// Para abrir un fichero había que elegir «Abrir imagen…» o «Abrir vídeo…» en el
// desplegable de fuente y luego pulsar el botón de al lado. No había Ctrl+O, no
// había entrada en ningún menú y soltar el fichero sobre la ventana no hacía
// nada: los tres gestos que cualquiera prueba primero con un programa que abre
// imágenes. Y cada vez había que volver a navegar hasta el mismo fichero.
//
// Ahora hay «Archivo ▸ Abrir imagen o vídeo…» con Ctrl+O, arrastrar funciona y
// hay una lista de los 5 últimos, guardada en los ajustes. Lo que se fija aquí:
//
//   - imagen o vídeo se decide POR LA EXTENSIÓN, con una sola lista para las
//     tres puertas: si el diálogo y el soltar tuvieran listas distintas, un
//     fichero se abriría por una y se rechazaría por la otra;
//   - soltar un formato que no vale lo DICE en la barra de estado, en vez de no
//     hacer nada, que es exactamente lo que había antes;
//   - soltar un fichero con otro ya abierto lo cambia. Parar la fuente es
//     asíncrono, y abrir el nuevo antes de que la vieja suelte es la forma de
//     quedarse sin ninguna;
//   - los recientes sobreviven a cerrar el programa, son 5 como mucho, el más
//     nuevo arriba y sin repetidos, y uno que ya no existe se quita al pedirlo.

#include <gtest/gtest.h>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QUrl>

#include <cstdio>
#include <functional>

#include "database/db.h"
#include "database/schema.h"
#include "repositories/settings_repository.h"
#include "ui/main_window.h"
#include "ui/source_files.h"

using namespace pci;

namespace {

QString makeImage(const QTemporaryDir& dir, const QString& name) {
    QImage photo(200, 150, QImage::Format_RGB888);
    photo.fill(QColor(240, 240, 240));
    const QString path = QDir(dir.path()).filePath(name);
    return photo.save(path) ? path : QString();
}

// Por reloj y no por vueltas: `processEvents` vuelve en el acto con la cola
// vacía, y con la batería en paralelo la enumeración de cámaras llega tarde.
bool waitFor(const std::function<bool()>& done) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 8000) {
        if (done()) {
            return true;
        }
        QApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return done();
}

QComboBox* sourceCombo(ui::MainWindow& window) {
    return window.findChild<QComboBox*>(QStringLiteral("sourceCombo"));
}

bool showsFile(ui::MainWindow& window, const QString& path) {
    auto* combo = sourceCombo(window);
    return combo != nullptr &&
           combo->currentText().contains(QFileInfo(path).fileName());
}

// Suelta `path` sobre la ventana como lo haría el ratón: primero entra, luego
// se suelta. Devuelve si la entrada se aceptó.
bool dropOnto(ui::MainWindow& window, const QString& path) {
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(path)});
    QDragEnterEvent enter(QPoint(20, 20), Qt::CopyAction, &mime, Qt::LeftButton,
                          Qt::NoModifier);
    QApplication::sendEvent(&window, &enter);
    if (!enter.isAccepted()) {
        return false;
    }
    QDropEvent drop(QPointF(20, 20), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&window, &drop);
    return true;
}

// Las rutas que ofrece el submenú de recientes, por su ayuda (la ruta entera).
QStringList recentPaths(ui::MainWindow& window) {
    QStringList paths;
    auto* menu = window.findChild<QMenu*>(QStringLiteral("recentFilesMenu"));
    if (menu == nullptr) {
        return paths;
    }
    for (auto* action : menu->actions()) {
        if (!action->isSeparator() &&
            action->objectName() != QStringLiteral("clearRecentFilesAction")) {
            paths << QDir::fromNativeSeparators(action->toolTip());
        }
    }
    return paths;
}

}  // namespace

TEST(OpenFile, ImageOrVideoComesFromTheExtension) {
    using camera::SourceKind;
    EXPECT_EQ(ui::sourceKindForFile(QStringLiteral("C:/p/pieza.png")), SourceKind::Image);
    EXPECT_EQ(ui::sourceKindForFile(QStringLiteral("C:/p/PIEZA.JPG")), SourceKind::Image);
    EXPECT_EQ(ui::sourceKindForFile(QStringLiteral("pieza.tiff")), SourceKind::Image);
    EXPECT_EQ(ui::sourceKindForFile(QStringLiteral("linea.mp4")), SourceKind::Video);
    EXPECT_EQ(ui::sourceKindForFile(QStringLiteral("linea.MOV")), SourceKind::Video);
    EXPECT_FALSE(ui::sourceKindForFile(QStringLiteral("plano.pdf")).has_value());
    EXPECT_FALSE(ui::sourceKindForFile(QStringLiteral("sin_extension")).has_value());
    // El filtro del diálogo sale de la MISMA lista: lo que se ofrece al abrir es
    // lo que se acepta al soltar.
    for (const auto& extension : ui::imageExtensions() + ui::videoExtensions()) {
        EXPECT_TRUE(ui::imageOrVideoFileFilter().contains(QStringLiteral("*.") + extension))
            << extension.toStdString();
    }
}

TEST(OpenFile, RecentListKeepsFiveNewestFirstWithoutRepeats) {
    QStringList recent;
    for (int i = 1; i <= 7; ++i) {
        recent = ui::withRecentFile(recent, QStringLiteral("C:/p/f%1.png").arg(i));
    }
    ASSERT_EQ(recent.size(), 5);
    EXPECT_TRUE(recent.front().endsWith(QStringLiteral("f7.png")));
    EXPECT_TRUE(recent.back().endsWith(QStringLiteral("f3.png")));

    // Volver a abrir uno lo sube arriba, no lo duplica.
    recent = ui::withRecentFile(recent, QStringLiteral("C:/p/f4.png"));
    EXPECT_EQ(recent.size(), 5);
    EXPECT_TRUE(recent.front().endsWith(QStringLiteral("f4.png")));
    EXPECT_EQ(recent.filter(QStringLiteral("f4.png")).size(), 1);

    // Y la ida y vuelta por el ajuste de texto no pierde nada.
    EXPECT_EQ(ui::decodeRecentFiles(ui::encodeRecentFiles(recent)), recent);
}

TEST(OpenFile, TheMenuEntryIsInArchivoWithCtrlO) {
    ui::MainWindow window;
    auto* action = window.findChild<QAction*>(QStringLiteral("openFileAction"));
    ASSERT_NE(action, nullptr) << "no está «Abrir imagen o vídeo…»";
    EXPECT_EQ(action->shortcut(), QKeySequence(QKeySequence::Open));

    const QMenu* owner = nullptr;
    for (auto* top : window.menuBar()->actions()) {
        if (top->menu() != nullptr && top->menu()->actions().contains(action)) {
            owner = top->menu();
        }
    }
    ASSERT_NE(owner, nullptr) << "la acción existe pero no cuelga de ningún menú";
    EXPECT_EQ(owner->title(), QStringLiteral("&Archivo"));
    EXPECT_EQ(window.menuBar()->actions().front()->menu(), owner)
        << "«Archivo» va el primero, que es donde se busca";
}

TEST(OpenFile, DroppingAnImageOpensIt) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = makeImage(dir, QStringLiteral("soltada.png"));
    ASSERT_FALSE(path.isEmpty());

    ui::MainWindow window;
    window.resize(900, 600);
    window.show();
    ASSERT_TRUE(dropOnto(window, path)) << "la ventana no acepta que le suelten un fichero";
    EXPECT_TRUE(waitFor([&] { return showsFile(window, path); }))
        << "se soltó «soltada.png» y la fuente dice «"
        << sourceCombo(window)->currentText().toStdString() << "»";
}

TEST(OpenFile, DroppingSomethingElseSaysWhyInTheStatusBar) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("plano.pdf"));
    QFile pdf(path);
    ASSERT_TRUE(pdf.open(QIODevice::WriteOnly));
    pdf.write("%PDF-1.4\n");
    pdf.close();

    ui::MainWindow window;
    window.resize(900, 600);
    window.show();
    // Primero que termine la enumeración de cámaras: su «N cámara(s)
    // detectada(s)» llega tarde y taparía el mensaje que se quiere leer.
    ASSERT_TRUE(waitFor([&] {
        auto* combo = sourceCombo(window);
        return combo != nullptr &&
               combo->findText(QStringLiteral("Abrir imagen"), Qt::MatchStartsWith) >= 0;
    }));
    ASSERT_TRUE(dropOnto(window, path));

    const QString message = window.statusBar()->currentMessage();
    std::printf("  [abrir] al soltar un PDF: «%s»\n", message.toStdString().c_str());
    EXPECT_TRUE(message.contains(QStringLiteral("plano.pdf")) &&
                message.contains(QStringLiteral("no se puede abrir")))
        << "soltar un formato que no vale tiene que decirlo; sin mensaje parece que el "
           "programa no ha visto nada";
    EXPECT_FALSE(showsFile(window, path));
}

TEST(OpenFile, DroppingAnotherFileWhileOneIsOpenSwitchesToIt) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString first = makeImage(dir, QStringLiteral("primera.png"));
    const QString second = makeImage(dir, QStringLiteral("segunda.png"));

    ui::MainWindow window;
    window.resize(900, 600);
    window.show();
    ASSERT_TRUE(window.openFile(first));
    ASSERT_TRUE(waitFor([&] { return showsFile(window, first); }));

    // Aquí la fuente está EN MARCHA: el fichero nuevo solo puede abrirse cuando
    // la anterior ha terminado de pararse.
    ASSERT_TRUE(dropOnto(window, second));
    EXPECT_TRUE(waitFor([&] { return showsFile(window, second); }))
        << "con «primera.png» abierta se soltó «segunda.png» y la fuente dice «"
        << sourceCombo(window)->currentText().toStdString() << "»";
}

TEST(OpenFile, RecentFilesSurviveARestartAndForgetMissingOnes) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    auto opened =
        database::Db::open(QDir(dir.path()).filePath(QStringLiteral("r.db")).toStdString());
    ASSERT_TRUE(opened.isOk()) << opened.error().message;
    auto db = std::move(opened.value());
    ASSERT_TRUE(database::migrate(*db).isOk());
    repositories::SettingsRepository settings(*db);
    ui::AppRepositories repos;
    repos.settings = &settings;

    QStringList files;
    for (int i = 1; i <= 6; ++i) {
        files << makeImage(dir, QStringLiteral("pieza%1.png").arg(i));
    }

    // --- Sesión 1: se abren seis, uno detrás de otro -----------------------
    {
        ui::MainWindow window(repos);
        window.resize(900, 600);
        window.show();
        for (const auto& path : files) {
            ASSERT_TRUE(window.openFile(path));
            ASSERT_TRUE(waitFor([&] { return showsFile(window, path); }))
                << "no llegó a abrirse " << path.toStdString();
        }
    }

    // --- Sesión 2: la lista sigue ahí ---------------------------------------
    ui::MainWindow window(repos);
    window.resize(900, 600);
    window.show();
    const QStringList recent = recentPaths(window);
    for (const auto& one : recent) {
        std::printf("  [recientes] %s\n", one.toStdString().c_str());
    }
    ASSERT_EQ(recent.size(), ui::kMaxRecentFiles);
    EXPECT_TRUE(recent.front().endsWith(QStringLiteral("pieza6.png")))
        << "el más nuevo tiene que ir arriba";
    EXPECT_TRUE(recent.back().endsWith(QStringLiteral("pieza2.png")));

    // Uno que ya no existe se dice y se quita, en vez de fallar en silencio.
    ASSERT_TRUE(QFile::remove(files[3]));  // pieza4.png
    EXPECT_FALSE(window.openFile(files[3]));
    EXPECT_TRUE(window.statusBar()->currentMessage().contains(QStringLiteral("pieza4.png")));
    const QStringList after = recentPaths(window);
    EXPECT_EQ(after.size(), ui::kMaxRecentFiles - 1);
    EXPECT_TRUE(after.filter(QStringLiteral("pieza4.png")).isEmpty());
}
