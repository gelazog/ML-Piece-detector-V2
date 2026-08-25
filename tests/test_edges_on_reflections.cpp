// DÓNDE FALLA HOY LA DETECCIÓN DE BORDES: REFLEJOS Y FONDO.
//
// Petición de uso: «si puedes mejorar la detección de bordes, debido a
// reflejos, fondo, etc.».
//
// Antes de tocar nada hay que saber qué pasa ahora. Esta prueba recorre las
// imágenes reales y publica, para cada una, lo que dan los dos métodos y lo que
// el consejero opina. No afirma cuál gana —eso se decide con los números
// delante—: lo que sí exige es que el consejero NO se contradiga con la
// realidad, que es lo único comprobable sin una verdad de campo.

#include <gtest/gtest.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "vision/edge_segmentation.h"
#include "vision/segmentation.h"

using namespace pci;

namespace {

// El corpus vive en `testdata/real`, y la prueba puede correr desde varios
// sitios segun quien la lance. Misma escalera que en test_edge_segmentation.
std::filesystem::path whereTheCorpusIs() {
    for (const auto* candidate : {"testdata/real", "../testdata/real",
                                  "../../testdata/real", "../../../testdata/real"}) {
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec)) {
            return std::filesystem::path(candidate);
        }
    }
    return {};
}

int countPieces(const cv::Mat& mask) {
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    int found = 0;
    for (const auto& contour : contours) {
        if (cv::contourArea(contour) >= 0.001 * static_cast<double>(mask.total())) {
            ++found;
        }
    }
    return found;
}

struct Outcome {
    int pieces = 0;
    double area = 0.0;
    double biggest = 0.0;
};

Outcome look(const cv::Mat& mask) {
    Outcome out;
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    for (const auto& contour : contours) {
        const double area = cv::contourArea(contour);
        // MÍNIMO RELATIVO AL ENCUADRE, no absoluto. Con 200 px² fijos, sobre una
        // foto de 1920x1285 se cuenta el polvo: en una tanda de candidatos eso
        // hizo que una escena pareciera dar 9 piezas por canto y 3 por nivel
        // cuando en realidad da 2 y 2. Un recuento que depende del tamaño de la
        // foto no compara nada.
        if (area < 0.001 * static_cast<double>(mask.total())) {
            continue;  // ruido, no pieza
        }
        ++out.pieces;
        out.area += area;
        out.biggest = std::max(out.biggest, area);
    }
    return out;
}

}  // namespace

TEST(EdgesOnReflections, WhereEachMethodStandsOnTheRealImages) {
    const std::filesystem::path dir("C:/Users/furro/Pictures/IMG-MC");
    if (!std::filesystem::exists(dir)) {
        GTEST_SKIP() << "las imágenes del usuario no están en esta máquina";
    }

    std::printf("\n  --- nivel vs borde sobre las imagenes reales ---\n");
    int looked = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        const cv::Mat gray = cv::imread(entry.path().string(), cv::IMREAD_GRAYSCALE);
        if (gray.empty()) {
            continue;
        }
        ++looked;

        vision::SegmentationOptions byLevel;
        byLevel.method = vision::SegmentationMethod::Level;
        vision::SegmentationOptions byEdge;
        byEdge.method = vision::SegmentationMethod::Edges;

        const auto levelMask = vision::segmentPiece(gray, byLevel);
        const auto edgeMask = vision::segmentPiece(gray, byEdge);
        ASSERT_TRUE(levelMask.isOk()) << entry.path().filename().string();
        ASSERT_TRUE(edgeMask.isOk()) << entry.path().filename().string();

        const Outcome level = look(levelMask.value());
        const Outcome edge = look(edgeMask.value());
        const bool advised = vision::edgeSegmentationLooksBetter(gray);
        const auto clip = vision::checkThresholdClipping(gray);
        const auto scene = vision::readScene(gray);
        std::printf("  %-30s  nivel %2d/%9.0f  borde %2d/%9.0f  %-14s  vaivén %5.1f%% %s\n",
                    entry.path().filename().string().c_str(), level.pieces, level.area,
                    edge.pieces, edge.area, advised ? "ACONSEJA BORDE" : "aconseja nivel",
                    clip.swing * 100.0, clip.thresholdCutsThePiece ? "RECORTA" : "");
    }
    EXPECT_GT(looked, 4) << "no se leyó casi ninguna imagen: la carpeta no es la que se cree";
}

