// CUÁNTAS PIEZAS HAY DE VERDAD EN CADA FOTO, CONTADAS MIRÁNDOLAS.
//
// El banco no tenía ni un recuento contrastado: se sabía cuántas piezas
// encuentra la aplicación, no cuántas hay. Y esas dos cifras se separan mucho
// más de lo que parecía, porque el área mínima de fábrica —el 0,5 % de la
// imagen— se lleva por delante las piezas pequeñas en cuanto la bandeja mezcla
// tamaños.
//
// Las cuatro fotos de aquí abajo son las que se pueden contar sin discusión: se
// ven todas las piezas, separadas y enteras. Las demás del banco no entran a
// propósito —`arandelas-1` y `arandelas-3` tienen unas veinte arandelas de
// tamaños muy distintos y contarlas «a ojo» sería inventarse la vara, que en
// este proyecto ya pasó una vez.
//
// Lo que fija esta prueba:
//
//   1. Que la aplicación acierta el recuento donde puede. Cien tuercas en una
//      bandeja de 10x10: cien. Eso no es poca cosa y no estaba comprobado.
//   2. Que lo que NO encuentra queda contado, no perdido. En `arandelas-4.png`
//      hay dieciséis piezas —cuatro anillos y doce tornillos— y con el mínimo de
//      fábrica salen cuatro: los doce tornillos caen por pequeños. La aplicación
//      tiene que decir «doce», que es lo que permite bajar el ajuste a sabiendas.
//   3. Y que bajándolo aparecen EXACTAMENTE las dieciséis. Sin esto, el punto 2
//      sería un consuelo: saber que se cayeron doce no sirve si al bajar el
//      listón salen treinta manchas de ruido.

#include <gtest/gtest.h>

#include <opencv2/imgcodecs.hpp>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "vision/pipeline.h"

using namespace pci;

namespace {

struct Countable {
    const char* file;
    int pieces;      // contadas mirando la foto
    const char* what;
};

}  // namespace

TEST(PieceCount, TheBankIsCountedAndWhatFallsOutIsSaid) {
    const std::vector<Countable> photos{
        {"producto-tuercas-prueba.jpg", 100, "bandeja de 10x10 tuercas autoblocantes"},
        {"tornillo-ojo-5.png", 5, "cinco cáncamos separados"},
        {"tornillos-1.png", 3, "tres tornillos de cabeza hexagonal"},
        {"tornillo-ojo-4.png", 2, "dos cáncamos que se tocan"},
    };

    int looked = 0;
    for (const auto& shot : photos) {
        const std::filesystem::path path =
            std::filesystem::path("C:/Users/furro/Pictures/IMG-MC") / shot.file;
        const cv::Mat image = cv::imread(path.string(), cv::IMREAD_COLOR);
        if (image.empty()) {
            continue;
        }
        ++looked;
        vision::PipelineConfig config;
        config.segmentation.recoverHighlightsBy = 12;
        int tooSmall = 0;
        auto found = vision::analyzeFrames(image, config, &tooSmall);
        ASSERT_TRUE(found.isOk()) << shot.file << ": " << found.error().message;
        const int seen = static_cast<int>(found.value().size());
        std::printf("  [recuento] %-30s hay %3d, encuentra %3d (%d pequeñas)  %s\n",
                    shot.file, shot.pieces, seen, tooSmall, shot.what);
        EXPECT_EQ(seen, shot.pieces)
            << shot.file << ": hay " << shot.pieces << " piezas (" << shot.what
            << ") y encuentra " << seen;
    }
    if (looked == 0) {
        GTEST_SKIP() << "las fotos del usuario no están en esta máquina";
    }
    EXPECT_EQ(looked, static_cast<int>(photos.size()))
        << "faltan fotos del banco: el recuento no se ha comprobado entero";
}

