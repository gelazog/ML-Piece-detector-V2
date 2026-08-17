// Pruebas de las FUENTES de archivo: que una imagen y un vídeo entreguen frames
// igual que lo hace la cámara, y que fallen diciendo por qué.
//
// Existen porque son el camino que hace verificable todo lo demás: hasta ahora,
// cualquier cosa que dependiera de ver una pieza necesitaba una cámara delante,
// y eso ha costado ya varios diseños con tests verdes que la cámara real
// desmintió.
#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QImage>
#include <QSignalSpy>
#include <QTest>
#include <QTemporaryDir>
#include <QTimer>

#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <cstdio>

#include "camera/file_sources.h"
#include "camera/frame_source.h"

using pci::camera::capabilitiesOf;
using pci::camera::SourceKind;
using pci::camera::StillImageSource;
using pci::camera::VideoFileSource;
using pci::camera::whyNotAdjustable;

namespace {

// Espera hasta que se cumpla una condición o venza el plazo, dejando correr el
// bucle de eventos. Sin esto no llegan ni las señales encoladas del hilo del
// vídeo ni los disparos del temporizador de la imagen.
bool waitFor(const std::function<bool()>& done, int timeoutMs = 3000) {
    QEventLoop loop;
    QTimer tick;
    bool ok = false;
    QObject::connect(&tick, &QTimer::timeout, [&] {
        if (done()) {
            ok = true;
            loop.quit();
        }
    });
    QTimer::singleShot(timeoutMs, &loop, [&loop] { loop.quit(); });
    tick.start(10);
    loop.exec();
    return ok || done();
}

QString writeImage(const QDir& dir, const QString& name, int w, int h) {
    QImage image(w, h, QImage::Format_RGB888);
    image.fill(Qt::darkGray);
    // Algo dentro, para que el frame no sea un rectángulo uniforme del que no
    // se pueda distinguir si llegó entero.
    for (int y = h / 4; y < 3 * h / 4; ++y) {
        for (int x = w / 4; x < 3 * w / 4; ++x) {
            image.setPixel(x, y, qRgb(220, 220, 220));
        }
    }
    const QString path = dir.filePath(name);
    EXPECT_TRUE(image.save(path));
    return path;
}

// Un vídeo de verdad, escrito con OpenCV: probar el lector contra un fichero
// inventado no probaría el lector.
QString writeVideo(const QDir& dir, const QString& name, int frames, int w, int h,
                   double fps = 25.0) {
    const QString path = dir.filePath(name);
    cv::VideoWriter writer(path.toStdString(), cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), fps,
                           cv::Size(w, h));
    if (!writer.isOpened()) {
        return {};
    }
    for (int i = 0; i < frames; ++i) {
        cv::Mat frame(h, w, CV_8UC3, cv::Scalar(40, 40, 40));
        // Un cuadrado que se mueve: así se puede comprobar que los frames
        // avanzan y no es el mismo repetido.
        cv::rectangle(frame, cv::Rect(10 + i * 5, 10, 30, 30), cv::Scalar(230, 230, 230),
                      cv::FILLED);
        writer.write(frame);
    }
    writer.release();
    return path;
}

}  // namespace

