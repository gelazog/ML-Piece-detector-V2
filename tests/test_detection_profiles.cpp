// UN PERFIL DE DETECCIÓN TIENE QUE DEVOLVER TODO LO QUE SE LE DIO.
//
// Queja del taller: «sigue habiendo problemas con el color del fondo y el de la
// pieza, y el cómo detecta las piezas». Buscándolo apareció esto.
//
// La tabla `DetectionProfiles` guardaba CUATRO de los OCHO campos de
// `SegmentationOptions`. Se quedaban fuera:
//
//     method                 nivel de gris o canto de la pieza
//     splitTouchingPieces    separar las piezas que se rozan
//     recoverHighlightsBy    recuperar lo que el brillo se lleva
//     backgroundKey + color  separar por el color del fondo
//
// No es que alguien los olvidara de una vez: los cuatro se añadieron DESPUÉS de
// que existiera la tabla, cada uno en su momento, y ninguno se acordó de ella.
//
// El efecto es el peor de los posibles. El operador afina la detección, la ve
// funcionar, la guarda como perfil —«contraluz», «mesa roja»— y al volver a
// cargarla la mitad de lo que ajustó ha vuelto a fábrica. No falla ni avisa:
// solo detecta peor. Desde fuera se vive como «el programa va peor desde hace un
// tiempo», que es imposible de relacionar con haber cargado un perfil.
//
// La prueba usa valores DISTINTOS de los de fábrica en todos los campos: con los
// de fábrica puestos, un campo perdido daría el mismo resultado que uno
// conservado y esto pasaría sin comprobar nada. Es la misma disciplina que
// `test_configure_roundtrip.cpp`, que existe por la misma familia de fallo en la
// ventana de Configurar.

#include <gtest/gtest.h>

#include <QDir>
#include <QString>
#include <QTemporaryDir>

#include <cstdio>

#include "database/db.h"
#include "database/schema.h"
#include "repositories/detection_profile_repository.h"

using namespace pci;

namespace {

vision::SegmentationOptions nothingLikeTheFactory() {
    vision::SegmentationOptions options;
    options.method = vision::SegmentationMethod::Edges;
    options.manualThreshold = 137;
    options.polarity = vision::SegmentationPolarity::LightPiece;
    options.blurKernel = 7;
    options.morphKernel = 3;
    options.splitTouchingPieces = true;
    options.recoverHighlightsBy = 12;
    options.backgroundKey = vision::SegmentationOptions::BackgroundKey::Fixed;
    options.background = cv::Vec3b(77, 63, 238);  // el rojo del cartón, en BGR
    return options;
}

void expectSame(const vision::SegmentationOptions& back,
                const vision::SegmentationOptions& given, const char* how) {
    EXPECT_EQ(static_cast<int>(back.method), static_cast<int>(given.method))
        << how << ": el método no vuelve. Quien guardó «por el canto de la pieza» carga "
                  "«por nivel de gris» y sus piezas cincadas vuelven a salir partidas";
    EXPECT_EQ(back.manualThreshold, given.manualThreshold) << how << ": el umbral no vuelve";
    EXPECT_EQ(static_cast<int>(back.polarity), static_cast<int>(given.polarity))
        << how << ": la polaridad no vuelve";
    EXPECT_EQ(back.blurKernel, given.blurKernel) << how << ": el suavizado no vuelve";
    EXPECT_EQ(back.morphKernel, given.morphKernel) << how << ": la morfología no vuelve";
    EXPECT_EQ(back.splitTouchingPieces, given.splitTouchingPieces)
        << how << ": la separación de piezas que se tocan no vuelve";
    EXPECT_EQ(back.recoverHighlightsBy, given.recoverHighlightsBy)
        << how << ": la recuperación de brillos no vuelve, y con ella los tornillos "
                  "cincados vuelven a contarse como cinco piezas en vez de tres";
    EXPECT_EQ(static_cast<int>(back.backgroundKey), static_cast<int>(given.backgroundKey))
        << how << ": la clave de color de fondo no vuelve. Sobre una mesa de color eso "
                  "deja de ver todas las piezas que no son cromadas";
    // Canal a canal: cruzar BGR con RGB da un color parecido en pantalla y una
    // segmentación contra otra cosa.
    EXPECT_EQ(back.background[0], given.background[0]) << how << ": el azul del fondo";
    EXPECT_EQ(back.background[1], given.background[1]) << how << ": el verde del fondo";
    EXPECT_EQ(back.background[2], given.background[2]) << how << ": el rojo del fondo";
}

struct Bench {
    QTemporaryDir dir;
    std::unique_ptr<database::Db> db;
};

bool openBench(Bench& bench) {
    if (!bench.dir.isValid()) {
        return false;
    }
    auto opened = database::Db::open(
        QDir(bench.dir.path()).filePath(QStringLiteral("p.db")).toStdString());
    if (!opened.isOk()) {
        return false;
    }
    bench.db = std::move(opened.value());
    return database::migrate(*bench.db).isOk();
}

}  // namespace

TEST(DetectionProfiles, EverythingThatWasSavedComesBack) {
    Bench bench;
    ASSERT_TRUE(openBench(bench));
    repositories::DetectionProfileRepository profiles(*bench.db);

    const auto given = nothingLikeTheFactory();
    auto saved = profiles.save("mesa roja", given);
    ASSERT_TRUE(saved.isOk()) << saved.error().message;

    auto loaded = profiles.load(saved.value());
    ASSERT_TRUE(loaded.isOk()) << loaded.error().message;
    std::printf("  [perfil] «%s» guardado y releído\n", loaded.value().name.c_str());
    expectSame(loaded.value().options, given, "al cargarlo por id");
}

TEST(DetectionProfiles, TheListCarriesTheSameThingAsTheLoad) {
    // La lista y la carga son dos consultas distintas, y ahí es donde se cuela
    // este fallo: añadir un campo a una y olvidarlo en la otra no rompe nada
    // visible. El operador elige el perfil de un desplegable que se llena con la
    // LISTA, así que un campo que solo falte ahí también se pierde.
    Bench bench;
    ASSERT_TRUE(openBench(bench));
    repositories::DetectionProfileRepository profiles(*bench.db);

    const auto given = nothingLikeTheFactory();
    ASSERT_TRUE(profiles.save("contraluz", given).isOk());

    auto all = profiles.list();
    ASSERT_TRUE(all.isOk()) << all.error().message;
    ASSERT_EQ(all.value().size(), 1U);
    expectSame(all.value().front().options, given, "en la lista");
}

TEST(DetectionProfiles, SavingTwiceUnderTheSameNameKeepsTheNewValues) {
    // El guardado es un upsert por nombre. Si la parte de UPDATE se deja campos
    // fuera —que es fácil, porque va escrita aparte de la de INSERT— reguardar
    // un perfil dejaría los valores viejos en los campos olvidados, y eso es
    // todavía más difícil de ver: funciona la primera vez y no la segunda.
    Bench bench;
    ASSERT_TRUE(openBench(bench));
    repositories::DetectionProfileRepository profiles(*bench.db);

    vision::SegmentationOptions first;
    ASSERT_TRUE(profiles.save("puesto 1", first).isOk());

    const auto second = nothingLikeTheFactory();
    auto again = profiles.save("puesto 1", second);
    ASSERT_TRUE(again.isOk()) << again.error().message;

    auto loaded = profiles.load(again.value());
    ASSERT_TRUE(loaded.isOk());
    expectSame(loaded.value().options, second, "tras reguardar con el mismo nombre");
}
