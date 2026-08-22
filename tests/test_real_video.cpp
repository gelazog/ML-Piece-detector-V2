// Pruebas contra VÍDEO REAL en MP4/H.264, no contra AVI generados aquí.
//
// Importa la diferencia, y mucho. Todo lo que este proyecto probaba de vídeo
// eran AVI de 8 a 250 frames escritos con MJPG por el propio test: intra-frame,
// fps entero, resolución redonda y sin códec de por medio. El operador trabaja
// con MP4 de H.264 de cincuenta minutos, donde:
//
//   - No hay índice de frames. `CAP_PROP_POS_FRAMES` es una estimación, y
//     buscar por él descodifica desde el keyframe anterior.
//   - Los fps no son enteros (aquí 45,238 y 29,97).
//   - La resolución no es redonda (aquí 1030x720, que ni siquiera es 16:9).
//
// Ese hueco ya se pagó una vez: la barra de posición «no cuadraba» sobre el MP4
// del operador y sobre los AVI del test iba perfecta.
//
// El corpus no está en el repositorio (vídeos de terceros con su licencia, y
// pesan). Estas pruebas SE SALTAN si no está.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include "vision/pipeline.h"

namespace {

std::string realVideo(const std::string& name) {
    for (const auto* base : {"testdata/real/", "../testdata/real/", "../../testdata/real/",
                             "../../../testdata/real/"}) {
        const std::string path = std::string(base) + name;
        std::error_code ec;
        if (std::filesystem::exists(path, ec)) {
            return path;
        }
    }
    return {};
}

#define REQUIRE_VIDEO(path)                                                            \
    if ((path).empty()) {                                                              \
        GTEST_SKIP() << "corpus de vídeo real no descargado (testdata/real/*.mp4)";     \
    }

}  // namespace

// Un MP4 de H.264 de verdad se abre, y sus propiedades son las que dice.
//
// La cabecera de un MP4 puede mentir sobre el número de frames —pasa, y por eso
// se comprueba— así que aquí se descodifica ENTERO y se cuenta a mano.
TEST(RealVideo, AnActualH264FileOpensAndItsHeaderTellsTheTruth) {
    const std::string path = realVideo("video_dado_unico.mp4");
    REQUIRE_VIDEO(path);

    cv::VideoCapture capture(path);
    ASSERT_TRUE(capture.isOpened()) << "OpenCV no abrió un MP4 de H.264 corriente";

    const double fps = capture.get(cv::CAP_PROP_FPS);
    const int declared = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_COUNT));
    const int width = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_WIDTH));
    const int height = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_HEIGHT));

    int decoded = 0;
    cv::Mat frame;
    while (capture.read(frame) && !frame.empty()) {
        ++decoded;
        ASSERT_LT(decoded, 100000) << "el vídeo no termina nunca";
    }

    std::printf("  [vídeo] %dx%d  %.4f fps  cabecera %d frames  descodificados %d\n", width,
                height, fps, declared, decoded);
    EXPECT_GT(decoded, 0);
    // Los fps NO son enteros, y eso es justo lo que ningún AVI del test tenía.
    EXPECT_NE(fps, std::floor(fps)) << "este vídeo debería tener fps fraccionarios (29,97)";
    EXPECT_NEAR(decoded, declared, 2)
        << "la cabecera declara " << declared << " frames y salen " << decoded;
}

// LA PRUEBA QUE FALTABA: buscar por tiempo en H.264 y aterrizar donde se pidió.
//
// Es lo que el operador reportó como «la posición no cuadra», y no se podía
// reproducir porque los AVI del test son intra-frame: en ellos cualquier frame
// es un punto de entrada y buscar siempre acierta. En H.264 sólo los keyframes
// lo son, así que pedir el segundo 7 deja el descodificador en el keyframe
// anterior — y si se pregunta por el frame en vez de por el tiempo, lo que se
// devuelve es la posición del KEYFRAME, no la pedida.
//
// Por eso la aplicación busca y lee la posición en MILISEGUNDOS.
TEST(RealVideo, SeekingByTimeLandsWhereItWasAskedInH264) {
    const std::string path = realVideo("video_dados_multiples.mp4");
    REQUIRE_VIDEO(path);

    cv::VideoCapture capture(path);
    ASSERT_TRUE(capture.isOpened());
    const double fps = capture.get(cv::CAP_PROP_FPS);
    const int frames = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_COUNT));
    ASSERT_GT(fps, 0.0);
    ASSERT_GT(frames, 30);
    const double durationMs = 1000.0 * frames / fps;

    double worstError = 0.0;
    for (const double fraction : {0.2, 0.4, 0.6, 0.8}) {
        const double askedMs = durationMs * fraction;
        ASSERT_TRUE(capture.set(cv::CAP_PROP_POS_MSEC, askedMs))
            << "el backend no admite buscar por tiempo";
        cv::Mat frame;
        ASSERT_TRUE(capture.read(frame)) << "no se pudo leer tras buscar al " << fraction;
        ASSERT_FALSE(frame.empty());
        const double landedMs = capture.get(cv::CAP_PROP_POS_MSEC);
        const double errorMs = std::abs(landedMs - askedMs);
        worstError = std::max(worstError, errorMs);
        std::printf("  [vídeo] pedido %7.1f ms -> aterrizó %7.1f ms  (desvío %5.1f ms)\n",
                    askedMs, landedMs, errorMs);
    }

    // Un frame dura 1000/45,24 = 22,1 ms. Se admite hasta medio segundo, que es
    // el orden de una distancia entre keyframes; más que eso sería la barra
    // señalando un sitio y la imagen otro.
    std::printf("  [vídeo] peor desvío %.1f ms sobre %.0f ms de duración\n", worstError,
                durationMs);
    EXPECT_LT(worstError, 500.0)
        << "buscar por tiempo aterriza lejos de lo pedido: la barra diría una cosa y la "
           "imagen sería otra";
}