TEST(FrameSources, AnImageDeliversItsFrameAndKeepsDeliveringIt) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = writeImage(QDir(dir.path()), QStringLiteral("pieza.png"), 320, 240);

    StillImageSource source(path);
    QSignalSpy frames(&source, &pci::camera::FrameSource::frameReady);
    source.start();

    // El primero sale YA, sin esperar al temporizador: si no, la ventana se
    // quedaría un cuarto de segundo en negro tras abrir el fichero y eso se lee
    // como «no ha cargado».
    ASSERT_GE(frames.count(), 1);
    const QImage first = frames.at(0).at(0).value<QImage>();
    EXPECT_EQ(first.size(), QSize(320, 240));
    EXPECT_EQ(first.format(), QImage::Format_RGB888)
        << "el resto de la aplicación cuenta con RGB888, como entrega la cámara";

    // Y sigue emitiendo. No es un capricho: media aplicación reacciona a «llegó
    // un frame nuevo», así que emitir una sola vez dejaría la pantalla
    // congelada en cuanto el operador tocara un ajuste de detección.
    EXPECT_TRUE(waitFor([&] { return frames.count() >= 3; }))
        << "la imagen dejó de emitir: los ajustes de detección no se verían aplicarse";
    std::printf("  [imagen] %d frames en %d ms\n", static_cast<int>(frames.count()),
                3 * StillImageSource::kRepeatMs);

    source.stop();
    const int afterStop = static_cast<int>(frames.count());
    EXPECT_FALSE(waitFor([&] { return frames.count() > afterStop + 1; }, 600))
        << "sigue emitiendo después de parar";
}

TEST(FrameSources, AnImageThatCannotBeOpenedSaysWhyInsteadOfGoingQuiet) {
    // El caso más frecuente de todos —ruta equivocada, formato raro— y el que
    // peor se lleva el silencio: sin mensaje, el operador ve la pantalla en
    // negro y concluye que la aplicación está rota.
    StillImageSource source(QStringLiteral("D:/no/existe/esto.png"));
    QSignalSpy errors(&source, &pci::camera::FrameSource::sourceError);
    QSignalSpy stopped(&source, &pci::camera::FrameSource::stopped);
    source.start();

    ASSERT_EQ(errors.count(), 1);
    const QString reason = errors.at(0).at(0).toString();
    EXPECT_FALSE(reason.isEmpty());
    EXPECT_TRUE(reason.contains(QStringLiteral("esto.png")))
        << "el mensaje tiene que decir QUÉ fichero: " << reason.toStdString();
    // Y avisa de que se paró, o la ventana se quedaría creyendo que hay fuente.
    EXPECT_EQ(stopped.count(), 1);
    EXPECT_FALSE(source.isRunning());
}

TEST(FrameSources, AVideoPlaysItsFramesInOrderAndLoops) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = writeVideo(QDir(dir.path()), QStringLiteral("pieza.avi"), 8, 160, 120);
    if (path.isEmpty()) {
        GTEST_SKIP() << "OpenCV no tiene con qué escribir MJPG en esta máquina";
    }

    VideoFileSource source(path);
    QSignalSpy frames(&source, &pci::camera::FrameSource::frameReady);
    source.start();

    // Más frames de los que tiene el fichero: eso es lo que demuestra que da la
    // vuelta. Un vídeo parado en el último frame obligaría a reabrirlo para
    // volver a mirar, y de un vídeo de una pieza lo que se quiere es verlo en
    // bucle mientras se ajusta la detección.
    EXPECT_TRUE(waitFor([&] { return frames.count() > 8; }))
        << "solo llegaron " << frames.count() << " frames de un vídeo de 8: no da la vuelta";
    std::printf("  [video] %d frames de un fichero de 8\n", static_cast<int>(frames.count()));

    const QImage frame = frames.at(0).at(0).value<QImage>();
    EXPECT_EQ(frame.size(), QSize(160, 120));
    EXPECT_EQ(frame.format(), QImage::Format_RGB888);

    source.stop();
    EXPECT_FALSE(source.isRunning());
}

TEST(FrameSources, AVideoThatCannotBeOpenedSaysWhy) {
    VideoFileSource source(QStringLiteral("D:/no/existe/esto.mp4"));
    QSignalSpy errors(&source, &pci::camera::FrameSource::sourceError);
    source.start();
    EXPECT_TRUE(waitFor([&] { return errors.count() >= 1; }));
    ASSERT_GE(errors.count(), 1);
    const QString reason = errors.at(0).at(0).toString();
    EXPECT_TRUE(reason.contains(QStringLiteral("esto.mp4"))) << reason.toStdString();
    // Y menciona el códec, que es la causa real cuando la ruta sí existe.
    EXPECT_TRUE(reason.contains(QStringLiteral("códec"), Qt::CaseInsensitive))
        << "sin la pista del códec, un MP4 que no se abre no tiene diagnóstico";
}

