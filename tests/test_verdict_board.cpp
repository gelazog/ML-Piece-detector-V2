// EL VEREDICTO Y LOS ERRORES, LEGIBLES DESDE DONDE ESTÁ EL OPERADOR.
//
// Queja del taller: el operador trabaja de pie, a 1–1,5 m de la pantalla y con
// prisa, y no veía ni el veredicto ni los errores.
//
//   - El OK/NG salía en una banda de 16 px, solo durante la auto-inspección, o
//     en la línea del panel de medidas, que es un dock y casi siempre está
//     cerrado. A metro y medio 16 px no se leen: hacen falta unos 20 para el
//     texto de trabajo, y bastante más para la palabra que decide.
//   - «Cámara desconectada o sin señal» iba a la barra de estado, y el
//     siguiente mensaje lo borraba a los pocos segundos. Quien volvía de colocar
//     la pieza encontraba la imagen quieta y ninguna explicación. La base de
//     datos caída solo se veía como «BD ✕» en la esquina.
//
// Estas pruebas vigilan las dos piezas nuevas: el tablero de veredicto (palabra
// grande + motivo en una línea, con contraste medido y sin depender solo del
// color) y la banda de lo que impide medir (se queda hasta que se arregla, con
// botón cuando hay algo que pulsar).

#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QStyleHints>
#include <QTemporaryDir>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <cstdio>
#include <functional>
#include <set>

#include "camera/frame_source.h"
#include "database/db.h"
#include "database/schema.h"
#include "inspection_editor/canvas/editor_canvas.h"
#include "inspection_editor/tools/tool_geometry.h"
#include "repositories/piece_repository.h"
#include "ui/blocking_notice.h"
#include "ui/main_window.h"
#include "ui/measurements_panel.h"
#include "ui/theme.h"
#include "ui/verdict_board.h"

using namespace pci;
using ui::Blocker;
using ui::VerdictState;

namespace {

constexpr VerdictState kShownStates[] = {VerdictState::Working, VerdictState::Good,
                                         VerdictState::Bad, VerdictState::NoPiece,
                                         VerdictState::Failed};

inspection::ToolRunResult measuring(std::int64_t id, const char* name, double value, bool ok,
                                    int pieceIndex = 0) {
    inspection::ToolRunResult result;
    result.toolId = id;
    result.name = name;
    result.measured = value;
    result.ok = ok;
    result.pieceIndex = pieceIndex;
    return result;
}

inspection::ToolConfig banded(std::int64_t id, double lo, double hi) {
    inspection::ToolConfig config;
    config.id = id;
    config.toleranceMin = lo;
    config.toleranceMax = hi;
    return config;
}

bool waitFor(const std::function<bool()>& condition, int ms = 8000) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms) {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        if (condition()) {
            return true;
        }
    }
    return condition();
}

int pixelSize(QLabel* label) {
    label->ensurePolished();
    return label->font().pixelSize();
}

}  // namespace

TEST(VerdictBoard, EveryStateHasItsOwnWordAndReadableContrast) {
    // WCAG 1.4.1: el color no puede ser lo único que separa OK de NG. Un
    // daltónico deutan —uno de cada doce hombres— no distingue ese verde de ese
    // rojo, así que cada estado tiene que decirse también con su palabra.
    std::set<QString> words;
    for (const VerdictState state : kShownStates) {
        const ui::VerdictLook look = ui::verdictLook(state);
        const double ratio =
            ui::theme::contrastRatio(ui::theme::color(look.ink), ui::theme::color(look.field));
        std::printf("  [veredicto] «%s»: contraste %.2f:1\n", look.word.toStdString().c_str(),
                    ratio);
        EXPECT_GE(ratio, 4.5) << "«" << look.word.toStdString()
                              << "» no llega al 4,5:1 de WCAG sobre su propio fondo";
        EXPECT_FALSE(look.word.isEmpty());
        words.insert(look.word);
    }
    EXPECT_EQ(words.size(), std::size(kShownStates))
        << "dos estados dicen la misma palabra: solo los separaría el color";
    EXPECT_TRUE(ui::verdictLook(VerdictState::Good).word.contains(QStringLiteral("OK")));
    EXPECT_TRUE(ui::verdictLook(VerdictState::Bad).word.contains(QStringLiteral("NG")));
    EXPECT_TRUE(ui::verdictLook(VerdictState::NoPiece).word.contains(QStringLiteral("pieza")));
}