// EL CONSEJERO ACIERTA DONDE EL BORDE GANA, Y CALLA DONDE PIERDE.
//
// La verdad de campo salió de mirar las fotos, no de suponerla:
//
//   tornillos-1.png   3 tornillos cincados sobre blanco
//   tornillo-2.png    1 tornillo galvanizado con marca oscura estampada
//   producto-tuercas-prueba.jpg   100 tuercas en bandeja
//
// Y lo que da cada método sobre ellas:
//
//   imagen            verdad   por nivel   por borde
//   tornillos-1          3       5 MAL       3 bien
//   tornillo-2           1       2 MAL       1 bien
//   bandeja de tuercas 100     100 bien     10 MAL
//
// O sea que el borde NO es mejor: es para otra escena. Lo que se exige aquí es
// que el consejo distinga las dos, que es exactamente lo que no hacía.
TEST(EdgesOnReflections, TheAdviceMatchesWhereEachMethodActuallyWins) {
    const std::filesystem::path dir("C:/Users/furro/Pictures/IMG-MC");
    if (!std::filesystem::exists(dir)) {
        GTEST_SKIP() << "las imágenes del usuario no están en esta máquina";
    }

    struct Expected {
        const char* file;
        bool shouldAdviseEdges;
        const char* why;
    };
    const std::vector<Expected> truth{
        {"tornillos-1.png", true, "3 tornillos brillantes; por nivel salen 5 trozos"},
        {"tornillo-2.png", true, "1 tornillo brillante; por nivel sale partido en 2"},
        {"producto-tuercas-prueba.jpg", false,
         "100 tuercas: el nivel las cuenta bien y el borde funde 10"},
        {"engranaje-1.png", false, "el nivel acierta y el vaivén es del 0,8 %"},
        {"tablero-ajedrez-medida.png", false, "casillas planas: nada que recortar"},
    };

    int checked = 0;
    for (const auto& want : truth) {
        const cv::Mat gray = cv::imread((dir / want.file).string(), cv::IMREAD_GRAYSCALE);
        if (gray.empty()) {
            continue;
        }
        ++checked;
        const auto scene = vision::readScene(gray);
        std::printf("  [consejo] %-30s dice %-6s (vaivén %5.1f%%) — %s\n", want.file,
                    scene.aSingleCutCannotDoIt ? "BORDE" : "nivel",
                    scene.thresholdSwing * 100.0, want.why);
        EXPECT_EQ(scene.aSingleCutCannotDoIt, want.shouldAdviseEdges)
            << want.file << ": " << want.why << "\n y el consejo dice lo contrario";
    }
    EXPECT_GE(checked, 4) << "faltan imágenes: esta prueba no comprobó casi nada";
}

// EL LADO CLARO NO SE PODÍA MIRAR, Y SE CALLABA.
//
// Con el fondo en 255 el techo de «más claro que el fondo» cae en 267, que
// ningún píxel de 8 bits alcanza. La cuenta salía 0,00 % y con ella
// `piecesStraddleTheBackground` quedaba en falso POR CONSTRUCCIÓN — cerrando la
// única puerta que ofrecía el método por borde. En las ocho imágenes reales del
// usuario el fondo va de 244 a 255: en las ocho.
TEST(EdgesOnReflections, ASaturatedBackgroundSaysItCannotSeeTheBrightSide) {
    cv::Mat onWhite(300, 300, CV_8UC1, cv::Scalar(255));
    cv::rectangle(onWhite, cv::Rect(100, 100, 100, 100), cv::Scalar(120), cv::FILLED,
                  cv::LINE_8);
    const auto white = vision::readScene(onWhite);
    std::printf("  [ciego] fondo %.0f -> lado claro %s\n", white.backgroundLevel,
                white.brightSideIsUnmeasurable ? "NO SE PUEDE MIRAR" : "medible");
    EXPECT_TRUE(white.brightSideIsUnmeasurable)
        << "con el fondo saturado dice 0 % de claros como si lo hubiera mirado";
    EXPECT_NE(white.summary.find("no se puede saber"), std::string::npos)
        << "no le dice al operador que ese lado se ha quedado sin mirar";

    // Y con un fondo medio SÍ se puede mirar: el aviso no puede salir siempre,
    // o se aprende a ignorarlo.
    cv::Mat onGrey(300, 300, CV_8UC1, cv::Scalar(128));
    cv::rectangle(onGrey, cv::Rect(100, 100, 100, 100), cv::Scalar(40), cv::FILLED,
                  cv::LINE_8);
    const auto grey = vision::readScene(onGrey);
    std::printf("  [ciego] fondo %.0f -> lado claro %s\n", grey.backgroundLevel,
                grey.brightSideIsUnmeasurable ? "NO SE PUEDE MIRAR" : "medible");
    EXPECT_FALSE(grey.brightSideIsUnmeasurable)
        << "dice que no puede mirar el lado claro con el fondo a media escala";
}