TEST(FrameSources, StoppingAVideoThatNeverStartedDoesNothing) {
    // Se llama al parar la ventana, al cambiar de fuente y al cerrar. Que sea
    // inofensivo no es evidente: `stop()` hace `join` de un hilo que puede no
    // existir.
    VideoFileSource source(QStringLiteral("D:/no/existe.mp4"));
    QSignalSpy stopped(&source, &pci::camera::FrameSource::stopped);
    ASSERT_NO_THROW(source.stop());
    EXPECT_EQ(stopped.count(), 0) << "no se para lo que nunca arrancó";
}

TEST(SourceCapabilities, EachSourcePromisesOnlyWhatItCanDo) {
    // Esto es lo que permite deshabilitar CON MOTIVO en vez de repartir
    // `if (esCamara)` por toda la ventana. Un control muerto sin explicación es
    // peor que un control ausente: el operador cree que la aplicación falla.
    const auto camera = capabilitiesOf(SourceKind::Camera);
    EXPECT_TRUE(camera.adjustableControls);
    EXPECT_TRUE(camera.selectableResolution);
    EXPECT_TRUE(camera.meaningfulCaptureFps);
    EXPECT_TRUE(camera.focusable);

    const auto image = capabilitiesOf(SourceKind::Image);
    EXPECT_FALSE(image.adjustableControls);
    EXPECT_FALSE(image.selectableResolution);
    EXPECT_FALSE(image.focusable) << "no se puede enfocar lo que ya está tomado";
    EXPECT_FALSE(image.meaningfulCaptureFps)
        << "una imagen se reemite al ritmo que se inventa la aplicación: enseñarlo sería "
           "responder a una pregunta que nadie hizo";

    const auto video = capabilitiesOf(SourceKind::Video);
    EXPECT_FALSE(video.adjustableControls);
    EXPECT_FALSE(video.focusable);
    EXPECT_TRUE(video.meaningfulCaptureFps)
        << "los fps de un vídeo SÍ dicen algo: son los del fichero";

    // Y siempre que algo no se pueda tocar, hay una frase que lo explica.
    EXPECT_TRUE(whyNotAdjustable(SourceKind::Camera).isEmpty());
    for (const auto kind : {SourceKind::Image, SourceKind::Video}) {
        const QString why = whyNotAdjustable(kind);
        EXPECT_FALSE(why.isEmpty());
        EXPECT_GT(why.size(), 30) << "un motivo de tres palabras no explica nada";
    }
}

// ---------------------------------------------------------------------------
// La foto: congelar el frame actual (T3)
// ---------------------------------------------------------------------------

TEST(FrameSources, AFrozenPhotoDeliversTheImageItWasGiven) {
    // No hay fichero de por medio: la foto llega ya en memoria, recién sacada
    // del vídeo. Es lo que permite congelar sin pasar por disco.
    QImage photo(200, 150, QImage::Format_RGB888);
    photo.fill(Qt::darkCyan);

    StillImageSource source(photo, QStringLiteral("Foto 10:57:12"), SourceKind::Photo);
    QSignalSpy frames(&source, &pci::camera::FrameSource::frameReady);
    source.start();

    ASSERT_GE(frames.count(), 1);
    const QImage first = frames.at(0).at(0).value<QImage>();
    EXPECT_EQ(first.size(), QSize(200, 150));
    EXPECT_EQ(first.format(), QImage::Format_RGB888);
    EXPECT_EQ(source.kind(), SourceKind::Photo);
    // El nombre lo pone quien la toma, porque aquí no hay fichero del que tirar.
    EXPECT_EQ(source.describe(), QStringLiteral("Foto 10:57:12"));

    EXPECT_TRUE(waitFor([&] { return frames.count() >= 3; }))
        << "la foto dejó de emitir: los ajustes de detección no se verían aplicarse";
    source.stop();
}

