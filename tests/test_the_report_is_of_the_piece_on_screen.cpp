// «MEDIR PIEZA» TIENE QUE HABLAR DE LA PIEZA QUE SE VE ELEGIDA.
//
// Con un número de piezas declarado —«esperadas: 2»— y una mancha de más en la
// mesa (una sombra, un tornillo suelto), el vídeo se queda con las DOS MAYORES y
// las numera en orden de lectura. «Medir pieza» buscaba las piezas por su
// cuenta y SIN ese recorte, así que contaba la mancha de más y su «pieza 1» era
// otra. En esta escena: el operador elige la 1 —en pantalla, la grande de
// arriba, 11 781 px² de área—, pulsa «Medir pieza» y el informe es el de la
// pequeña de su izquierda, 2 891 px². Sin ningún aviso, y «Vigilar estas cotas»
// se habría llevado las cotas de la pequeña a la grande.
//
// No se veía porque las pruebas del navegador declaran tantas piezas como hay
// (o ninguna), y entonces las dos numeraciones coinciden.

#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QStatusBar>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>

#include <cstdio>
#include <functional>

#include "camera/frame_source.h"
#include "database/db.h"
#include "database/schema.h"
#include "inspection_editor/canvas/editor_canvas.h"
#include "repositories/settings_repository.h"
#include "ui/main_window.h"
#include "ui/piece_report_dialog.h"

using namespace pci;

namespace {

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

// Pulsa «Medir pieza» y devuelve el área que da el informe, o −1 si no llegó a
// abrirse. El diálogo es modal: se lee y se cierra desde dentro de su bucle.
double reportedArea(ui::MainWindow& window) {
    double area = -1.0;
    QTimer poll;
    poll.setInterval(20);
    QObject::connect(&poll, &QTimer::timeout, [&] {
        auto* dialog = qobject_cast<ui::PieceReportDialog*>(QApplication::activeModalWidget());
        if (dialog == nullptr) {
            return;
        }
        poll.stop();
        if (auto* table = dialog->findChild<QTableWidget*>(QStringLiteral("reportTable"))) {
            for (int row = 0; row < table->rowCount(); ++row) {
                const auto* name = table->item(row, 0);
                const auto* value = table->item(row, 1);
                if (name != nullptr && value != nullptr && name->text() == QStringLiteral("Área")) {
                    area = value->text().toDouble();
                }
            }
        }
        dialog->reject();
    });
    poll.start();
    auto* measure = window.findChild<QPushButton*>(QStringLiteral("measurePieceButton"));
    if (measure != nullptr) {
        measure->click();
    }
    poll.stop();
    if (area < 0.0) {
        std::printf("  [informe] sin informe: botón %s, barra de estado «%s»\n",
                    measure == nullptr ? "no encontrado"
                                       : (measure->isEnabled() ? "activo" : "apagado"),
                    window.statusBar()->currentMessage().toStdString().c_str());
    }
    return area;
}

}  // namespace

