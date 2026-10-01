// EL CONTORNO SOBRE LA FOTO NO SE VE POR SU COLOR, SE VE POR SU HALO.
//
// Una línea de color encima de una FOTO no tiene contraste garantizado: depende
// de lo que haya debajo, y debajo puede haber cualquier cosa. Medido sobre el
// banco, el contraste del rojo del contorno contra los píxeles por los que
// pasa:
//
//     foto            rojo p05   rojo mediana   con halo p05   mediana
//     arandelas-1        1,02        1,22          5,57         7,27
//     arandelas-5        1,07        1,67          2,55         9,17
//     engranaje-1        1,90        2,32         11,33        13,83
//     tornillo-1         1,59        2,13          9,46        12,67
//     tornillos-1        2,15        2,64         12,84        15,74
//
// El color solo no llega ni al 3:1 que necesita un elemento gráfico. Con el
// borde oscuro debajo pasa de 5 a 15. O sea que lo que hace visible el contorno
// no es su color: es el halo.
//
// EL LIENZO DEL EDITOR YA LO HACÍA. No lo hacían el vídeo en vivo —donde el
// operador mira todo el día— ni el informe de inspección, que es donde se
// decide si una pieza se rechaza.
//
// Y se probó primero lo que parecía obvio: cambiar el rojo por el de veredicto
// (`kBadOnDark`, más claro, con su contraste medido sobre superficie oscura).
// Sale PEOR en las siete fotos —1,17 contra 1,22 de mediana en la peor— porque
// es más claro y las piezas son claras. El color no era el problema.

#include <gtest/gtest.h>

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QPolygonF>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <opencv2/core.hpp>

#include <vector>

#include "inspection_editor/canvas/editor_canvas.h"
#include "ui/analysis_overlay.h"
#include "ui/video_widget.h"

using namespace pci;

namespace {

// LO MÁS OSCURO QUE HAY ENCIMA DE LA IMAGEN, en su canal más alto.
//
// El criterio costó dos vueltas y las dos merecen quedar escritas.
//
// La primera fue «cuenta los píxeles con los tres canales por debajo de 90», y
// daba CERO con el halo dibujándose perfectamente: el halo va con alfa 150, así
// que sobre un fondo de 245 aterriza en 245·(1−150/255) ≈ 101 — más oscuro que
// el fondo, pero no «oscuro». La prueba medía mal y habría culpado al código.
//
// La segunda fue contar los píxeles por debajo de 160, y salían CUATRO: el
// antialiasing reparte el halo en una falda de tonos y solo su núcleo llega tan
// abajo. Cuatro píxeles es una cifra que cambia con cualquier detalle del
// dibujo, y una prueba que se apoya en eso falla el día menos pensado sin que
// nada esté roto.
//
// Lo que sí separa limpio es UNA cifra: lo más oscuro que aparece. Sin halo, lo
// más oscuro encima de un fondo 245 sería la propia línea de color —el verde
// (0,220,0) llega a 220 en su canal más alto, el rojo a 255—. Con halo baja a
// ~100. Entre 100 y 220 hay sitio de sobra para un tope que no sea delicado.
int darkestOver(const QImage& shot, const QRect& area) {
    int darkest = 255;
    for (int y = area.top(); y < area.bottom(); ++y) {
        for (int x = area.left(); x < area.right(); ++x) {
            const QColor pixel = shot.pixelColor(x, y);
            const int brightest =
                std::max(pixel.red(), std::max(pixel.green(), pixel.blue()));
            darkest = std::min(darkest, brightest);
        }
    }
    return darkest;
}

}  // namespace