TEST(FrameSources, AnEmptyPhotoSaysSoInsteadOfShowingNothing) {
    // Congelar antes de que llegue el primer frame es un caso real, y una
    // pantalla en negro sin mensaje se lee como avería.
    StillImageSource source(QImage(), QStringLiteral("Foto"), SourceKind::Photo);
    QSignalSpy errors(&source, &pci::camera::FrameSource::sourceError);
    QSignalSpy stopped(&source, &pci::camera::FrameSource::stopped);
    source.start();
    EXPECT_EQ(errors.count(), 1);
    EXPECT_EQ(stopped.count(), 1);
    EXPECT_FALSE(source.isRunning());
}

TEST(SourceCapabilities, APhotoIsNotAFileAndThatDistinctionIsAboutCalibration) {
    // Las dos son una imagen fija y aun así son tipos distintos, porque la
    // ESCALA no se comporta igual: una foto sale de esta cámara, con esta óptica
    // y a esta distancia, así que los mm/px siguen valiendo. Un fichero no
    // garantiza ninguna de las tres.
    //
    // Tratarlas igual obligaría a elegir entre dos errores: avisar de
    // «calibración obsoleta» cada vez que alguien congela —un aviso que se
    // aprende a ignorar en dos días— o callarse también al abrir un fichero, que
    // es cuando de verdad hay que avisar.
    EXPECT_NE(SourceKind::Photo, SourceKind::Image);

    // En lo demás se comportan igual: nada que ajustar y sin fps que enseñar.
    const auto photo = capabilitiesOf(SourceKind::Photo);
    const auto image = capabilitiesOf(SourceKind::Image);
    EXPECT_EQ(photo.adjustableControls, image.adjustableControls);
    EXPECT_EQ(photo.selectableResolution, image.selectableResolution);
    EXPECT_EQ(photo.focusable, image.focusable);
    EXPECT_EQ(photo.meaningfulCaptureFps, image.meaningfulCaptureFps);

    // Y su motivo es el suyo: con una foto la cámara SIGUE conectada, así que el
    // texto tiene que decir cómo volver en vez de dar a entender que se perdió.
    const QString why = whyNotAdjustable(SourceKind::Photo);
    EXPECT_FALSE(why.isEmpty());
    EXPECT_TRUE(why.contains(QStringLiteral("vivo"), Qt::CaseInsensitive))
        << "no dice cómo volver al vídeo: " << why.toStdString();
    EXPECT_NE(why, whyNotAdjustable(SourceKind::Image))
        << "la foto y el fichero dan el mismo motivo, y no están en la misma situación";
}

// ---------------------------------------------------------------------------
// Control de reproducción: pausa, salto y paso a paso
// ---------------------------------------------------------------------------
//
// Un vídeo sin esto no sirve para lo que se abre un vídeo: encontrar EL frame en
// el que la pieza se ve bien y trabajar sobre él. Antes había que reabrirlo y
// esperar a que el bucle volviera a pasar por donde uno quería.