TEST(VerdictBoard, ItIsBigEnoughToReadFromAMetreAndAHalf) {
    // La banda de antes tenía 16 px. A 1–1,5 m el texto de trabajo necesita unos
    // 20 px; la palabra del veredicto tiene que verse antes de enfocar.
    ui::VerdictBoard board;
    board.showVerdict(VerdictState::Bad, QStringLiteral("Ø interior: se pasa 0.15mm"));
    auto* word = board.findChild<QLabel*>(QStringLiteral("verdictWord"));
    auto* reason = board.findChild<QLabel*>(QStringLiteral("verdictReason"));
    ASSERT_NE(word, nullptr);
    ASSERT_NE(reason, nullptr);
    std::printf("  [veredicto] palabra %d px, motivo %d px\n", pixelSize(word),
                pixelSize(reason));
    EXPECT_GE(pixelSize(word), 40) << "la palabra del veredicto no se lee a metro y medio";
    EXPECT_GE(pixelSize(reason), 20) << "el motivo del NG no se lee a metro y medio";
    EXPECT_EQ(board.word(), ui::verdictLook(VerdictState::Bad).word);
}

TEST(VerdictBoard, TheReasonStaysOnOneLineAndTheFullTextIsInTheTooltip) {
    // El motivo va debajo, en UNA línea: si pudiera partirse, un motivo largo
    // empujaría el vídeo hacia abajo en cada fotograma que cambiara.
    ui::VerdictBoard board;
    board.resize(360, 110);
    board.show();
    const QString longReason =
        QStringLiteral("Pieza 3 · Ø interior del alojamiento del rodamiento: se pasa 0.15mm "
                       "(+4 más)");
    board.showVerdict(VerdictState::Bad, longReason);
    QApplication::processEvents();
    auto* reason = board.findChild<QLabel*>(QStringLiteral("verdictReason"));
    ASSERT_NE(reason, nullptr);
    std::printf("  [veredicto] motivo cortado: «%s»\n", reason->text().toStdString().c_str());
    EXPECT_FALSE(reason->wordWrap());
    EXPECT_LT(reason->text().size(), longReason.size()) << "el motivo largo no se corta";
    EXPECT_EQ(board.toolTip(), longReason) << "el motivo entero no queda en el tooltip";
    EXPECT_TRUE(board.accessibleName().contains(longReason))
        << "un lector de pantalla no lee el motivo entero";
}

TEST(VerdictBoard, ItsColoursDoNotDependOnTheTheme) {
    // Tema claro y oscuro: el tablero lleva su propio fondo, así que el
    // contraste medido arriba vale igual con Windows en claro que en oscuro.
    // Se comprueba pintándolo: el píxel de dentro es el fondo del estado, no el
    // de la ventana.
    auto* hints = QGuiApplication::styleHints();
    const Qt::ColorScheme before = hints->colorScheme();
    for (const Qt::ColorScheme scheme : {Qt::ColorScheme::Light, Qt::ColorScheme::Dark}) {
        hints->setColorScheme(scheme);
        for (const VerdictState state : {VerdictState::Good, VerdictState::Bad}) {
            ui::VerdictBoard board;
            board.resize(300, 110);
            board.showVerdict(state, QStringLiteral("motivo"));
            const QImage image = board.grab().toImage();
            const QColor inside = image.pixelColor(image.width() / 2, 3);
            EXPECT_EQ(inside.name(), ui::theme::color(ui::verdictLook(state).field).name())
                << "con el esquema " << (scheme == Qt::ColorScheme::Dark ? "oscuro" : "claro")
                << " el fondo del tablero no es el de su estado";
        }
    }
    hints->setColorScheme(before);
}

