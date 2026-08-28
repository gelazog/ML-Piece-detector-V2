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
#include <QSignalSpy>
#include <QAbstractButton>
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
#include "ui/configure_dialog.h"
#include "ui/main_window.h"
#include "ui/piece_mosaic.h"
#include "ui/pieces_page.h"

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

// «DICE QUE SE VEN 3 Y TENGO DOS; CUANDO LE DOY, SIGUE SALIENDO 2».
//
// Queja de uso, y era un fallo de verdad: el panel daba DOS NÚMEROS DISTINTOS
// para la misma pregunta.
//
//   · el aviso de abajo usaba las MANCHAS vistas  -> «se ven 3»
//   · el botón «usar lo que se ve» ponía las USADAS -> 2
//
// Y las usadas ya vienen recortadas a lo declarado, así que el botón nunca
// podía SUBIR el número — que es literalmente el propósito que su propio
// comentario tenía escrito: «tiene que poder subir el número cuando de verdad
// hay más piezas de las declaradas».
//
// Encima, al pulsar, el aviso se reescribía con el número nuevo y el «3»
// desaparecía: el operador se queda sin saber qué vio el programa.
TEST(TwoPiecesUi, TheButtonUsesTheSameNumberTheNoticeReports) {
    // POR LA VENTANA DE VERDAD, y no llamando a la página a mano.
    //
    // La primera versión de esta prueba construía la PiecesPage suelta y
    // simulaba lo que hace la ventana. Mutando el arreglo —devolviendo el botón
    // a las piezas USADAS— la prueba seguía en verde: no protegía nada. Es la
    // misma trampa que ya apareció antes en este proyecto, un camino de prueba
    // que funciona mientras el camino real no.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    // TRES manchas en el encuadre y DOS declaradas: es el caso exacto del
    // operador —«dice que se ven 3 y tengo dos»— y el único donde los dos
    // números se separan.
    //
    // Con UNA declarada no sirve: ahí el motor deja de enumerar a propósito, y
    // entonces «vistas» y «usadas» valen lo mismo y la mutación es invisible.
    // Costo una vuelta descubrirlo.
    cv::Mat scene(500, 900, CV_8UC1, cv::Scalar(testing_support::kSceneBackground));
    cv::rectangle(scene, cv::Rect(100, 140, 160, 230),
                  cv::Scalar(testing_support::kScenePiece), cv::FILLED, cv::LINE_8);
    cv::rectangle(scene, cv::Rect(370, 120, 160, 260),
                  cv::Scalar(testing_support::kScenePiece), cv::FILLED, cv::LINE_8);
    // La tercera, más pequeña: la que sobra respecto a lo declarado.
    cv::rectangle(scene, cv::Rect(650, 190, 120, 120),
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
    ASSERT_TRUE(waitFor([&] { return canvas->livePieceCount() >= 3; }))
        << "el análisis no llega a ver las tres manchas";

    // Se abre Configurar y se declaran DOS: entonces «vistas» son 3 y «usadas»
    // 2, que es justo el desacuerdo que sufría el operador.
    auto* configure = window.findChild<QAction*>(QStringLiteral("configureAction"));
    ASSERT_NE(configure, nullptr);
    configure->trigger();
    auto* dialog = window.findChild<pci::ui::ConfigureDialog*>();
    ASSERT_NE(dialog, nullptr);
    auto* page = dialog->piecesPage();
    ASSERT_NE(page, nullptr);
    page->setExpectedPieces(2);

    // Se deja que llegue un análisis con la declaración puesta. La línea de
    // estado se coge por su nombre y lo que se espera es lo que DICE: buscarla
    // por «Se ven» mezclaba las dos cosas, y si el rótulo cambiara el test
    // esperaría para siempre a una etiqueta que ya no existe.
    auto* notice = page->findChild<QLabel*>(QStringLiteral("countStatus"));
    ASSERT_NE(notice, nullptr) << "la página de piezas no tiene línea de estado";
    ASSERT_TRUE(waitFor([&] {
        return notice->text().contains(QStringLiteral("Se ven 3"));
    })) << "el aviso nunca llega a decir que se ven tres manchas. Dice: "
        << notice->text().toStdString();
    std::printf("  [contador] aviso: «%s»\n", notice->text().toStdString().c_str());

    // Y ahora el botón: tiene que dejar el campo en el MISMO número.
    //
    // POR NOMBRE Y NO POR TEXTO. Esto lo buscaba por la frase «lo que se ve», y
    // se rompió al reescribir el rótulo — que era justo lo que se pedía hacer.
    // Es la tercera vez que pasa en este proyecto: una prueba que se cae al
    // reescribir una etiqueta desanima a reescribir etiquetas.
    auto* use = page->findChild<QAbstractButton*>(QStringLiteral("useDetected"));
    ASSERT_NE(use, nullptr) << "no está el botón que copia el recuento detectado";
    use->click();

    std::printf("  [contador] tras pulsar, el campo dice %d\n", page->expectedPieces());
    EXPECT_EQ(page->expectedPieces(), 3)
        << "el aviso dice que se ven 3 y el botón deja otro número: son dos "
           "respuestas a la misma pregunta, y con las USADAS el botón nunca puede "
           "subir el número, que es para lo que existe";
}

TEST(TwoPiecesUi, TheAutomaticCounterIsNamedForWhatItDoes) {
    // Petición de uso: «debería llamarse contador automático de piezas». Tiene
    // razón — «Automática» a secas no dice automática QUÉ, y puesto al lado de
    // un campo numérico eso se lee como «el número es automático», que es otra
    // cosa.
    pci::ui::PiecesPage page(0);
    bool found = false;
    for (auto* candidate : page.findChildren<QAbstractButton*>()) {
        if (candidate->text().contains(QStringLiteral("Contador"), Qt::CaseInsensitive)) {
            found = true;
            std::printf("  [contador] opción: «%s»\n",
                        candidate->text().toStdString().c_str());
        }
    }
    EXPECT_TRUE(found) << "la opción de contar automáticamente no dice que cuenta";
    // Y sigue significando lo de siempre: cero declarado.
    EXPECT_EQ(page.expectedPieces(), 0);
    EXPECT_EQ(page.countMode(), pci::ui::PiecesPage::CountMode::Automatic);
}
