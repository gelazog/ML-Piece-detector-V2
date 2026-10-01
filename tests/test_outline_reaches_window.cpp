// RODEAR UNA PIEZA TIENE QUE LLEGAR A LA VENTANA.
//
// «Marcar una pieza rodeándola…» y «Descartar lo que no es una pieza…» trazan
// sobre el lienzo, y el lienzo avisa con `pieceOutlined`. La ventana conectaba
// esa señal ANTES de crear el lienzo, sobre un puntero todavía nulo: Qt se
// limitaba a escribir un aviso en la consola y la conexión no existía. El trazo
// se dibujaba, el modo se quedaba encendido y no pasaba nada más. Es el mismo
// fallo que ya había costado cuatro rondas con `edgeCorrected`; salió al partir
// el constructor en métodos, porque el orden de llamadas quedó a la vista.
//
// Las dos pruebas emiten la señal como lo haría el lienzo al cerrar el trazo y
// miran lo único que la ventana hace siempre al recibirla: decirlo en la barra
// de estado.

#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QStatusBar>
#include <QStringList>
#include <QtGlobal>
#include <QTemporaryDir>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <functional>
#include <vector>

#include "camera/frame_source.h"
#include "inspection_editor/canvas/editor_canvas.h"
#include "ui/main_window.h"

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

}  // namespace

TEST(OutlineReachesWindow, AStrokeThatEnclosesNothingIsReported) {
    ui::MainWindow window;
    window.resize(1000, 700);
    window.show();
    auto* canvas = window.findChild<inspection::EditorCanvas*>();
    ASSERT_NE(canvas, nullptr);

    window.statusBar()->clearMessage();
    emit canvas->pieceOutlined(std::vector<cv::Point>{{10, 10}, {20, 20}}, true);
    QApplication::processEvents();

    EXPECT_TRUE(window.statusBar()->currentMessage().contains(QStringLiteral("no encierra")))
        << "la ventana no se enteró del trazo: «"
        << window.statusBar()->currentMessage().toStdString() << "»";
}

TEST(OutlineReachesWindow, DiscardingAnAreaOnAnOpenImageTurnsItIntoBackground) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("disco.png"));
    cv::Mat scene(480, 640, CV_8UC3, cv::Scalar(225, 225, 225));
    cv::circle(scene, cv::Point(320, 240), 120, cv::Scalar(40, 40, 40), cv::FILLED);
    ASSERT_TRUE(cv::imwrite(path.toStdString(), scene));

    ui::MainWindow window;
    window.resize(1000, 700);
    window.show();
    ASSERT_TRUE(window.startFileSourceAtPath(camera::SourceKind::Image, path));
    auto* canvas = window.findChild<inspection::EditorCanvas*>();
    ASSERT_NE(canvas, nullptr);
    ASSERT_TRUE(waitFor([&] { return canvas->livePieceCount() >= 1; }))
        << "la imagen no llegó a enseñar la pieza";

    window.statusBar()->clearMessage();
    const std::vector<cv::Point> square{{20, 20}, {120, 20}, {120, 120}, {20, 120}};
    emit canvas->pieceOutlined(square, false);
    QApplication::processEvents();

    EXPECT_TRUE(window.statusBar()->currentMessage().startsWith(QStringLiteral("Descartado")))
        << "descartar una zona no llegó a la ventana: «"
        << window.statusBar()->currentMessage().toStdString() << "»";
}

// NINGUNA CONEXIÓN RECHAZADA AL ARMAR LA VENTANA.
//
// Lo que tienen en común este fallo y el de `edgeCorrected`: Qt no se para al
// conectar sobre un puntero nulo o con una señal que no existe, solo escribe
// «QObject::connect: …» en la consola. Esta guarda escucha esos avisos mientras
// se construye la ventana entera y no deja pasar ninguno, venga de la conexión
// que venga.
namespace {
QStringList gConnectWarnings;
void collectConnectWarnings(QtMsgType, const QMessageLogContext&, const QString& message) {
    if (message.contains(QStringLiteral("QObject::connect"))) {
        gConnectWarnings << message;
    }
}
}  // namespace

TEST(OutlineReachesWindow, BuildingTheWindowLeavesNoRejectedConnection) {
    gConnectWarnings.clear();
    const QtMessageHandler previous = qInstallMessageHandler(collectConnectWarnings);
    {
        ui::MainWindow window;
        window.resize(1000, 700);
        window.show();
        QApplication::processEvents();
    }
    qInstallMessageHandler(previous);
    EXPECT_TRUE(gConnectWarnings.isEmpty())
        << "conexiones rechazadas al armar la ventana:\n"
        << gConnectWarnings.join(QLatin1Char('\n')).toStdString();
}