// LO QUE CUESTA EL CONSEJO.
//
// `readScene` corre por fotograma mientras el panel de detección está abierto, y
// ahora arrastra el comprobador de recorte, que segmenta la imagen DOS veces
// —con el corte apretado y con el corte flojo— para ver cuánto se mueve la
// silueta. Eso no es gratis y no se supone: se mide.
//
// El panel solo está abierto cuando alguien pelea con la iluminación, y en ese
// momento vale la pena pagarlo. Lo que no puede es dejar la vista a tirones.
TEST(EdgesOnReflections, TheAdviceCostsWhatSegmentingTwiceCosts) {
    const std::filesystem::path dir("C:/Users/furro/Pictures/IMG-MC");
    const cv::Mat gray = cv::imread((dir / "tornillos-1.png").string(), cv::IMREAD_GRAYSCALE);
    if (gray.empty()) {
        GTEST_SKIP() << "las imágenes del usuario no están en esta máquina";
    }

    vision::SegmentationOptions plain;
    constexpr int kRounds = 20;

    const auto timeOf = [&](auto&& work) {
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < kRounds; ++i) {
            work();
        }
        return std::chrono::duration<double, std::milli>(
                   std::chrono::steady_clock::now() - start)
                   .count() /
               kRounds;
    };

    const double oneSegmentation = timeOf([&] { (void)vision::segmentPiece(gray, plain); });
    const double theAdvice = timeOf([&] { (void)vision::readScene(gray); });
    std::printf("  [coste] una segmentación %.1f ms · el consejo entero %.1f ms · x%.2f\n",
                oneSegmentation, theAdvice, theAdvice / oneSegmentation);

    // NO PUEDE COSTAR MENOS DE UNA SEGMENTACIÓN: si costara, sería que el
    // recorte no se está midiendo y el consejo vuelve a darse a ciegas.
    EXPECT_GT(theAdvice, oneSegmentation)
        << "el consejo sale más barato que segmentar una vez: no está midiendo el recorte";
    // Y no más de doce veces: son dos segmentaciones y unas comparaciones.
    EXPECT_LT(theAdvice / oneSegmentation, 12.0)
        << "aconsejar cuesta desproporcionadamente para correr por fotograma";
}

// PIEZAS METÁLICAS DE VERDAD, BUSCADAS A PROPÓSITO.
//
// Petición de uso: «busca en internet imágenes que cumplan con esos problemas
// para medirlos, como tuercas, tornillos, engranajes, piezas».
//
// El corpus tenía bolas cerámicas y una tuerca mate: nada que reflejara de
// verdad. Se buscaron en Wikimedia Commons y se quedaron TRES, cada una por un
// motivo distinto. Las otras seis que se descargaron se tiraron y conviene
// saber por qué: un montaje de tres fotos en un fichero, dos primeros planos que
// desbordan el encuadre, una instantánea de una mano sujetando un mecanismo y
// dos arandelas cortadas por los cuatro lados. Ninguna es una escena de
// inspección, y medir sobre ellas habría dado números que no significan nada.
//
// Las que se quedan NO son todas casos que salgan bien. Un corpus que solo
// guarda lo que funciona deja de avisar de nada.
TEST(EdgesOnReflections, TheMetalPartsFoundOnPurposeSayWhereTheAdviceStands) {
    const std::filesystem::path dir = whereTheCorpusIs();
    if (dir.empty()) {
        GTEST_SKIP() << "corpus no descargado: python3 testdata/fetch_real_images.py";
    }

    struct Case {
        const char* file;
        int truth;
        const char* what;
    };
    const Case metal[] = {
        {"perno_cromado_con_arandela.jpg", 1,
         "conjunto cromado con reflejos: por nivel sale partido en trozos"},
        {"diez_tornillos_y_tuercas_juntos.jpg", 10,
         "diez piezas brillantes que se tocan: LÍMITE, no lo resuelve ningún método"},
        {"pieza_clara_sobre_fondo_texturizado.jpg", 1,
         "clara sobre fondo texturizado con sombra: el canto no cierra y lo dice"},
    };

    int seen = 0;
    for (const auto& one : metal) {
        const cv::Mat gray = cv::imread((dir / one.file).string(), cv::IMREAD_GRAYSCALE);
        if (gray.empty()) {
            continue;
        }
        ++seen;
        vision::SegmentationOptions byLevel;
        vision::SegmentationOptions byEdge;
        byEdge.method = vision::SegmentationMethod::Edges;
        const auto levelMask = vision::segmentPiece(gray, byLevel);
        const auto edgeMask = vision::segmentPiece(gray, byEdge);
        const auto scene = vision::readScene(gray);

        const int byLevelCount = levelMask.isOk() ? countPieces(levelMask.value()) : -1;
        const int byEdgeCount = edgeMask.isOk() ? countPieces(edgeMask.value()) : -1;
        std::printf("  [metal] %-42s verdad %2d -> nivel %2d, canto %2d  %s (%.1f%%)\n",
                    one.file, one.truth, byLevelCount, byEdgeCount,
                    scene.aSingleCutCannotDoIt ? "CANTO" : "nivel",
                    scene.thresholdSwing * 100.0);
    }
    ASSERT_EQ(seen, 3) << "faltan imágenes del corpus metálico";
}