TEST(VerdictBoard, TheReasonNamesTheFailingCotaAndPiece) {
    // El tablero y la línea del panel salen de la MISMA cuenta
    // (`judgeMeasurements`) para no contradecirse. El motivo tiene que decir qué
    // cota falla y, con varias piezas, cuál.
    const std::vector<inspection::ToolRunResult> results{
        measuring(1, "Ø exterior", 15.0, true, 0), measuring(2, "Ø interior", 6.0, true, 0),
        measuring(1, "Ø exterior", 15.1, true, 1), measuring(2, "Ø interior", 6.4, false, 1)};
    const auto verdict =
        ui::judgeMeasurements(results, {banded(1, 14.75, 15.25), banded(2, 5.9, 6.25)}, 1.0,
                              inspection::LengthUnit::Millimeters);
    std::printf("  [veredicto] motivo: «%s»\n", verdict.reason.toStdString().c_str());
    EXPECT_TRUE(verdict.judged);
    EXPECT_FALSE(verdict.good);
    EXPECT_TRUE(verdict.reason.contains(QStringLiteral("Ø interior")));
    EXPECT_TRUE(verdict.reason.contains(QStringLiteral("2")));
    EXPECT_TRUE(verdict.reason.contains(QStringLiteral("0.15")) ||
                verdict.reason.contains(QStringLiteral("0,15")));

    const auto good = ui::judgeMeasurements({measuring(1, "Ancho", 15.0, true)}, {}, 1.0,
                                            inspection::LengthUnit::Millimeters);
    EXPECT_TRUE(good.good);
    EXPECT_TRUE(good.reason.isEmpty()) << "un OK no tiene motivo de NG";

    // Solo construcciones: no hay nada que juzgar y el tablero no puede decir OK.
    auto reference = measuring(3, "Punto medio", 0.0, true);
    reference.informative = true;
    EXPECT_FALSE(ui::judgeMeasurements({reference}, {}, 1.0,
                                       inspection::LengthUnit::Millimeters)
                     .judged);
}

TEST(VerdictBoard, TheWindowShowsTheLiveVerdictOfTheDrawnTools) {
    // De punta a punta: una imagen con un disco, un círculo dibujado encima, y
    // el tablero tiene que salir con su veredicto sin abrir el panel de medidas.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("disco.png"));
    cv::Mat scene(480, 640, CV_8UC3, cv::Scalar(225, 225, 225));
    cv::circle(scene, cv::Point(320, 240), 120, cv::Scalar(40, 40, 40), cv::FILLED,
               cv::LINE_AA);
    ASSERT_TRUE(cv::imwrite(path.toStdString(), scene));

    ui::MainWindow window;
    window.resize(1200, 800);
    window.show();
    auto* board = window.findChild<ui::VerdictBoard*>();
    ASSERT_NE(board, nullptr);
    ASSERT_TRUE(window.startFileSourceAtPath(camera::SourceKind::Image, path));
    auto* canvas = window.findChild<inspection::EditorCanvas*>();
    ASSERT_NE(canvas, nullptr);
    ASSERT_TRUE(waitFor([&] { return canvas->livePieceCount() >= 1; }));
    EXPECT_FALSE(board->isVisible()) << "sin herramientas no hay nada que juzgar";

    inspection::CircleGeometry circle;
    circle.center = cv::Point2f(0.0F, 0.0F);
    circle.radius = 120.0F;
    circle.searchBand = 30.0F;
    circle.rayCount = 72;
    emit canvas->toolCreated(inspection::ToolGeometry{circle});
    ASSERT_TRUE(waitFor([&] { return board->isVisible(); }))
        << "con una herramienta dibujada el tablero de veredicto no aparece";
    std::printf("  [veredicto] ventana: «%s» / «%s»\n", board->word().toStdString().c_str(),
                board->reason().toStdString().c_str());
    EXPECT_TRUE(board->state() == VerdictState::Good || board->state() == VerdictState::Bad);
}

TEST(BlockingNotice, StaysUntilResolvedAndOffersItsAction) {
    ui::BlockingNotice notice;
    QSignalSpy pressed(&notice, &ui::BlockingNotice::actionRequested);
    EXPECT_TRUE(notice.isHidden()) << "sin problemas la banda no ocupa sitio";

    notice.report(Blocker::Database, QStringLiteral("No hay base de datos."));
    notice.report(Blocker::Source, QStringLiteral("Cámara desconectada."),
                  QStringLiteral("Reintentar"));
    EXPECT_TRUE(!notice.isHidden());

    auto* retry = notice.findChild<QPushButton*>(QStringLiteral("blockerAction_source"));
    auto* dbButton = notice.findChild<QPushButton*>(QStringLiteral("blockerAction_database"));
    ASSERT_NE(retry, nullptr);
    ASSERT_NE(dbButton, nullptr);
    EXPECT_TRUE(retry->isVisibleTo(&notice)) << "la cámara caída no ofrece Reintentar";
    EXPECT_FALSE(dbButton->isVisibleTo(&notice)) << "un botón que no hace nada es un engaño";
    retry->click();
    ASSERT_EQ(pressed.count(), 1);
    EXPECT_EQ(pressed.front().front().value<Blocker>(), Blocker::Source);

    // No se va sola: ni con el tiempo ni con otros avisos.
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 300) {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    EXPECT_TRUE(notice.isReported(Blocker::Source));

    notice.resolve(Blocker::Source);
    EXPECT_FALSE(notice.isReported(Blocker::Source));
    EXPECT_TRUE(!notice.isHidden()) << "queda la base de datos";
    notice.resolve(Blocker::Database);
    EXPECT_TRUE(notice.isHidden()) << "resuelto todo, la banda sigue ahí";
}

