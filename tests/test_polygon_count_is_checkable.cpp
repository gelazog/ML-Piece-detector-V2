// LA HERRAMIENTA QUE CUENTA LADOS SE RECHAZABA A SÍ MISMA SIEMPRE.
//
// El recuento de lados es una cota distinta de las longitudes: su tolerancia
// vigila que no aparezca ni falte una cara, que es otra avería. Para no dar un
// número que dependa de la tolerancia con la que se simplifica el contorno, la
// herramienta se autocomprobaba mirando TRES epsilon —el elegido, su mitad y su
// doble— y exigiendo que los tres dieran el mismo recuento.
//
// Sobre el banco de fotos, eso pasa en **0 de 106** piezas.
//
// Y no porque las piezas sean malas: un salto de 4× es enorme al lado de la
// meseta real, y en el borde de una foto siempre hay algún epsilon del camino
// que mete o quita un vértice. La consecuencia en pantalla: la aplicación
// reconocía «hexágono de 6 lados» y **nunca** ofrecía comprobar que la pieza
// siguiera teniendo seis caras — justo la avería que ese recuento vigila.
//
// Ahora usa el criterio de meseta, que es EL MISMO con el que se decide la clase
// (`vision::stableSideCountOf`). Que las dos partes respondan lo mismo no es
// estilo: la última vez que leyeron el contorno con dos criterios distintos, la
// aplicación decía «6 lados» y proponía dos lados y tres redondeos.
//
// Medido sobre el contorno de la aplicación: pasan **102 de 105**, contra 0 de
// la comprobación vieja.
//
// Y hacen falta LAS DOS MITADES del criterio, como en E8. Con la meseta sola, un
// disco pasaba como «octógono»: `approxPolyDP` le da 8 vértices a lo largo de
// medio barrido, tan estable como los de un octógono de verdad. Lo que los
// separa es cuánto se apartan esos ocho lados del contorno — 13,4 px contra
// ~1 px.
//
// Y HACEN FALTA **TRES**, que es lo que se vio barriendo el banco pieza por
// pieza. Con la meseta y la tolerancia, la herramienta daba un recuento de
// lados comprobable en **17 piezas que la clasificación llama arandela o
// círculo** — «8 lados» sobre una arandela, con su banda, que la arandela de al
// lado no repite. Las dos partes contradiciéndose sobre la misma pieza, otra
// vez, y esta vez con las dos condiciones puestas.
//
// No era un fallo del ajuste: un octógono se ciñe a un disco de 50 px de
// diámetro con ~2 px de error, y la tolerancia admite 9. Una tolerancia
// ABSOLUTA no distingue nada en una pieza pequeña. Lo que separa un disco de un
// polígono no es cuánto se aparta el polígono, sino QUÉ SE APARTA MENOS.
//
// Medido: en esas 17 el círculo se aparta entre 0,51 y 3,47 px y el polígono
// entre 1,89 y 8,58; en los 106 polígonos del banco gana el polígono en todos,
// y el margen más justo son las tuercas con el borde en sombra (1,03).
//
// De 17 a 0, y sin perder ninguno de los 106.
//
// LO QUE ESTO NO ARREGLA, Y CONVIENE SABERLO. La propuesta automática sigue sin
// llegar casi nunca, y la razón está en otro sitio: la herramienta se saca su
// PROPIO contorno con un Otsu local dentro de su recuadro, en vez de usar la
// silueta que la aplicación ya tiene. Sobre la misma tuerca, el contorno de la
// aplicación da 6 lados aguantando 17 de 30 epsilon y el de la herramienta da 6
// aguantando 7. Eso es E7 en MEJORAS, y está aparcado por decisión del dueño del
// proyecto — así que aquí se deja medido y no se toca.

#include <gtest/gtest.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <cstdio>
#include <string>
#include <vector>

#include "vision/contour_analysis.h"
#include "vision/pipeline.h"
#include "vision/shape_class.h"

using namespace pci;

namespace {

std::vector<cv::Point> outerOf(const cv::Mat& mask) {
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
    return contours.empty() ? std::vector<cv::Point>{} : contours.front();
}

cv::Mat regularPolygon(int n, int radius) {
    cv::Mat canvas(radius * 2 + 80, radius * 2 + 80, CV_8UC1, cv::Scalar(0));
    std::vector<cv::Point> points;
    for (int i = 0; i < n; ++i) {
        const double angle = 2.0 * CV_PI * i / n;
        points.emplace_back(cvRound(radius + 40 + radius * std::cos(angle)),
                            cvRound(radius + 40 + radius * std::sin(angle)));
    }
    cv::fillPoly(canvas, std::vector<std::vector<cv::Point>>{points}, cv::Scalar(255),
                 cv::LINE_AA);
    return canvas;
}

}  // namespace

