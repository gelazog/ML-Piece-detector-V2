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
