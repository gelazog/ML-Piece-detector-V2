// CON DOS PIEZAS TIENE QUE HABER FORMA DE MIRARLAS UNA A UNA.
//
// Queja de uso: «probé en otra imagen dos piezas, y no sale para estarlas
// checando». Hay dos caminos para eso y los dos se construyeron esta semana: el
// selector de flechas de la barra inferior y el panel de mosaico.
//
// Que existan en el código no significa que aparezcan. Esta prueba abre una
// imagen de DOS piezas en la ventana de verdad y comprueba que salen — que es
// lo único que le importa a quien la usa.

#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QDockWidget>
#include <QLabel>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

#include <opencv2/imgcodecs.hpp>

#include <cstdio>
#include <functional>

#include "camera/frame_source.h"
#include "inspection_editor/canvas/editor_canvas.h"
#include "synthetic_scenes.h"
#include "ui/main_window.h"
#include "ui/piece_mosaic.h"

using namespace pci;

namespace {

// Espera activa a que se cumpla algo, bombeando eventos: el análisis va en otro
// hilo y llega cuando llega.
bool waitFor(const std::function<bool()>& predicate, int ms = 6000) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms) {
        if (predicate()) {
            return true;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
        QTest::qWait(20);
    }
    return predicate();
}

}  // namespace

TEST(TwoPiecesUi, TheArrowsAndTheMosaicAppearWithTwoPieces) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    // Dos piezas bien separadas: dos barras de distinto alto.
    cv::Mat scene(500, 700, CV_8UC1, cv::Scalar(testing_support::kSceneBackground));
    cv::rectangle(scene, cv::Rect(120, 140, 150, 220),
                  cv::Scalar(testing_support::kScenePiece), cv::FILLED, cv::LINE_8);
    cv::rectangle(scene, cv::Rect(420, 120, 150, 260),
                  cv::Scalar(testing_support::kScenePiece), cv::FILLED, cv::LINE_8);
    const std::string path =
        QDir(dir.path()).filePath(QStringLiteral("dos.png")).toStdString();
    ASSERT_TRUE(cv::imwrite(path, scene));

    pci::ui::MainWindow window;
    window.resize(1400, 900);
    window.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));
    ASSERT_TRUE(window.startFileSourceAtPath(camera::SourceKind::Image,
                                             QString::fromStdString(path)));

    auto* canvas = window.findChild<inspection::EditorCanvas*>();
    ASSERT_NE(canvas, nullptr);
    // Hasta que el análisis encuentre las dos.
    ASSERT_TRUE(waitFor([&] { return canvas->livePieceCount() >= 2; }))
        << "el análisis no llega a ver dos piezas en una escena con dos piezas";

    // 1) EL SELECTOR DE FLECHAS. Es el camino de «pasar de una en una».
    QLabel* navLabel = window.findChild<QLabel*>(QStringLiteral("pieceNavLabel"));
    ASSERT_NE(navLabel, nullptr) << "no existe el selector de pieza";
    const bool navVisible = waitFor([&] { return navLabel->isVisible(); }, 3000);
    std::printf("  [dos piezas] selector: %s | texto «%s»\n",
                navVisible ? "VISIBLE" : "oculto",
                navLabel->text().toStdString().c_str());
    EXPECT_TRUE(navVisible)
        << "con dos piezas no aparece el selector para pasar de una a otra";

    // 2) EL MOSAICO. Es el camino de «verlas todas a la vez».
    auto* dock = window.findChild<QDockWidget*>(QStringLiteral("mosaicDock"));
    ASSERT_NE(dock, nullptr) << "no existe el panel de mosaico";
    const bool dockVisible = waitFor([&] { return dock->isVisible(); }, 3000);
    auto* mosaic = window.findChild<pci::ui::PieceMosaic*>();
    ASSERT_NE(mosaic, nullptr);
    const bool tiles = waitFor([&] { return mosaic->tileCount() >= 2; }, 3000);
    std::printf("  [dos piezas] mosaico: %s | %d baldosas\n",
                dockVisible ? "VISIBLE" : "oculto", mosaic->tileCount());
    EXPECT_TRUE(dockVisible) << "con dos piezas el panel de mosaico no se abre solo";
    EXPECT_TRUE(tiles) << "el mosaico está abierto pero vacío: "
                       << mosaic->tileCount() << " baldosas";
}