TEST(PolygonCountIsCheckable, ACleanPolygonGivesAStableCount) {
    // Lo que la herramienta tiene que saber contar, y con la meseta ancha: una
    // figura de lados rectos y esquinas vivas.
    // Se incluyen el 12, el 14 y el 16 A PROPÓSITO: son los que aprietan el
    // umbral. Cuantos más lados tiene un polígono, más estrecha es la ventana de
    // epsilon en la que sobreviven todos —el hexágono aguanta 29 de 30 y el de
    // 16 solo 6—, así que son los que se caen si alguien sube el listón pensando
    // que «más estable es mejor».
    for (const int sides : {3, 4, 5, 6, 8, 12, 14, 16}) {
        const cv::Mat mask = regularPolygon(sides, sides <= 6 ? 150 : 180);
        const auto stable = vision::stableSideCountOf(outerOf(mask));
        std::printf("  [recuento] %2d lados -> dice %2d, meseta %2d/%d, %s\n", sides,
                    stable.sides, stable.plateau, stable.swept,
                    stable.stable ? "estable" : "NO estable");
        EXPECT_TRUE(stable.stable)
            << "un polígono limpio de " << sides
            << " lados no se considera estable: entonces la herramienta se rechaza a sí "
               "misma también en el caso fácil";
        EXPECT_EQ(stable.sides, sides)
            << "un polígono de " << sides << " lados se cuenta como " << stable.sides;
    }
}

TEST(PolygonCountIsCheckable, ACircleIsNotAPolygonAndSaysSo) {
    // La otra mitad, y sin ella la de arriba pasaría con cualquier criterio que
    // dijera «estable» siempre. Un disco no tiene lados, y darle un recuento
    // sería inventar una cota.
    cv::Mat canvas(400, 400, CV_8UC1, cv::Scalar(0));
    cv::circle(canvas, {200, 200}, 150, cv::Scalar(255), cv::FILLED, cv::LINE_AA);
    const auto stable = vision::stableSideCountOf(outerOf(canvas));
    std::printf("  [recuento] disco -> dice %d, meseta %d/%d, %s\n", stable.sides,
                stable.plateau, stable.swept, stable.stable ? "estable" : "NO estable");
    EXPECT_FALSE(stable.stable)
        << "a un disco se le da un recuento de lados estable: es una cota inventada, y el "
           "operador le pondría una tolerancia";
}

TEST(PolygonCountIsCheckable, ThePiecesThatAreNotPolygonsAreStillRefused) {
    // EL OTRO LADO DEL UMBRAL, y es el que impide bajarlo «un poquito más» hasta
    // que todo pase. Un cáncamo y un tornillo no son polígonos, y su mejor
    // recuento —de los que además explican el contorno— aguanta 3 y 1 de 30.
    // Los polígonos legítimos más difíciles aguantan 6. Ese es todo el hueco que
    // hay, así que las dos mitades tienen que comprobarse juntas o la siguiente
    // persona lo cierra sin enterarse.
    for (const auto* photo : {"tornillo-ojo-5.png", "tornillos-1.png"}) {
        const cv::Mat image =
            cv::imread(std::string("C:/Users/furro/Pictures/IMG-MC/") + photo,
                       cv::IMREAD_COLOR);
        if (image.empty()) {
            GTEST_SKIP() << "sin banco de fotos";
        }
        vision::PipelineConfig config;
        config.segmentation.recoverHighlightsBy = 12;
        auto all = vision::analyzeFrames(image, config);
        ASSERT_TRUE(all.isOk());
        int pieces = 0;
        int stable = 0;
        for (const auto& piece : all.value()) {
            ++pieces;
            if (vision::stableSideCountOf(piece.contour.points).stable) {
                ++stable;
            }
        }
        std::printf("  [recuento] %s: %d piezas, %d con recuento comprobable\n", photo,
                    pieces, stable);
        EXPECT_EQ(stable, 0)
            << photo
            << ": a una pieza que no es un polígono se le da un recuento de lados con "
               "tolerancia. El operador le pondría «exactamente 12 lados» y la pieza "
               "buena de al lado daría otro número";
    }
}

