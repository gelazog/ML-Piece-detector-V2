// QUÉ HERRAMIENTA LE TOCA A CADA PIEZA: LAS TRES COSAS A LA VEZ.
//
// Las herramientas, la medición automática y las fotos reales, cruzadas. Nació
// de una queja concreta del taller: «en unos tornillos, que tienen rosca, usa
// otras herramientas en lugar de las correctas, o se pasa». Las dos mitades de
// esa frase resultaron ciertas y medibles.
//
// LO QUE SE ENCONTRÓ AL MEDIR (todo publicado por las pruebas de abajo):
//
//   - La medición automática conocía SIETE de las treinta y dos clases de
//     herramienta. Ni la Rosca ni el Engranaje estaban entre ellas: cero
//     propuestas de rosca en tres fotos de rosca evidente, cero de engranaje en
//     el engranaje de veinte dientes.
//   - Y sí «se pasaba»: a `tornillo-2.png` le ofrecía NUEVE «Radio» y tres
//     reglas; al engranaje, NUEVE «Lado» —ocho de sus cuarenta flancos de
//     diente, elegidos por orden de lista—; a `rosca-1.png`, SEIS ángulos que
//     medían los seis 102°, que es el mismo flanco contado seis veces. Los
//     nombres lo delataban solos: «Espesor 117», «Lado 30», «Radio 42».
//   - La herramienta de Rosca NO SABÍA DECIR QUE NO. Con el eje trazado de punta
//     a punta decía que sí en las dieciséis fotos del banco —arandelas, tuercas
//     y cáncamos incluidos— con perlas como «paso=1,3 px». Su hermana la del
//     Engranaje decía que no en quince de dieciséis, y explicando por qué.
//
// VERDAD DE CAMPO, y de dónde sale cada número:
//
//   rosca-1.png     paso = 66,5 px. La propia imagen lleva impreso «1 pulgada»
//                   con su flecha y numera 6 hilos dentro. La flecha se midió
//                   por sus píxeles cian: de x=165 a x=564, 399 px. 399/6.
//   engranaje-1.png z = 20 dientes. Por dos caminos independientes: la
//                   autocorrelación del perfil radial da 20 con confianza 1,00,
//                   y contando picos salen 20 repartidos a ±0,5°.
//   tornillo-1.png  paso ≈ 34 px, contando las vueltas sobre la foto reglada.
//   tornillo-2.png  paso ≈ 32 px, igual.
//   tornillos-1.png paso ≈ 18 px (el tornillo alto, que es el que segmenta).
//
// Los tres últimos se contaron a ojo sobre la imagen ampliada y con rejilla, así
// que se comprueban con holgura. Los dos primeros no: esos salen de la propia
// imagen y de dos algoritmos que coinciden.

#include <gtest/gtest.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "inspection_editor/auto_measure.h"
#include "vision/geometry_features.h"
#include "vision/pipeline.h"
#include "vision/position_fixture.h"
#include "vision/shape_class.h"

using namespace pci;

namespace {

struct Scene {
    cv::Mat gray;
    cv::Mat mask;
    vision::Fixture fixture;
};

bool load(const char* file, Scene& scene) {
    const std::filesystem::path path =
        std::filesystem::path("C:/Users/furro/Pictures/IMG-MC") / file;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return false;
    }
    scene.gray = cv::imread(path.string(), cv::IMREAD_GRAYSCALE);
    if (scene.gray.empty()) {
        return false;
    }
    vision::PipelineConfig config;
    config.segmentation.recoverHighlightsBy = 12;
    const auto piece = vision::analyzeFrame(scene.gray, config);
    if (!piece.isOk()) {
        return false;
    }
    scene.mask = piece.value().mask;
    scene.fixture = piece.value().fixture;
    return !scene.mask.empty();
}