TEST(OverlayHalo, TheLiveContourCarriesItsHaloLikeTheEditorDoes) {
    ui::VideoWidget video;
    video.resize(640, 480);

    // Escena CLARA: encima, una línea de color se pierde. Es la peor situación
    // para el contorno y la más común en este taller — las piezas se fotografían
    // sobre mesa blanca.
    QImage bright(320, 240, QImage::Format_RGB888);
    bright.fill(QColor(245, 245, 245));
    video.setFrame(bright);

    ui::AnalysisOverlay overlay;
    overlay.valid = true;
    overlay.analysed = true;
    // El dibujo solo se pinta si el overlay habla de ESTE frame: sin esta línea
    // la prueba pasaría por no dibujarse nada, que es la trampa de siempre.
    overlay.frameSize = bright.size();
    overlay.contour << QPointF(80, 60) << QPointF(240, 60) << QPointF(240, 180)
                    << QPointF(80, 180);
    overlay.centroid = QPointF(160, 120);
    overlay.angleDeg = 0.0;
    video.setOverlay(overlay);

    QImage shot(video.size(), QImage::Format_RGB888);
    video.render(&shot);

    // SOLO DENTRO DE LA IMAGEN. Contando el widget entero, su propio fondo es
    // casi negro y la comprobación pasaría sin que hubiera ningún halo — que es
    // exactamente la trampa que ya se documentó al escribir la prueba del
    // lienzo del editor.
    const QRect inside(video.width() / 4, video.height() / 4, video.width() / 2,
                       video.height() / 2);
    const int darkest = darkestOver(shot, inside);
    std::printf("  [halo] sobre escena clara, lo más oscuro dibujado: %d\n",
                darkest);

    EXPECT_LT(darkest, 160)
        << "lo más oscuro que se dibuja encima de la imagen es " << darkest
        << ", o sea que no hay halo: solo está la línea de color. Sobre una foto su "
           "color solo da 1,2-2,6 de contraste contra lo que tiene debajo, así que "
           "sobre una pieza clara desaparece — y es lo que el operador mira todo el "
           "día.";
}

TEST(OverlayHalo, WithNoPieceThereIsNoContourAndSoNoHalo) {
    // La otra mitad: sin pieza no se dibuja contorno, así que tampoco puede
    // haber halo. Sin esto, la comprobación de arriba pasaría igual pintando
    // una mancha oscura en cualquier sitio.
    ui::VideoWidget video;
    video.resize(640, 480);
    QImage bright(320, 240, QImage::Format_RGB888);
    bright.fill(QColor(245, 245, 245));
    video.setFrame(bright);
    video.setOverlay(ui::AnalysisOverlay{});

    QImage shot(video.size(), QImage::Format_RGB888);
    video.render(&shot);
    const QRect inside(video.width() / 4, video.height() / 4, video.width() / 2,
                       video.height() / 2);
    const int darkest = darkestOver(shot, inside);
    std::printf("  [halo] sin pieza, lo más oscuro dibujado: %d\n", darkest);
    EXPECT_GT(darkest, 200) << "se está pintando algo oscuro encima de la imagen sin que "
                               "haya ninguna pieza que contornear";
}

// ---------------------------------------------------------------------------
// EL LIENZO DEL EDITOR: LO QUE SE DIBUJA, NO SOLO EL CONTORNO
// ---------------------------------------------------------------------------
//
// El contorno de la pieza llevaba halo desde hace tiempo. Lo demás que se pinta
// encima de la foto, no: las herramientas (cian, ámbar, rosa), la cota medida
// (verde o rojo), la zona de detección (ámbar a rayas), lo que se está
// trazando (blanco a rayas), la rejilla del tablero. Son colores claros a
// propósito —para verse sobre piezas oscuras— y por eso mismo se pierden sobre
// mesa blanca, que es el montaje normal: el cian del calibre (0,200,255) sobre
// un blanco 245 deja como canal más alto 245, o sea que no hay nada más oscuro
// que el propio fondo, y el ámbar de la zona da 1,1:1 contra él.
//
// Se mide igual que arriba: lo más oscuro que aparece cerca del trazo. Sin halo
// es el propio fondo (245) o el color de la línea; con halo baja a ~100.
namespace {

using pci::inspection::CaliperGeometry;
using pci::inspection::EditedTool;
using pci::inspection::EditorCanvas;
using pci::inspection::ToolRunResult;
using pci::inspection::ToolType;

// Escena blanca del MISMO tamaño que el lienzo, para que píxel de imagen y de
// pantalla coincidan al ajustar.
QImage whiteScene() {
    QImage white(800, 600, QImage::Format_RGB888);
    white.fill(QColor(245, 245, 245));
    return white;
}

QImage shotOf(EditorCanvas& canvas) {
    QImage shot(canvas.size(), QImage::Format_RGB888);
    canvas.render(&shot);
    return shot;
}

}  // namespace