TEST(PolygonCountIsCheckable, TheOldThreePointCheckFailedOnEveryRealPiece) {
    // Que el cambio no se dé por bueno de palabra. Se vuelve a ejecutar la
    // comprobación ANTIGUA —el epsilon elegido, su mitad y su doble— sobre el
    // banco, y tiene que suspender masivamente: si pasara, el motivo para
    // cambiarla era falso.
    //
    // Y de paso se comprueba que la nueva sí pasa, sobre los mismos contornos.
    const std::vector<std::string> photos = {
        "producto-tuercas-prueba.jpg", "arandelas-2.png", "arandelas-3.jpg",
        "arandelas-5.png", "Producto_Tuerca_Liv_02.jpg"};

    int polygons = 0;
    int oldWouldPass = 0;
    int newPasses = 0;
    for (const auto& photo : photos) {
        const cv::Mat image =
            cv::imread("C:/Users/furro/Pictures/IMG-MC/" + photo, cv::IMREAD_COLOR);
        if (image.empty()) {
            GTEST_SKIP() << "sin banco de fotos";
        }
        vision::PipelineConfig config;
        config.segmentation.recoverHighlightsBy = 12;
        auto all = vision::analyzeFrames(image, config);
        if (!all.isOk()) {
            continue;
        }
        for (const auto& piece : all.value()) {
            const cv::Mat mask =
                vision::pieceMaskWithHoles(image, piece.mask, config.segmentation);
            const auto shape = vision::classifyShape(piece.contour.points, mask);
            if (shape.kind != vision::ShapeKind::Polygon || shape.sides < 3) {
                continue;
            }
            ++polygons;
            const auto& outer = piece.contour.points;
            const double perimeter = cv::arcLength(outer, true);
            const auto sidesAt = [&outer](double eps) {
                std::vector<cv::Point> approx;
                cv::approxPolyDP(outer, approx, eps, true);
                return static_cast<int>(approx.size());
            };
            const double epsilon = std::max(1.0, 0.02 * perimeter);
            const int here = sidesAt(epsilon);
            if (here == sidesAt(epsilon * 0.5) && here == sidesAt(epsilon * 2.0)) {
                ++oldWouldPass;
            }
            if (vision::stableSideCountOf(outer).stable) {
                ++newPasses;
            }
        }
    }
    std::printf("  [recuento] %d polígonos del banco: la comprobación vieja pasaría en %d, "
                "la de meseta en %d\n",
                polygons, oldWouldPass, newPasses);

    ASSERT_GT(polygons, 50) << "el banco da muy pocos polígonos: esto no comprueba nada";
    EXPECT_EQ(oldWouldPass, 0)
        << "la comprobación antigua SÍ pasaba en alguna pieza real. Entonces el motivo "
           "para cambiarla —que se rechazaba a sí misma siempre— no era cierto, y hay que "
           "volver a mirarlo";
    EXPECT_GT(newPasses, polygons / 2)
        << "con el criterio de meseta la mayoría de las piezas siguen sin poder dar un "
           "recuento comprobable: el cambio no ha servido para lo que se hizo";
}

TEST(PolygonCountIsCheckable, ARingIsRefusedEvenThoughItsOctagonFitsTheTolerance) {
    // LA TERCERA CONDICIÓN, y lo que esta prueba comprueba es que rechaza ELLA,
    // no las otras dos. Sin esa distinción seguiría en verde el día que alguien
    // apretara la tolerancia, y entonces no estaría comprobando lo que cree.
    //
    // Un disco de 50 px de diámetro: el octógono se le ciñe con ~2 px de error
    // sobre los 9 admitidos, o sea que SÍ explica el contorno; y la meseta es
    // ancha, porque `approxPolyDP` le devuelve ocho vértices a lo largo de medio
    // barrido. Las dos condiciones de siempre dicen que sí. La que dice que no
    // es que la circunferencia se aparta cuatro veces menos.
    cv::Mat canvas(120, 120, CV_8UC1, cv::Scalar(0));
    cv::circle(canvas, {60, 60}, 25, cv::Scalar(255), cv::FILLED, cv::LINE_AA);
    const auto small = vision::stableSideCountOf(outerOf(canvas));
    std::printf("  [redondo] disco de 50 px -> %d lados, meseta %d/%d, se separa %.2f px de "
                "%.2f admitidos, y de la circunferencia %.2f px\n",
                small.sides, small.plateau, small.swept, small.deviation, small.admissible,
                small.circleDeviation);

    EXPECT_TRUE(small.explainsContour)
        << "el ajuste poligonal de un disco pequeño ya NO cabe en la tolerancia: entonces "
           "esta prueba está comprobando la condición vieja y no la nueva";
    EXPECT_TRUE(small.roundIsABetterFit)
        << "la circunferencia no gana: el contorno de un disco se estaría explicando igual "
           "de bien con lados";
    EXPECT_FALSE(small.stable)
        << "a un disco de 50 px se le da un recuento de lados comprobable. El operador le "
           "pone «exactamente 8 lados» y la arandela de al lado da otro número";
}