TEST(ReportOfThePieceOnScreen, WithADeclaredCountTheNumberMeansTheSamePiece) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    // Arriba, en la misma fila: la pequeña a la izquierda y la mayor a la
    // derecha. Abajo, la mediana. Con dos declaradas la pequeña sobra.
    QImage photo(400, 300, QImage::Format_RGB888);
    photo.fill(QColor(20, 20, 20));
    {
        QPainter painter(&photo);
        painter.fillRect(QRect(60, 60, 60, 50), QColor(230, 230, 230));
        painter.fillRect(QRect(250, 50, 120, 100), QColor(230, 230, 230));
        painter.fillRect(QRect(70, 200, 90, 70), QColor(230, 230, 230));
    }
    const QString path = QDir(dir.path()).filePath(QStringLiteral("dos_y_una_sombra.png"));
    ASSERT_TRUE(photo.save(path));

    ui::MainWindow window;  // sin repositorios: ninguna pieza registrada
    window.resize(1200, 800);
    window.show();
    window.declareExpectedPieces(2);
    ASSERT_TRUE(window.startFileSourceAtPath(camera::SourceKind::Image, path));
    auto* canvas = window.findChild<inspection::EditorCanvas*>();
    auto* next = window.findChild<QToolButton*>(QStringLiteral("pieceNextButton"));
    auto* label = window.findChild<QLabel*>(QStringLiteral("pieceNavLabel"));
    ASSERT_NE(canvas, nullptr);
    ASSERT_NE(next, nullptr);
    ASSERT_NE(label, nullptr);
    ASSERT_TRUE(waitFor([&] { return canvas->livePieceCount() == 2 && next->isVisible(); }))
        << "con dos declaradas no se ven dos piezas (" << canvas->livePieceCount() << ")";

    // La pieza 1 de la pantalla: la grande de arriba.
    next->click();
    ASSERT_TRUE(waitFor([&] { return label->text().contains(QStringLiteral("pieza 1/2")); }))
        << "el selector no llega a la pieza 1: «" << label->text().toStdString() << "»";

    const double area = reportedArea(window);
    std::printf("  [informe] pieza 1 elegida, área del informe: %.0f px²\n", area);
    ASSERT_GT(area, 0.0) << "«Medir pieza» no abrió el informe";
    // La grande da ~11 800 px² y la pequeña ~2 900: el corte deja margen de sobra
    // a los dos lados.
    EXPECT_GT(area, 8000.0) << "el informe es de otra pieza: la 1 en pantalla es la grande, y "
                               "el informe da "
                            << area << " px²";
}

// UNA ZONA FIJA APAGADA NO RECORTA EL INFORME.
//
// La zona fija se guarda aunque se pase a «imagen entera» o a automática, para
// poder volver a ella. El vídeo la respeta apagada; «Medir pieza», la
// inspección, el registro y el editor salen de `inspectionConfig`, que solo
// apagaba la zona LIBRE y seguía recortando con el rectángulo. En esta escena
// el rectángulo guardado rodea la pieza pequeña: con la zona apagada el vídeo
// mide la mayor (11 781 px²) y el informe daba la pequeña (2 891 px²).
TEST(ReportOfThePieceOnScreen, AFixedZoneThatIsOffDoesNotCropTheReport) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QImage photo(400, 300, QImage::Format_RGB888);
    photo.fill(QColor(20, 20, 20));
    {
        QPainter painter(&photo);
        painter.fillRect(QRect(60, 60, 60, 50), QColor(230, 230, 230));
        painter.fillRect(QRect(250, 50, 120, 100), QColor(230, 230, 230));
    }
    const QString path = QDir(dir.path()).filePath(QStringLiteral("dos.png"));
    ASSERT_TRUE(photo.save(path));

    auto opened =
        database::Db::open(QDir(dir.path()).filePath(QStringLiteral("z.db")).toStdString());
    ASSERT_TRUE(opened.isOk()) << opened.error().message;
    auto db = std::move(opened.value());
    ASSERT_TRUE(database::migrate(*db).isOk());
    repositories::SettingsRepository settings(*db);
    // Una zona dibujada otro día alrededor de la pequeña, y luego apagada.
    ASSERT_TRUE(settings.setInt("det_roi_x", 0).isOk());
    ASSERT_TRUE(settings.setInt("det_roi_y", 0).isOk());
    ASSERT_TRUE(settings.setInt("det_roi_w", 200).isOk());
    ASSERT_TRUE(settings.setInt("det_roi_h", 300).isOk());
    ASSERT_TRUE(settings.setString("work_zone_mode", "off").isOk());
    ui::AppRepositories repos;
    repos.settings = &settings;

    ui::MainWindow window(repos);
    window.resize(1200, 800);
    window.show();
    ASSERT_TRUE(window.startFileSourceAtPath(camera::SourceKind::Image, path));
    auto* canvas = window.findChild<inspection::EditorCanvas*>();
    ASSERT_NE(canvas, nullptr);
    ASSERT_TRUE(waitFor([&] { return canvas->livePieceCount() == 2; }))
        << "con la zona apagada el vídeo tiene que ver las dos piezas";

    const double area = reportedArea(window);
    std::printf("  [informe] zona apagada, área del informe: %.0f px²\n", area);
    ASSERT_GT(area, 0.0) << "«Medir pieza» no abrió el informe";
    EXPECT_GT(area, 8000.0) << "el vídeo mide la mayor y el informe da " << area
                            << " px²: lo recorta la zona apagada";
}