// QUÉ HAY EN CADA FOTO, mirándolas una por una.
//
// `pitch` y `teeth` valen 0 cuando esa pieza no lleva esa cosa; entonces lo que
// se comprueba es lo contrario, que NO se le proponga. Que es la mitad del valor
// de todo esto: una medición automática que ofrece rosca a una arandela es peor
// que una que no ofrece rosca a nada.
struct Photo {
    const char* file;
    double pitch;       // paso de rosca en px, 0 = esta pieza no lleva rosca a la vista
    double pitchSlack;  // holgura relativa admitida
    int teeth;          // dientes, 0 = no es un engranaje visto de cara
    const char* what;
};

const std::vector<Photo>& photos() {
    static const std::vector<Photo> all{
        {"rosca-1.png", 66.5, 0.05, 0, "varilla roscada de perfil, con su pulgada impresa"},
        {"tornillo-1.png", 34.0, 0.20, 0, "tornillo de cabeza hexagonal, caña roscada entera"},
        {"tornillo-2.png", 32.0, 0.20, 0, "tirafondo de cabeza hexagonal, rosca gruesa"},
        {"tornillos-1.png", 18.0, 0.25, 0, "tres tornillos; segmenta el más alto"},
        {"engranaje-1.png", 0.0, 0.0, 20, "rueda dentada de cara, con chavetero"},
        {"engranajes-1.jpg", 0.0, 0.0, 0, "dos ruedas dentadas que se solapan"},
        {"tornillo-ojo-3.png", 0.0, 0.0, 0, "un cáncamo"},
        {"tornillo-ojo-4.png", 0.0, 0.0, 0, "dos cáncamos que se tocan"},
        {"tornillo-ojo-5.png", 0.0, 0.0, 0, "cinco cáncamos"},
        {"arandelas-1.png", 0.0, 0.0, 0, "arandelas sobre fondo rojo"},
        {"arandelas-2.png", 0.0, 0.0, 0, "arandelas"},
        {"arandelas-3.jpg", 0.0, 0.0, 0, "arandelas"},
        {"arandelas-4.png", 0.0, 0.0, 0, "arandelas"},
        {"arandelas-5.png", 0.0, 0.0, 0, "arandelas"},
        {"producto-tuercas-prueba.jpg", 0.0, 0.0, 0, "tuercas hexagonales"},
        {"Producto_Tuerca_Liv_02.jpg", 0.0, 0.0, 0, "tuerca hexagonal"},
    };
    return all;
}

std::vector<cv::Point> biggestContour(const cv::Mat& mask) {
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
    if (contours.empty()) {
        return {};
    }
    return *std::max_element(contours.begin(), contours.end(), [](const auto& a, const auto& b) {
        return cv::contourArea(a) < cv::contourArea(b);
    });
}

const inspection::AutoProposal* findType(const std::vector<inspection::AutoProposal>& proposals,
                                         inspection::ToolType type) {
    for (const auto& p : proposals) {
        if (p.config.type == type) {
            return &p;
        }
    }
    return nullptr;
}

// El eje que sale de mirar la pieza sin más: el largo del rectángulo mínimo.
struct LongAxis {
    cv::Point2f tail;
    cv::Point2f dir;
    float length = 0.0F;
    float width = 0.0F;
};

LongAxis longAxisOf(const std::vector<cv::Point>& contour) {
    const cv::RotatedRect box = cv::minAreaRect(contour);
    const double angle = box.angle * CV_PI / 180.0;
    LongAxis axis;
    axis.dir = cv::Point2f(static_cast<float>(std::cos(angle)),
                           static_cast<float>(std::sin(angle)));
    axis.length = box.size.width;
    axis.width = box.size.height;
    if (box.size.height > box.size.width) {
        axis.dir = cv::Point2f(-axis.dir.y, axis.dir.x);
        axis.length = box.size.height;
        axis.width = box.size.width;
    }
    axis.tail = box.center - axis.dir * (axis.length / 2.0F);
    return axis;
}

}  // namespace

// --- 1) LA TABLA CRUZADA -----------------------------------------------------