TEST(PolygonCountIsCheckable, NoWasherInTheBankIsOfferedASideCount) {
    // Y sobre las piezas de verdad, que es donde se vio. Las arandelas del banco
    // son pequeñas —de ahí que una tolerancia absoluta no las distinga— y van
    // fotografiadas con sombra, así que su octógono se ciñe de sobra.
    int pieces = 0;
    int round = 0;
    int withCount = 0;
    for (const auto* photo : {"arandelas-1.png", "arandelas-3.jpg", "arandelas-4.png",
                              "arandelas-5.png"}) {
        const cv::Mat image =
            cv::imread(std::string("C:/Users/furro/Pictures/IMG-MC/") + photo,
                       cv::IMREAD_COLOR);
        if (image.empty()) {
            GTEST_SKIP() << "sin banco de fotos";
        }
        vision::PipelineConfig config;
        config.segmentation.recoverHighlightsBy = 12;
        auto all = vision::analyzeFrames(image, config);
        if (!all.isOk()) {
            continue;
        }
        for (const auto& piece : all.value()) {
            const cv::Mat mask =
                vision::pieceMaskWithHoles(image, piece.mask, config.segmentation);
            const auto shape = vision::classifyShape(piece.contour.points, mask);
            ++pieces;
            if (shape.kind != vision::ShapeKind::Ring &&
                shape.kind != vision::ShapeKind::Circle) {
                continue;
            }
            ++round;
            if (vision::stableSideCountOf(piece.contour.points).stable) {
                ++withCount;
            }
        }
    }
    std::printf("  [redondo] %d piezas, %d redondas según la clase, %d con recuento de "
                "lados comprobable\n",
                pieces, round, withCount);
    ASSERT_GT(round, 10) << "apenas salen piezas redondas: esta prueba no comprueba nada";
    EXPECT_EQ(withCount, 0)
        << "la herramienta ofrece contar los lados de una pieza que la aplicación llama "
           "redonda. Son las dos partes contradiciéndose sobre la misma pieza, que es "
           "justo lo que este fichero vino a cerrar";
}

TEST(PolygonCountIsCheckable, AndTheRealPolygonsKeptTheirs) {
    // LA OTRA MITAD, y es la que impide «arreglarlo» rechazando más. La
    // condición nueva no le quitó el recuento a NINGUNO de los 106 polígonos del
    // banco: el margen más estrecho son las tuercas —el círculo se aparta 1,03
    // veces lo que el polígono, porque un hexágono de 80 px se parece bastante a
    // su circunferencia— y el suelo está en 0,8.
    //
    // Ese margen de 0,23 es la razón de que esto se compruebe sobre el banco
    // entero y no sobre una figura de laboratorio: si alguien sube el factor
    // «para asegurar», las cien tuercas se quedan sin poder comprobar que
    // siguen teniendo seis caras, y aquí se ve.
    int polygons = 0;
    int keptTheCount = 0;
    double worstRatio = 1e9;
    for (const auto* photo : {"producto-tuercas-prueba.jpg", "Producto_Tuerca_Liv_02.jpg",
                              "arandelas-2.png", "rosca-1.png"}) {
        const cv::Mat image =
            cv::imread(std::string("C:/Users/furro/Pictures/IMG-MC/") + photo,
                       cv::IMREAD_COLOR);
        if (image.empty()) {
            GTEST_SKIP() << "sin banco de fotos";
        }
        vision::PipelineConfig config;
        config.segmentation.recoverHighlightsBy = 12;
        auto all = vision::analyzeFrames(image, config);
        if (!all.isOk()) {
            continue;
        }
        for (const auto& piece : all.value()) {
            const cv::Mat mask =
                vision::pieceMaskWithHoles(image, piece.mask, config.segmentation);
            const auto shape = vision::classifyShape(piece.contour.points, mask);
            if (shape.kind != vision::ShapeKind::Polygon || shape.sides < 3) {
                continue;
            }
            ++polygons;
            const auto stable = vision::stableSideCountOf(piece.contour.points);
            if (!stable.roundIsABetterFit) {
                ++keptTheCount;
            }
            if (stable.deviation > 0.0) {
                worstRatio = std::min(worstRatio, stable.circleDeviation / stable.deviation);
            }
        }
    }
    std::printf("  [redondo] %d polígonos, %d sin que el círculo les gane; el margen más "
                "estrecho, círculo/polígono = %.2f\n",
                polygons, keptTheCount, worstRatio);
    ASSERT_GT(polygons, 50) << "el banco da muy pocos polígonos: esto no comprueba nada";
    EXPECT_EQ(keptTheCount, polygons)
        << "la condición de «mejor redondo» le quita el recuento a un polígono de verdad: "
           "está apretada de más, y una tuerca hexagonal se queda sin poder comprobar que "
           "sigue teniendo seis caras";
}