TEST(PieceCount, WhatTheMinimumAreaLeavesOutIsCountedAndRecoverable) {
    const std::filesystem::path path{
        "C:/Users/furro/Pictures/IMG-MC/arandelas-4.png"};
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        GTEST_SKIP() << "las fotos del usuario no están en esta máquina";
    }
    const cv::Mat image = cv::imread(path.string(), cv::IMREAD_COLOR);
    ASSERT_FALSE(image.empty());

    // Contadas mirando la foto: cuatro anillos separadores (taladro central y
    // seis agujeros cada uno) y doce tornillos en fila a la derecha.
    constexpr int kPiecesInThePhoto = 16;

    vision::PipelineConfig factory;
    factory.segmentation.recoverHighlightsBy = 12;
    int tooSmall = 0;
    auto asShipped = vision::analyzeFrames(image, factory, &tooSmall);
    ASSERT_TRUE(asShipped.isOk()) << asShipped.error().message;
    const int seen = static_cast<int>(asShipped.value().size());
    std::printf("  [recuento] arandelas-4: hay %d, de fábrica salen %d y se dicen %d "
                "pequeñas\n",
                kPiecesInThePhoto, seen, tooSmall);

    // Lo que importa no es que el mínimo deje fuera los tornillos —es su
    // trabajo, el mismo que impide que las letras de un rótulo se cuenten como
    // piezas— sino que NINGUNA se pierda en silencio.
    EXPECT_EQ(seen + tooSmall, kPiecesInThePhoto)
        << "de las " << kPiecesInThePhoto << " piezas de la foto, la aplicación enseña "
        << seen << " y declara " << tooSmall << " pequeñas: " << kPiecesInThePhoto - seen -
               tooSmall
        << " se han perdido sin que nadie las cuente";

    // Y bajando el listón salen todas, sin ruido de propina: si al aflojar el
    // ajuste aparecieran treinta manchas, decirle al operador que lo baje sería
    // mandarlo a un sitio peor.
    vision::PipelineConfig finer = factory;
    finer.minAreaFraction = 0.0005;
    int stillTooSmall = 0;
    auto withAll = vision::analyzeFrames(image, finer, &stillTooSmall);
    ASSERT_TRUE(withAll.isOk()) << withAll.error().message;
    std::printf("  [recuento] arandelas-4: con el mínimo al 0,05 %% salen %d (%d pequeñas)\n",
                static_cast<int>(withAll.value().size()), stillTooSmall);
    EXPECT_EQ(static_cast<int>(withAll.value().size()), kPiecesInThePhoto)
        << "bajando el área mínima tienen que salir las dieciséis y ninguna más";
}

// «0,5 %» NO ES UNA UNIDAD CON LA QUE SE PUEDA DECIDIR.
//
// El área mínima manda de verdad —de ella depende que una mancha sea una pieza o
// no exista— y se pedía en fracción de la imagen. Nadie mira una tuerca y piensa
// «esto es el 0,4 % del encuadre».
//
// Y hay que decidirlo de verdad: con el valor de fábrica, `arandelas-4.png`
// enseña 4 piezas de 16 porque los doce tornillos no llegan al mínimo. El
// operador que abre este ajuste para arreglarlo no tiene forma de saber si 0,5
// es mucho o poco para lo que está mirando.
//
// Traducido a píxeles sí: «una mancha de unos 39×39 px no llega a pieza» se
// compara de un vistazo con la pieza del vídeo. Se da el LADO del cuadrado y no
// solo el área, porque un área en px² tampoco se imagina.
//
// Lo que esta prueba cuida además: que sin imagen no se traduzca nada. Inventar
// un tamaño de referencia sería dar una equivalencia que no vale para la cámara
// que haya puesta — la misma familia de fallo que decir «poco firme» de un
// recuento que nadie ha medido.

#include <QApplication>
#include <QDoubleSpinBox>
#include <QLabel>

#include "ui/configure_dialog.h"
#include "ui/detection_page.h"