TEST(ToolForThePiece, TheAutomaticOffersTheToolThePieceActuallyNeeds) {
    int looked = 0;
    for (const auto& photo : photos()) {
        Scene scene;
        if (!load(photo.file, scene)) {
            std::printf("  %-30s (no se puede cargar/segmentar)\n", photo.file);
            continue;
        }
        ++looked;
        const auto contour = biggestContour(scene.mask);
        const std::string shape =
            contour.empty()
                ? "?"
                : vision::shapeKindName(vision::classifyShape(contour, scene.mask).kind);

        const auto proposals = inspection::proposeTools(scene.gray, scene.mask, scene.fixture);
        std::map<std::string, int> byType;
        for (const auto& p : proposals) {
            byType[inspection::toolTypeName(p.config.type)]++;
        }
        std::string summary;
        for (const auto& [name, count] : byType) {
            summary += name + "x" + std::to_string(count) + " ";
        }
        std::printf("  %-30s %-20s %2zu cotas: %s\n", photo.file, shape.c_str(),
                    proposals.size(), summary.c_str());

        const auto* thread = findType(proposals, inspection::ToolType::Thread);
        const auto* gear = findType(proposals, inspection::ToolType::Gear);

        if (photo.pitch > 0.0) {
            ASSERT_NE(thread, nullptr)
                << photo.file << " (" << photo.what
                << ") lleva rosca a la vista y la medición automática no la propone. Es la "
                   "queja original: usa otras herramientas en lugar de la correcta";
            EXPECT_NEAR(thread->measured, photo.pitch, photo.pitch * photo.pitchSlack)
                << photo.file
                << ": el paso propuesto no cuadra con el contado sobre la foto. "
                << thread->detail;
        } else {
            EXPECT_EQ(thread, nullptr)
                << photo.file << " (" << photo.what
                << ") no lleva rosca vista de perfil y se le propone una. Ofrecer una cota "
                   "de rosca a una pieza sin rosca es peor que no ofrecer ninguna: "
                << (thread != nullptr ? thread->detail : std::string());
        }

        if (photo.teeth > 0) {
            ASSERT_NE(gear, nullptr)
                << photo.file << " (" << photo.what
                << ") es una rueda dentada y no se le propone contar los dientes";
            EXPECT_EQ(static_cast<int>(std::lround(gear->measured)), photo.teeth)
                << photo.file << ": " << gear->detail;
        } else {
            EXPECT_EQ(gear, nullptr)
                << photo.file << " (" << photo.what
                << ") no es una rueda dentada vista de cara y se le propone un recuento de "
                   "dientes: "
                << (gear != nullptr ? gear->detail : std::string());
        }
    }
    EXPECT_GE(looked, 10) << "casi no se ha mirado ninguna foto: la carpeta no es la que se cree";
}

// --- 2) LA ROSCA SABE DECIR QUE NO -------------------------------------------

TEST(ToolForThePiece, TheThreadToolRefusesWhenThereIsNoThreadUnderTheAxis) {
    // El eje de punta a punta con la banda al ancho entero es el trazo INGENUO:
    // el que sale del rectángulo mínimo de la pieza sin mirar dónde está la
    // parte roscada. En un tornillo mete dentro la cabeza y el perfil deja de
    // repetirse; en una arandela no hay nada que repetir.
    //
    // Antes de este arreglo la herramienta contestaba que SÍ a las dieciséis.
    int refused = 0;
    int looked = 0;
    for (const auto& photo : photos()) {
        Scene scene;
        if (!load(photo.file, scene)) {
            continue;
        }
        const auto contour = biggestContour(scene.mask);
        if (contour.empty()) {
            continue;
        }
        ++looked;
        const LongAxis axis = longAxisOf(contour);
        inspection::ToolConfig config;
        config.type = inspection::ToolType::Thread;
        config.name = "rosca";
        config.toleranceMax = 1e9;
        inspection::ThreadGeometry g;
        g.axisFrom = vision::toPieceCoords(scene.fixture, axis.tail);
        g.axisTo = vision::toPieceCoords(scene.fixture, axis.tail + axis.dir * axis.length);
        g.searchBand = axis.width;
        config.geometryJson = inspection::toJson(inspection::ToolGeometry{g});
        const auto run = inspection::runTool(scene.gray, scene.fixture, config);

        ASSERT_TRUE(run.isOk());
        if (!run.value().ok) {
            ++refused;
        }
        EXPECT_FALSE(run.value().ok)
            << photo.file
            << ": con el eje trazado a lo bruto la herramienta publica un paso. Un paso "
               "que sale de un perfil que no se repite no es una medida, y con las "
               "tolerancias abiertas se llevaría un OK verde: "
            << run.value().detail;
    }
    std::printf("  [rosca] eje ingenuo: %d de %d fotos rechazadas\n", refused, looked);
    EXPECT_GE(looked, 10);
}