TEST(VideoControls, PausingStopsTheFramesAndResumingBringsThemBack) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = writeVideo(QDir(dir.path()), QStringLiteral("pieza.avi"), 30, 160, 120);
    if (path.isEmpty()) {
        GTEST_SKIP() << "OpenCV no tiene con qué escribir MJPG en esta máquina";
    }

    VideoFileSource source(path);
    QSignalSpy frames(&source, &pci::camera::FrameSource::frameReady);
    source.start();
    ASSERT_TRUE(waitFor([&] { return frames.count() > 2; })) << "el vídeo no arrancó";

    source.setPaused(true);
    EXPECT_TRUE(source.isPaused());
    // Se deja pasar tiempo de sobra para varios frames: si siguieran llegando,
    // la pausa no estaría pausando nada.
    QTest::qWait(220);
    const int afterPause = frames.count();
    QTest::qWait(220);
    EXPECT_EQ(frames.count(), afterPause) << "en pausa siguieron llegando frames";

    source.setPaused(false);
    EXPECT_TRUE(waitFor([&] { return frames.count() > afterPause; }))
        << "al reanudar no volvieron los frames";

    // Y parar tiene que funcionar CON EL VÍDEO EN PAUSA, que es justo cuando más
    // se cierra: si el bucle durmiera de una sola vez, cerrar tardaría lo que
    // durase la siesta.
    source.setPaused(true);
    source.stop();
    EXPECT_FALSE(source.isRunning()) << "no se pudo parar el vídeo en pausa";
}

TEST(VideoControls, SteppingAdvancesOneFrameAndLeavesItPaused) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = writeVideo(QDir(dir.path()), QStringLiteral("pieza.avi"), 30, 160, 120);
    if (path.isEmpty()) {
        GTEST_SKIP() << "OpenCV no tiene con qué escribir MJPG en esta máquina";
    }

    VideoFileSource source(path);
    QSignalSpy frames(&source, &pci::camera::FrameSource::frameReady);
    source.start();
    ASSERT_TRUE(waitFor([&] { return frames.count() > 2; }));
    source.setPaused(true);
    QTest::qWait(200);
    const int paused = frames.count();

    // Un paso: exactamente UNO. Con la barra no se puede elegir el frame — un
    // píxel de barra son varios frames en un vídeo largo.
    source.stepOneFrame();
    EXPECT_TRUE(waitFor([&] { return frames.count() == paused + 1; }))
        << "el paso no dio exactamente un frame: " << frames.count() - paused;
    QTest::qWait(220);
    EXPECT_EQ(frames.count(), paused + 1) << "tras el paso siguió reproduciendo";
    EXPECT_TRUE(source.isPaused()) << "el paso dejó el vídeo en marcha";

    source.stop();
}

TEST(VideoControls, SeekingMovesWhereItIsToldAndSaysWhereItIs) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = writeVideo(QDir(dir.path()), QStringLiteral("pieza.avi"), 40, 160, 120);
    if (path.isEmpty()) {
        GTEST_SKIP() << "OpenCV no tiene con qué escribir MJPG en esta máquina";
    }

    VideoFileSource source(path);
    QSignalSpy position(&source, &VideoFileSource::positionChanged);
    source.start();
    ASSERT_TRUE(waitFor([&] { return position.count() > 2; })) << "no informa de su posición";

    // El total tiene que ser creíble: sin él la barra no puede colocarse.
    const auto total = position.last().at(1).toLongLong();
    std::printf("  [video] el fichero declara %lld frames\n", static_cast<long long>(total));
    ASSERT_GT(total, 1) << "el contenedor no dice cuántos frames tiene";

    source.seekToFraction(0.75);
    // Se espera a que el bucle aplique el salto y reporte desde allí.
    EXPECT_TRUE(waitFor([&] {
        return !position.isEmpty() &&
               position.last().at(0).toLongLong() > total / 2;
    })) << "el salto no movió la reproducción";
    std::printf("  [video] tras saltar al 75 %%: frame %lld de %lld\n",
                static_cast<long long>(position.last().at(0).toLongLong()),
                static_cast<long long>(total));

    // Y al principio: saltar hacia atrás también tiene que funcionar, o solo
    // serviría para adelantar.
    source.seekToFraction(0.0);
    EXPECT_TRUE(waitFor([&] {
        return !position.isEmpty() && position.last().at(0).toLongLong() < total / 2;
    })) << "no se pudo volver atrás";

    source.stop();
}