TEST(OverlayHalo, AToolOnAWhiteTableCarriesItsHalo) {
    EditorCanvas canvas;
    canvas.resize(800, 600);
    canvas.setScene(whiteScene(), pci::vision::Fixture{});
    std::vector<EditedTool> tools(1);
    tools[0].config.id = 1;
    tools[0].config.type = ToolType::Caliper;
    tools[0].geometry = CaliperGeometry{{300.0F, 300.0F}, {500.0F, 300.0F}, 10.0F};
    canvas.setTools(&tools);

    const QImage shot = shotOf(canvas);
    const QRect strip(350, 285, 100, 30);
    int cyan = 0;
    for (int y = strip.top(); y < strip.bottom(); ++y) {
        for (int x = strip.left(); x < strip.right(); ++x) {
            const QColor c = shot.pixelColor(x, y);
            if (c.blue() > 200 && c.red() < 120) {
                ++cyan;
            }
        }
    }
    const int darkest = darkestOver(shot, strip);
    std::printf("  [halo] herramienta sobre mesa blanca: %d px de su color, lo más oscuro %d\n",
                cyan, darkest);
    EXPECT_GT(cyan, 50) << "no se está pintando la herramienta: la prueba no mide nada";
    EXPECT_LT(darkest, 160) << "la herramienta no lleva halo: su cian sobre mesa blanca no se ve";
}

TEST(OverlayHalo, TheMeasuredDimensionCarriesItsHalo) {
    EditorCanvas canvas;
    canvas.resize(800, 600);
    canvas.setScene(whiteScene(), pci::vision::Fixture{});
    ToolRunResult result;
    result.toolId = 1;
    result.name = "medida";
    result.ok = true;
    result.measured = 600.0;
    result.overlaySegments = {{cv::Point2f(100.0F, 450.0F), cv::Point2f(700.0F, 450.0F)}};
    canvas.setResults({result});

    // Lejos de la etiqueta, que va en una caja negra y aprobaría sola.
    const QImage shot = shotOf(canvas);
    const QRect strip(520, 440, 150, 20);
    const int darkest = darkestOver(shot, strip);
    std::printf("  [halo] cota que cumple sobre mesa blanca, lo más oscuro: %d\n", darkest);
    EXPECT_LT(darkest, 160) << "la cota no lleva halo: el verde de «cumple» sobre una pieza "
                               "clara no se distingue";
}

TEST(OverlayHalo, TheDetectionZoneCarriesItsHalo) {
    EditorCanvas canvas;
    canvas.resize(800, 600);
    canvas.setScene(whiteScene(), pci::vision::Fixture{});
    canvas.setDetectionRegion(true, cv::Rect(200, 150, 400, 300));

    const QImage shot = shotOf(canvas);
    const QRect strip(300, 140, 200, 20);
    const int darkest = darkestOver(shot, strip);
    std::printf("  [halo] zona de detección sobre mesa blanca, lo más oscuro: %d\n", darkest);
    EXPECT_LT(darkest, 160) << "la zona no lleva halo: su ámbar a rayas sobre blanco no se ve";
}

TEST(OverlayHalo, ANothingDrawnSceneStaysClean) {
    // La otra mitad: sin nada encima, nada oscuro. Si no, las tres de arriba
    // pasarían igual pintando una mancha en cualquier sitio.
    EditorCanvas canvas;
    canvas.resize(800, 600);
    canvas.setScene(whiteScene(), pci::vision::Fixture{});
    const QImage shot = shotOf(canvas);
    const int darkest = darkestOver(shot, QRect(200, 140, 400, 320));
    std::printf("  [halo] lienzo sin nada encima, lo más oscuro: %d\n", darkest);
    EXPECT_GT(darkest, 200);
}