TEST(ToolForThePiece, TheThreadToolStillMeasuresWhenTheAxisIsOnTheThread) {
    // La otra mitad, y la que impide que «saber decir que no» degenere en «decir
    // que no a todo»: con el eje sobre la parte roscada tiene que MEDIR, y medir
    // bien.
    Scene scene;
    if (!load("rosca-1.png", scene)) {
        GTEST_SKIP() << "no está el banco de fotos";
    }
    const auto contour = biggestContour(scene.mask);
    ASSERT_FALSE(contour.empty());
    const LongAxis axis = longAxisOf(contour);

    inspection::ToolConfig config;
    config.type = inspection::ToolType::Thread;
    config.name = "rosca";
    config.toleranceMax = 1e9;
    inspection::ThreadGeometry g;
    g.axisFrom = vision::toPieceCoords(scene.fixture, axis.tail + axis.dir * (axis.length * 0.05F));
    g.axisTo = vision::toPieceCoords(scene.fixture, axis.tail + axis.dir * (axis.length * 0.95F));
    g.searchBand = axis.width / 2.0F;
    config.geometryJson = inspection::toJson(inspection::ToolGeometry{g});
    const auto run = inspection::runTool(scene.gray, scene.fixture, config);
    ASSERT_TRUE(run.isOk());
    std::printf("  [rosca] %s\n", run.value().detail.c_str());
    ASSERT_TRUE(run.value().ok)
        << "sobre la varilla roscada, con el eje encima de la rosca, la herramienta se "
           "niega. Entonces no sabe decir que no: sabe decir que no a todo. "
        << run.value().detail;

    // 399 px de pulgada, medidos sobre los píxeles cian de la propia flecha,
    // divididos entre los 6 hilos que la imagen numera dentro de esa pulgada.
    constexpr double kPrintedPitchPx = 399.0 / 6.0;
    EXPECT_NEAR(run.value().measured, kPrintedPitchPx, kPrintedPitchPx * 0.05)
        << "la foto lleva impreso cuántos hilos hay en una pulgada y el paso medido no "
           "cuadra con ellos: "
        << run.value().detail;
}

TEST(ToolForThePiece, AFlankOfZeroDegreesIsNotAnAngleAndIsNotWrittenAsOne) {
    // `flankAngleDeg` devuelve cero cuando se rinde, y ese cero se escribía con
    // dos decimales —«flanco=0.00°»— en catorce de las dieciséis fotos. Un
    // flanco de 0° sería una rosca de paredes verticales: no existe.
    Scene scene;
    if (!load("tornillo-1.png", scene)) {
        GTEST_SKIP() << "no está el banco de fotos";
    }
    const auto proposals = inspection::proposeTools(scene.gray, scene.mask, scene.fixture);
    const auto* thread = findType(proposals, inspection::ToolType::Thread);
    ASSERT_NE(thread, nullptr);
    std::printf("  [rosca] %s\n", thread->detail.c_str());
    EXPECT_EQ(thread->detail.find("flanco=0.00"), std::string::npos)
        << "se está publicando un flanco de cero grados como si fuera una medida: "
        << thread->detail;
}

// --- 3) LO QUE «SE PASABA» ---------------------------------------------------