// EL CONJUNTO CROMADO: el caso que la petición describía, y donde el consejo
// acierta. Verdad 1 pieza; el umbral por nivel la parte en cuatro.
TEST(EdgesOnReflections, AChromePartFragmentedByTheLevelCutIsSentToTheEdge) {
    const std::filesystem::path dir = whereTheCorpusIs();
    const cv::Mat gray =
        dir.empty() ? cv::Mat()
                    : cv::imread((dir / "perno_cromado_con_arandela.jpg").string(),
                                 cv::IMREAD_GRAYSCALE);
    if (gray.empty()) {
        GTEST_SKIP() << "corpus no descargado";
    }
    vision::SegmentationOptions byLevel;
    const auto mask = vision::segmentPiece(gray, byLevel);
    ASSERT_TRUE(mask.isOk());
    const int fragments = countPieces(mask.value());
    const auto scene = vision::readScene(gray);
    std::printf("  [cromado] el nivel lo parte en %d; se aconseja %s\n", fragments,
                scene.aSingleCutCannotDoIt ? "CANTO" : "nivel");

    EXPECT_GT(fragments, 1)
        << "el nivel ya no parte esta pieza: si eso cambió, este caso dejó de "
           "probar lo que dice probar";
    EXPECT_TRUE(scene.aSingleCutCannotDoIt)
        << "una pieza cromada que el nivel parte en trozos y no se ofrece el canto: "
           "es exactamente la queja de la que salió todo esto";
}

// EL LÍMITE QUE NO SE ARREGLA, y se guarda para que no se olvide.
//
// Diez piezas brillantes que se tocan entre sí. Por nivel salen 2 y por canto 2:
// lo que falla aquí no es el NIVEL DE GRIS sino que las piezas están pegadas, y
// eso es otro problema con otra herramienta (`splitTouchingPieces`).
//
// Esta prueba existe para que el día que alguien crea haberlo arreglado, lo
// compruebe contra una foto real y no contra una idea.
TEST(EdgesOnReflections, TenTouchingShinyPartsAreBeyondBothMethods) {
    const std::filesystem::path dir = whereTheCorpusIs();
    const cv::Mat gray =
        dir.empty() ? cv::Mat()
                    : cv::imread((dir / "diez_tornillos_y_tuercas_juntos.jpg").string(),
                                 cv::IMREAD_GRAYSCALE);
    if (gray.empty()) {
        GTEST_SKIP() << "corpus no descargado";
    }
    vision::SegmentationOptions byLevel;
    vision::SegmentationOptions byEdge;
    byEdge.method = vision::SegmentationMethod::Edges;
    const auto levelMask = vision::segmentPiece(gray, byLevel);
    const auto edgeMask = vision::segmentPiece(gray, byEdge);
    ASSERT_TRUE(levelMask.isOk());
    ASSERT_TRUE(edgeMask.isOk());
    const int byLevelCount = countPieces(levelMask.value());
    const int byEdgeCount = countPieces(edgeMask.value());
    std::printf("  [límite] diez piezas pegadas -> nivel %d, canto %d\n", byLevelCount,
                byEdgeCount);

    // Se fija el hecho, no la esperanza: hoy ninguno de los dos se acerca a diez.
    EXPECT_LT(byLevelCount, 6) << "el nivel ha mejorado en esta escena: actualiza la nota";
    EXPECT_LT(byEdgeCount, 6) << "el canto ha mejorado en esta escena: actualiza la nota";
}