// Leer el vídeo entero de principio a fin sin que el tiempo retroceda.
//
// Un tiempo que da un salto atrás a mitad de la reproducción es lo que se ve
// como «va a saltos», y sobre material intra-frame no aparece nunca.
TEST(RealVideo, TimeNeverGoesBackwardsWhilePlayingStraightThrough) {
    const std::string path = realVideo("video_dados_multiples.mp4");
    REQUIRE_VIDEO(path);

    cv::VideoCapture capture(path);
    ASSERT_TRUE(capture.isOpened());

    double previousMs = -1.0;
    int backwards = 0;
    int read = 0;
    cv::Mat frame;
    while (capture.read(frame) && !frame.empty()) {
        const double nowMs = capture.get(cv::CAP_PROP_POS_MSEC);
        if (nowMs < previousMs - 1e-6) {
            ++backwards;
        }
        previousMs = nowMs;
        ++read;
    }
    std::printf("  [vídeo] %d frames leídos, %d retrocesos de tiempo, final %.0f ms\n", read,
                backwards, previousMs);
    EXPECT_GT(read, 100);
    EXPECT_EQ(backwards, 0) << "el tiempo retrocede durante la reproducción normal";
}

// VARIAS PIEZAS EN MOVIMIENTO, sobre un fondo metálico con reflejos: cuatro o
// cinco dados cayendo. Es el caso difícil de verdad —el fondo brilla tanto como
// las piezas en algunos frames— y lo que se le exige no es acertar el número
// exacto de dados, que depende de cuántos se solapan en cada instante, sino:
//
//   1. que encuentre MÁS DE UNA pieza en algún momento, y
//   2. que ninguna «pieza» sea mayor que el propio frame, que es la forma que
//      tiene un fallo de segmentación de asomar en un vídeo real.
TEST(RealVideo, SeveralMovingPiecesAreFoundAndNoneIsBiggerThanTheFrame) {
    const std::string path = realVideo("video_dados_multiples.mp4");
    REQUIRE_VIDEO(path);

    cv::VideoCapture capture(path);
    ASSERT_TRUE(capture.isOpened());
    const int width = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_WIDTH));
    const int height = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_HEIGHT));

    int analysed = 0;
    int mostPieces = 0;
    int framesWithSeveral = 0;
    cv::Mat frame;
    // Uno de cada quince: recorrer 454 frames enteros con el pipeline completo
    // haría de esta prueba la más lenta de la batería sin decir nada nuevo.
    for (int index = 0; capture.read(frame) && !frame.empty(); ++index) {
        if (index % 15 != 0) {
            continue;
        }
        const auto all = pci::vision::analyzeFrames(frame, {});
        if (!all.isOk()) {
            continue;
        }
        ++analysed;
        const int found = static_cast<int>(all.value().size());
        mostPieces = std::max(mostPieces, found);
        if (found > 1) {
            ++framesWithSeveral;
        }
        for (const auto& piece : all.value()) {
            const cv::Rect box = cv::boundingRect(piece.contour.points);
            ASSERT_LE(box.width, width) << "una pieza más ancha que el frame, en el frame "
                                        << index;
            ASSERT_LE(box.height, height);
            ASSERT_GT(piece.contour.area, 0.0);
        }
    }

    std::printf("  [vídeo] %d frames analizados; hasta %d piezas a la vez; %d frames con "
                "más de una\n",
                analysed, mostPieces, framesWithSeveral);
    EXPECT_GT(analysed, 10);
    EXPECT_GT(mostPieces, 1)
        << "con cuatro dados en el aire no encontró más de una pieza en ningún momento";
}

// El mismo frame de un vídeo real, medido dos veces, da lo mismo. Sobre H.264
// esto no es gratis: descodificar el mismo instante dos veces pasa por el mismo
// keyframe y la misma cadena de predicción, y si algo de eso no fuera
// determinista, las medidas bailarían sin que nada en la imagen cambiara.
TEST(RealVideo, TheSameInstantOfARealVideoMeasuresTheSameTwice) {
    const std::string path = realVideo("video_dado_unico.mp4");
    REQUIRE_VIDEO(path);

    const auto grabAt = [&path](double ms) {
        cv::VideoCapture capture(path);
        capture.set(cv::CAP_PROP_POS_MSEC, ms);
        cv::Mat frame;
        capture.read(frame);
        return frame;
    };

    const cv::Mat first = grabAt(2000.0);
    const cv::Mat second = grabAt(2000.0);
    ASSERT_FALSE(first.empty());
    ASSERT_FALSE(second.empty());

    const auto a = pci::vision::analyzeFrame(first, {});
    const auto b = pci::vision::analyzeFrame(second, {});
    ASSERT_TRUE(a.isOk()) << a.error().message;
    ASSERT_TRUE(b.isOk()) << b.error().message;
    std::printf("  [vídeo] el instante 2,0 s mide %.1f px2 las dos veces\n",
                a.value().contour.area);
    EXPECT_DOUBLE_EQ(a.value().contour.area, b.value().contour.area);
    EXPECT_DOUBLE_EQ(a.value().contour.perimeter, b.value().contour.perimeter);
}
