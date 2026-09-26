// LAS MEDIDAS TIENEN QUE SALIR CON CUALQUIER FUENTE, NO SOLO CON LA CÁMARA.
//
// Queja del dueño: «la toma de mediciones no está funcionando con las
// diferentes capturas, más que en cámara en vivo, al igual que la toma de
// foto». Esta prueba abre cada fuente que se puede abrir sin cámara —una
// imagen y un vídeo—, dibuja un círculo sobre la pieza como lo haría el
// operador y comprueba que el panel de medidas llega a enseñar la medida.

#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QListWidget>
#include <QPushButton>
#include <QTemporaryDir>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <cstdio>
#include <functional>

#include "camera/frame_source.h"
#include "inspection_editor/canvas/editor_canvas.h"
#include "inspection_editor/tools/tool_geometry.h"
#include "ui/main_window.h"
#include "ui/measurements_panel.h"

using namespace pci;

namespace {

// Un disco oscuro de radio 120 px sobre fondo claro, en el centro.
cv::Mat sceneWithADisc() {
    cv::Mat scene(480, 640, CV_8UC3, cv::Scalar(225, 225, 225));
    cv::circle(scene, cv::Point(320, 240), 120, cv::Scalar(40, 40, 40), cv::FILLED,
               cv::LINE_AA);
    return scene;
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

// Dibuja un círculo sobre el disco, igual que el gesto del operador: el
// lienzo emite `toolCreated` con la geometría en coordenadas de PIEZA, y el
// disco está centrado en su propio fixture, así que el centro es (0,0).
int measuredRowsAfterDrawingACircle(ui::MainWindow& window) {
    auto* canvas = window.findChild<inspection::EditorCanvas*>();
    auto* panel = window.findChild<ui::MeasurementsPanel*>();
    if (canvas == nullptr || panel == nullptr) {
        return -1;
    }
    // Primero, que la pieza esté detectada: sin fixture no hay dónde anclar.
    if (!waitFor([&] { return canvas->livePieceCount() >= 1; })) {
        return -2;
    }
    inspection::CircleGeometry circle;
    circle.center = cv::Point2f(0.0F, 0.0F);
    circle.radius = 120.0F;
    circle.searchBand = 30.0F;
    circle.rayCount = 72;
    emit canvas->toolCreated(inspection::ToolGeometry{circle});
    waitFor([&] { return panel->rowCount() > 0; });
    return panel->rowCount();
}

}  // namespace

TEST(MeasuresOnEverySource, AnOpenedImageIsMeasured) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("disco.png"));
    ASSERT_TRUE(cv::imwrite(path.toStdString(), sceneWithADisc()));

    ui::MainWindow window;
    window.resize(1200, 800);
    window.show();
    ASSERT_TRUE(window.startFileSourceAtPath(camera::SourceKind::Image, path));

    const int rows = measuredRowsAfterDrawingACircle(window);
    std::printf("  [fuentes] imagen: %d fila(s) en el panel de medidas\n", rows);
    EXPECT_GT(rows, 0) << "con una imagen abierta, el círculo dibujado no llega a medirse "
                          "(código "
                       << rows << ")";
}

// LA FOTO DE UN VÍDEO SE PUEDE CAPTURAR Y MEDIR, IGUAL QUE LA DE LA CÁMARA.
//
// Dos fallos juntos, los dos del mismo recorrido:
//
//   1. «Capturar foto» contestaba «solo se puede capturar una foto del vídeo en
//      vivo de la cámara»: con un vídeo grabado la tira se quedaba vacía.
//   2. Elegir una captura de la tira creaba la fuente nueva ENCIMA de la que
//      había, sin pararla. Con un vídeo abierto, el aviso de «detenida» de la
//      fuente vieja llegaba a la ventana y desmontaba la nueva; con la cámara,
//      sus frames pisaban la foto.
//
// El recorrido entero: abrir un vídeo, capturar, elegir la captura y dibujar un
// círculo. La medida tiene que salir, y el vídeo tiene que quedar cerrado.
TEST(MeasuresOnEverySource, AFrameCapturedFromAVideoCanBeChosenAndMeasured) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("disco.avi"));
    {
        cv::VideoWriter writer(path.toStdString(), cv::VideoWriter::fourcc('M', 'J', 'P', 'G'),
                               10.0, cv::Size(640, 480));
        ASSERT_TRUE(writer.isOpened());
        for (int i = 0; i < 40; ++i) {
            writer.write(sceneWithADisc());
        }
    }

    ui::MainWindow window;
    window.resize(1200, 800);
    window.show();
    ASSERT_TRUE(window.startFileSourceAtPath(camera::SourceKind::Video, path));
    auto* canvas = window.findChild<inspection::EditorCanvas*>();
    ASSERT_NE(canvas, nullptr);
    ASSERT_TRUE(waitFor([&] { return canvas->livePieceCount() >= 1; }))
        << "el vídeo no llega a enseñar la pieza";

    auto* freeze = window.findChild<QPushButton*>(QStringLiteral("freezeButton"));
    auto* list = window.findChild<QListWidget*>(QStringLiteral("captureList"));
    ASSERT_NE(freeze, nullptr);
    ASSERT_NE(list, nullptr);
    ASSERT_TRUE(freeze->isEnabled()) << "con un vídeo abierto no se puede capturar";
    freeze->click();
    QApplication::processEvents(QEventLoop::AllEvents, 50);
    std::printf("  [fuentes] capturas en la tira tras pulsar: %d\n", list->count());
    ASSERT_EQ(list->count(), 1) << "«Capturar foto» con un vídeo no guarda nada en la tira";

    list->setCurrentRow(0);  // elegir la captura, como el operador
    QApplication::processEvents(QEventLoop::AllEvents, 100);

    const int rows = measuredRowsAfterDrawingACircle(window);
    std::printf("  [fuentes] captura de vídeo: %d fila(s) en el panel de medidas\n", rows);
    EXPECT_GT(rows, 0) << "sobre la captura elegida el círculo no llega a medirse (código "
                       << rows << ")";
}

TEST(MeasuresOnEverySource, AnOpenedVideoIsMeasured) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("disco.avi"));
    {
        cv::VideoWriter writer(path.toStdString(), cv::VideoWriter::fourcc('M', 'J', 'P', 'G'),
                               10.0, cv::Size(640, 480));
        ASSERT_TRUE(writer.isOpened()) << "no se pudo escribir el vídeo de prueba";
        for (int i = 0; i < 40; ++i) {
            writer.write(sceneWithADisc());
        }
    }

    ui::MainWindow window;
    window.resize(1200, 800);
    window.show();
    ASSERT_TRUE(window.startFileSourceAtPath(camera::SourceKind::Video, path));

    const int rows = measuredRowsAfterDrawingACircle(window);
    std::printf("  [fuentes] vídeo: %d fila(s) en el panel de medidas\n", rows);
    EXPECT_GT(rows, 0) << "con un vídeo abierto, el círculo dibujado no llega a medirse "
                          "(código "
                       << rows << ")";
}