TEST(BlockingNotice, TheMarkerOnlyCountsAsLostAfterAStreak) {
    // Un fotograma suelto sin marcador (un reflejo, la mano) no puede sacar la
    // banda roja: parpadearía y se aprendería a ignorarla. Uno bueno la quita.
    ui::MissStreak streak(10);
    for (int i = 0; i < 9; ++i) {
        EXPECT_FALSE(streak.observe(false)) << "salta con " << i + 1 << " fallos";
    }
    EXPECT_TRUE(streak.observe(false));
    EXPECT_TRUE(streak.observe(false));
    EXPECT_FALSE(streak.observe(true)) << "vuelve el marcador y el aviso sigue";
    EXPECT_FALSE(streak.observe(false));
}

TEST(BlockingNotice, TheWindowKeepsACameraFailureUntilAFrameArrives) {
    ui::MainWindow window;
    window.show();
    auto* notice = window.findChild<ui::BlockingNotice*>();
    ASSERT_NE(notice, nullptr);

    ASSERT_TRUE(QMetaObject::invokeMethod(&window, "onCameraError", Qt::DirectConnection,
                                          Q_ARG(QString, QStringLiteral(
                                                             "Cámara desconectada o sin señal"))));
    EXPECT_TRUE(notice->isReported(Blocker::Source));
    std::printf("  [banda] «%s»\n", notice->text(Blocker::Source).toStdString().c_str());
    EXPECT_TRUE(notice->text(Blocker::Source).contains(QStringLiteral("desconectada")));
    auto* retry = notice->findChild<QPushButton*>(QStringLiteral("blockerAction_source"));
    ASSERT_NE(retry, nullptr);
    EXPECT_EQ(retry->text(), QStringLiteral("Reintentar"));

    // Otros mensajes de la barra de estado no la borran; llega imagen, sí.
    QImage frame(64, 48, QImage::Format_RGB888);
    frame.fill(Qt::gray);
    ASSERT_TRUE(QMetaObject::invokeMethod(&window, "onFrame", Qt::DirectConnection,
                                          Q_ARG(QImage, frame)));
    EXPECT_FALSE(notice->isReported(Blocker::Source))
        << "llegó un fotograma y el aviso de cámara sigue puesto";
}

TEST(BlockingNotice, TheWindowSaysWhenThereIsNoDatabase) {
    // Sin base de datos (así arranca si no se puede abrir ni migrar) no se
    // puede inspeccionar: tiene que decirse arriba, no solo con «BD ✕».
    ui::MainWindow down;
    auto* downNotice = down.findChild<ui::BlockingNotice*>();
    ASSERT_NE(downNotice, nullptr);
    EXPECT_TRUE(downNotice->isReported(Blocker::Database));

    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    auto opened = database::Db::open(
        QDir(dir.path()).filePath(QStringLiteral("banda.db")).toStdString());
    ASSERT_TRUE(opened.isOk()) << opened.error().message;
    auto db = std::move(opened.value());
    ASSERT_TRUE(database::migrate(*db).isOk());
    repositories::PieceRepository pieces(*db);
    ui::AppRepositories repos;
    repos.pieces = &pieces;
    ui::MainWindow up(repos);
    auto* upNotice = up.findChild<ui::BlockingNotice*>();
    ASSERT_NE(upNotice, nullptr);
    EXPECT_FALSE(upNotice->isReported(Blocker::Database))
        << "con la base de datos abierta se avisa de que no hay";
}