TEST(ToolForThePiece, APeriodicRimDoesNotGetItsTeethOfferedOneByOne) {
    // El engranaje tiene veinte dientes y por tanto unos cuarenta flancos. La
    // descomposición del contorno los devuelve todos, y con un tope de doce
    // propuestas cualquier docena que se elija de ahí es una MUESTRA ARBITRARIA:
    // se ofrecían «Lado 2», «Lado 7», «Lado 8», «Lado 17», «Lado 22», «Lado 25»,
    // «Lado 30»..., con el número del nombre siendo un índice interno que al
    // operador no le dice nada. Y de paso se llevaban el presupuesto entero.
    Scene scene;
    if (!load("engranaje-1.png", scene)) {
        GTEST_SKIP() << "no está el banco de fotos";
    }
    const auto proposals = inspection::proposeTools(scene.gray, scene.mask, scene.fixture);
    int sides = 0;
    int arcs = 0;
    for (const auto& p : proposals) {
        if (p.config.name.rfind("Lado ", 0) == 0) {
            ++sides;
        }
        if (p.config.type == inspection::ToolType::Arc) {
            ++arcs;
        }
    }
    std::printf("  [engranaje] %zu cotas, %d «Lado», %d arcos\n", proposals.size(), sides, arcs);
    EXPECT_EQ(sides, 0) << "se siguen ofreciendo flancos de diente sueltos como si fueran "
                           "cotas de la pieza";
    EXPECT_LE(proposals.size(), 6U)
        << "la lista sigue llena: lo que hace que doce propuestas no se revisen es "
           "justamente que la mayoría no signifiquen nada";
}

TEST(ToolForThePiece, ARegularPolygonStillGetsEveryOneOfItsSides) {
    // La otra cara, y por qué esto no se arregló colapsando los valores que se
    // repiten: las seis caras de una tuerca hexagonal miden lo mismo y son seis
    // cotas, porque cada una puede salirse de tolerancia por su cuenta. Fundirlas
    // en una dejaría cinco sin comprobar.
    //
    // La diferencia con los dientes no es que se repitan: es que seis caras son
    // TODAS las que hay, y ocho flancos de cuarenta son una muestra. Hubo una
    // versión de esto que colapsaba por valor repetido y se llevó por delante los
    // ocho lados de un octógono y los doce de un dodecágono; la cazaron dos
    // pruebas que ya existían y se borró en vez de ajustarle el umbral.
    Scene scene;
    if (!load("Producto_Tuerca_Liv_02.jpg", scene)) {
        GTEST_SKIP() << "no está el banco de fotos";
    }
    const auto proposals = inspection::proposeTools(scene.gray, scene.mask, scene.fixture);
    int sides = 0;
    for (const auto& p : proposals) {
        if (p.config.name.rfind("Lado ", 0) == 0) {
            ++sides;
        }
    }
    std::printf("  [tuerca hexagonal] %zu cotas, %d «Lado»\n", proposals.size(), sides);
    EXPECT_GE(sides, 5) << "una tuerca hexagonal se ha quedado sin las cotas de sus caras: el "
                           "filtro de piezas periódicas se la está comiendo";
    EXPECT_EQ(findType(proposals, inspection::ToolType::Gear), nullptr)
        << "un hexágono repite su radio seis veces por vuelta, igual que un engranaje de "
           "seis dientes. Si se le propone contar dientes, el filtro no distingue una "
           "tuerca de una rueda";
}