// Y LO QUE DE VERDAD TRAE AL OPERADOR A ESTE AJUSTE: CUÁNTAS ENTRAN.
//
// La equivalencia en píxeles dice qué es el número; lo que se quiere saber es
// qué HACE. La pregunta que trae aquí es «se ven 4 piezas y hay 16», y se
// contesta sin coste: las áreas de todas las manchas ya las calculó el análisis
// que se está viendo, así que esto es contar en una lista ordenada.
//
// Volver a segmentar en cada tecla habría costado un frame entero por pulsación
// y, peor, podría dar un resultado distinto del que hay en pantalla — el ajuste
// diría una cosa y el vídeo otra.
//
// Los números de esta prueba son los de `arandelas-4.png`, medidos: dieciséis
// manchas, cuatro anillos de unos 28 000 px² y doce tornillos de unos 700.
TEST(DetectionPage, ItSaysHowManyPiecesWouldEnterWithThatValue) {
    ui::ConfigureDialog::Inputs inputs;
    inputs.minAreaFraction = 0.005;   // 1505 px² en esta imagen
    inputs.frameSize = QSize(631, 477);
    for (int i = 0; i < 4; ++i) {
        inputs.blobAreas.push_back(28000.0);  // los anillos
    }
    for (int i = 0; i < 12; ++i) {
        inputs.blobAreas.push_back(700.0);  // los tornillos
    }
    ui::ConfigureDialog dialog(inputs);
    auto* hint = dialog.findChild<QLabel*>(QStringLiteral("minAreaHint"));
    ASSERT_NE(hint, nullptr);
    std::printf("  [área mínima] %s\n", hint->text().toStdString().c_str());
    EXPECT_NE(hint->text().indexOf(QStringLiteral("4 y se quedan fuera 12")), -1)
        << "no dice qué hace este valor con lo que hay delante: «"
        << hint->text().toStdString() << "»";

    // Y al bajarlo, los doce entran. Ese es el gesto entero.
    for (auto* box : dialog.detectionPage()->findChildren<QDoubleSpinBox*>()) {
        if (box->suffix().contains(QStringLiteral("%")) && box->value() < 5.0) {
            box->setValue(0.05);  // 150 px²
            break;
        }
    }
    QApplication::processEvents();
    std::printf("  [área mínima] al 0,05 %%: %s\n", hint->text().toStdString().c_str());
    EXPECT_NE(hint->text().indexOf(QStringLiteral("las 16")), -1)
        << "bajando el mínimo tenían que entrar las dieciséis: «"
        << hint->text().toStdString() << "»";
}

TEST(DetectionPage, TheMinimumAreaIsAlsoSaidInPixelsOfThisImage) {
    ui::ConfigureDialog::Inputs inputs;
    inputs.minAreaFraction = 0.005;
    inputs.frameSize = QSize(631, 477);  // el tamaño de `arandelas-4.png`
    ui::ConfigureDialog dialog(inputs);

    auto* hint = dialog.findChild<QLabel*>(QStringLiteral("minAreaHint"));
    ASSERT_NE(hint, nullptr) << "no hay equivalencia en píxeles del área mínima";
    // `isHidden` y no `isVisible`: la pestaña de Detección no es la que se abre
    // por delante, así que sus hijos no están «visibles» aunque nadie los haya
    // escondido. Lo que se comprueba es que la etiqueta NO se haya apagado.
    ASSERT_FALSE(hint->isHidden()) << "la equivalencia está apagada habiendo imagen";
    std::printf("  [área mínima] %s\n", hint->text().toStdString().c_str());
    // 0,5 % de 631x477 son 1505 px², o sea un cuadrado de 39 px de lado.
    EXPECT_NE(hint->text().indexOf(QStringLiteral("1505")), -1)
        << "no dice cuántos píxeles son: «" << hint->text().toStdString() << "»";
    EXPECT_NE(hint->text().indexOf(QStringLiteral("39×39")), -1)
        << "no dice el lado de la mancha, que es lo que se compara con la pieza: «"
        << hint->text().toStdString() << "»";

    // Y sigue al ajuste mientras se toca, que es cuando hace falta.
    auto* spin = dialog.detectionPage()->findChild<QDoubleSpinBox*>();
    ASSERT_NE(spin, nullptr);
    const QString before = hint->text();
    for (auto* box : dialog.detectionPage()->findChildren<QDoubleSpinBox*>()) {
        if (box->suffix().contains(QStringLiteral("%")) && box->value() < 5.0) {
            box->setValue(0.05);
            break;
        }
    }
    QApplication::processEvents();
    EXPECT_NE(hint->text(), before)
        << "la equivalencia no cambia al mover el ajuste: entonces está mintiendo en "
           "cuanto se toca";
    std::printf("  [área mínima] al 0,05 %%: %s\n", hint->text().toStdString().c_str());
}

// Sin imagen todavía, no se traduce.
TEST(DetectionPage, WithNoFrameYetItPromisesNothing) {
    ui::ConfigureDialog::Inputs inputs;
    inputs.minAreaFraction = 0.005;
    ui::ConfigureDialog dialog(inputs);
    auto* hint = dialog.findChild<QLabel*>(QStringLiteral("minAreaHint"));
    ASSERT_NE(hint, nullptr);
    EXPECT_TRUE(hint->isHidden())
        << "sin frame no se sabe de qué tamaño es la imagen, y una equivalencia "
           "inventada vale menos que ninguna: «"
        << hint->text().toStdString() << "»";
}