TEST(ToolForThePiece, AThreadedBoltDoesNotGetItsCrestsOfferedAsRadii) {
    // El mismo criterio que con los dientes, y aquí hubo que aprenderlo dos
    // veces.
    //
    // Hubo una versión que apagaba solo lo que caía DENTRO del tramo de eje
    // sobre el que la Rosca había conseguido medir, para conservar las caras y
    // las esquinas de la cabeza del tornillo, que son cotas de verdad. La idea
    // era buena y la medición la tumbó: ese tramo no delimita la rosca.
    //
    // Descomponiendo `tornillo-1.png`, la rosca va del 0 % al 89 % del eje
    // —tramos de 0,6 a 0,9 pasos, uno cada 3,5 % del eje, que es el paso— y la
    // cabeza está del 89 % al 100 %, con tramos de 2,5 a 3,8 pasos. La
    // colocación que ganó fue la del 30 % al 100 %: metía la cabeza entera y
    // dejaba fuera el primer 30 % de rosca. Volvían NUEVE arcos sentados al 4,
    // 8, 11, 15, 18, 22 y 25 % del eje, separados exactamente un paso, llamados
    // «Radio 14», «Radio 15», «Radio 16»...
    //
    // Esta prueba existe para que eso no vuelva. El precio —el tornillo se queda
    // sin las cotas de su cabeza— está asumido y dicho en el README.
    for (const auto& photo : photos()) {
        if (photo.pitch <= 0.0) {
            continue;
        }
        Scene scene;
        if (!load(photo.file, scene)) {
            continue;
        }
        const auto proposals =
            inspection::proposeTools(scene.gray, scene.mask, scene.fixture);
        int arcs = 0;
        int sides = 0;
        for (const auto& p : proposals) {
            if (p.config.type == inspection::ToolType::Arc) {
                ++arcs;
            }
            if (p.config.name.rfind("Lado ", 0) == 0) {
                ++sides;
            }
        }
        std::printf("  %-22s %zu cotas, %d arcos, %d «Lado»\n", photo.file, proposals.size(),
                    arcs, sides);
        EXPECT_EQ(arcs, 0) << photo.file
                           << ": se están ofreciendo crestas de la rosca como radios de "
                              "redondeo, que es exactamente el «se pasa» de la queja";
        EXPECT_EQ(sides, 0) << photo.file << ": se están ofreciendo flancos de filete como "
                               "caras de la pieza";
        EXPECT_LE(proposals.size(), 6U) << photo.file << ": la lista vuelve a estar llena";
    }
}

TEST(ToolForThePiece, TwoOverlappingGearsAreRefusedAndThatIsTheRightAnswer) {
    // `engranajes-1.jpg` son DOS ruedas dentadas vistas de cara y se solapan.
    // Miradas de cerca, cada una tiene unos treinta dientes y nueve agujeros de
    // aligeramiento.
    //
    // Con la detección por defecto, la segmentación las funde en UNA pieza de
    // 210x406 —una rueda sola daría una caja cuadrada— y el Engranaje se niega.
    // Encendiendo «Separar las piezas que se tocan» salen dos piezas, pero el
    // separador corta por donde se solapan y se lleva dientes por delante: la
    // caja de una queda en 203x184 y el recuento deja de ser estable (sale 30 con
    // el radio interior al 70 % del exterior y 31 al 88 %).
    //
    // Así que la herramienta tiene razón al negarse, y esta prueba lo fija. Los
    // dientes son la identidad de la rueda: uno de más o de menos ya es otra
    // pieza, y no hay forma de contarlos con fiabilidad en esta foto. Publicar
    // un número aquí sería peor que no publicar ninguno.
    Scene scene;
    if (!load("engranajes-1.jpg", scene)) {
        GTEST_SKIP() << "no está el banco de fotos";
    }
    const auto contour = biggestContour(scene.mask);
    ASSERT_FALSE(contour.empty());
    const cv::RotatedRect box = cv::minAreaRect(contour);
    const double aspect = std::max(box.size.width, box.size.height) /
                          std::max(1.0F, std::min(box.size.width, box.size.height));
    std::printf("  [dos ruedas] una sola pieza de %.0fx%.0f (relación %.2f)\n", box.size.width,
                box.size.height, aspect);
    EXPECT_GT(aspect, 1.5)
        << "la segmentación ya no funde las dos ruedas; entonces esta prueba mide otra "
           "cosa y hay que volver a mirarla";

    const auto proposals = inspection::proposeTools(scene.gray, scene.mask, scene.fixture);
    EXPECT_EQ(findType(proposals, inspection::ToolType::Gear), nullptr)
        << "se está publicando un recuento de dientes sobre dos ruedas fundidas en una "
           "sola silueta";
}
