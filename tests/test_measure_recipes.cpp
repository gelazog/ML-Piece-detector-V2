// UNA RECETA DE MEDICIÓN NO PUEDE INVENTAR COTAS.
//
// Petición de uso: «un conjunto personalizado de reglas para algunas piezas
// específicas —engranajes, círculos, piezas cuadradas, rectangulares— para tomar
// de mejor manera las medidas».
//
// El riesgo de una función así es exactamente el contrario del que parece. Lo
// fácil es que una receta se convierta en «forzar»: elegir «Engranaje» sobre
// una arandela y recibir un módulo. Ese número saldría —la herramienta lo
// calcularía— y sería basura con aspecto de medida, que es la clase de fallo
// que este proyecto lleva tres vueltas quitando: los «Radio 28 px» sobre una
// tuerca de esquinas vivas, los «Ángulo 164°» sobre una curva, los «8 lados»
// sobre una arandela.
//
// Así que una receta hace dos cosas y solo dos: acota QUÉ clases de cota se
// proponen, y dice que NO cuando la pieza no es de su familia. Lo segundo es lo
// que estas pruebas vigilan, porque lo primero se ve a simple vista y lo
// segundo es lo que alguien quitará el día que un operador pida «déjame
// forzarla».
//
// Y una tercera, que es la que impide que las recetas se conviertan en ajustes
// escondidos: SIN receta, todo sigue igual. La receta «Todas las cotas» tiene
// que dar exactamente lo mismo que no elegir ninguna.

#include <gtest/gtest.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <cstdio>
#include <string>
#include <vector>

#include "inspection_editor/measure_recipe.h"
#include "vision/contour_analysis.h"
#include "vision/pipeline.h"
#include "vision/shape_class.h"

using namespace pci;

namespace {

struct Sample {
    cv::Mat image;
    cv::Mat mask;
    vision::Fixture fixture;
    vision::ShapeClass shape;
    bool ok = false;
};

// La pieza mayor de una foto del banco, con su máscara y su figura — que es
// justo lo que el editor tiene delante cuando el operador pide medir.
Sample biggestPieceOf(const std::string& photo) {
    Sample sample;
    const cv::Mat image =
        cv::imread("C:/Users/furro/Pictures/IMG-MC/" + photo, cv::IMREAD_COLOR);
    if (image.empty()) {
        return sample;
    }
    vision::PipelineConfig config;
    config.segmentation.recoverHighlightsBy = 12;
    auto all = vision::analyzeFrames(image, config);
    if (!all.isOk() || all.value().empty()) {
        return sample;
    }
    const vision::PieceAnalysis* biggest = nullptr;
    double area = 0.0;
    for (const auto& piece : all.value()) {
        const double here = cv::contourArea(piece.contour.points);
        if (here > area) {
            area = here;
            biggest = &piece;
        }
    }
    if (biggest == nullptr) {
        return sample;
    }
    sample.image = image;
    sample.mask = vision::pieceMaskWithHoles(image, biggest->mask, config.segmentation);
    sample.fixture = biggest->fixture;
    sample.shape = vision::classifyShape(biggest->contour.points, sample.mask);
    sample.ok = true;
    return sample;
}

// Una pieza de la familia que se pida, buscada en el banco. Devuelve la primera
// que encaje: qué foto la trae no es lo que se comprueba.
Sample aPieceOfFamily(inspection::PieceFamily family) {
    for (const auto* photo : {"arandelas-4.png", "arandelas-1.png", "arandelas-3.jpg",
                              "producto-tuercas-prueba.jpg", "Producto_Tuerca_Liv_02.jpg",
                              "engranaje-1.png", "tornillo-1.png"}) {
        const cv::Mat image =
            cv::imread(std::string("C:/Users/furro/Pictures/IMG-MC/") + photo,
                       cv::IMREAD_COLOR);
        if (image.empty()) {
            continue;
        }
        vision::PipelineConfig config;
        config.segmentation.recoverHighlightsBy = 12;
        auto all = vision::analyzeFrames(image, config);
        if (!all.isOk()) {
            continue;
        }
        for (const auto& piece : all.value()) {
            Sample sample;
            sample.image = image;
            sample.mask = vision::pieceMaskWithHoles(image, piece.mask, config.segmentation);
            sample.fixture = piece.fixture;
            sample.shape = vision::classifyShape(piece.contour.points, sample.mask);
            if (inspection::familyOf(sample.shape) == family) {
                sample.ok = true;
                return sample;
            }
        }
    }
    return {};
}

int howManyNamed(const std::vector<inspection::AutoProposal>& proposals,
                 const std::string& prefix) {
    int count = 0;
    for (const auto& proposal : proposals) {
        if (proposal.config.name.rfind(prefix, 0) == 0) {
            ++count;
        }
    }
    return count;
}

}  // namespace

TEST(MeasureRecipes, EveryRecipeOnlyOffersTheCotasItPromises) {
    // Lo básico, y aun así hace falta: una receta que ofreciera algo fuera de su
    // lista sería un filtro que no filtra, y el operador que eligió «Engranaje»
    // se encontraría con lados en la tabla sin saber de dónde salieron.
    const Sample round = aPieceOfFamily(inspection::PieceFamily::Round);
    const Sample ring = aPieceOfFamily(inspection::PieceFamily::Ring);
    if (!round.ok && !ring.ok) {
        GTEST_SKIP() << "sin banco de fotos";
    }
    for (const auto& recipe : inspection::factoryRecipes()) {
        const Sample& sample =
            recipe.family == inspection::PieceFamily::Ring ? ring : round;
        if (!sample.ok) {
            continue;
        }
        const auto result = inspection::proposeWithRecipe(sample.image, sample.mask,
                                                          sample.fixture, recipe, 0.0);
        if (!result.applies) {
            continue;  // esa comprobación es la de más abajo
        }
        for (const auto& proposal : result.proposals) {
            EXPECT_TRUE(recipe.options.allows(proposal.config.type))
                << "la receta «" << recipe.name << "» propone «" << proposal.config.name
                << "», que no está entre las clases que dice traer";
        }
    }
}

TEST(MeasureRecipes, ARecipeForAnotherFamilySaysSoInsteadOfMeasuring) {
    // EL CORAZÓN DE ESTO. Si esta prueba desaparece, «receta» pasa a significar
    // «forzar», y una arandela puede recibir el entrecaras de una tuerca: un
    // número que sale, que se guarda con su tolerancia, y que no mide nada.
    const Sample ring = aPieceOfFamily(inspection::PieceFamily::Ring);
    if (!ring.ok) {
        GTEST_SKIP() << "sin banco de fotos";
    }
    const auto* hexagonal = inspection::recipeNamed("Tuerca hexagonal");
    ASSERT_NE(hexagonal, nullptr) << "la receta de fábrica ha cambiado de nombre, y el "
                                     "nombre es la clave con la que se guarda en la pieza";

    const auto result = inspection::proposeWithRecipe(ring.image, ring.mask, ring.fixture,
                                                       *hexagonal, 0.0);
    std::printf("  [receta] tuerca hexagonal sobre una arandela -> %s\n",
                result.why.c_str());
    EXPECT_FALSE(result.applies)
        << "la receta de la tuerca se aplica a una arandela: entonces «receta» quiere "
           "decir forzar, y el entrecaras de una pieza redonda es un número inventado";
    EXPECT_TRUE(result.proposals.empty())
        << "no aplica y aun así propone " << result.proposals.size() << " cotas";
    // Y que el motivo nombre LAS DOS COSAS: qué pedía la receta y qué se
    // reconoció. «No aplica» a secas deja al operador probando recetas a ver
    // cuál entra.
    EXPECT_NE(result.why.find("arandela"), std::string::npos)
        << "el motivo no dice qué se reconoció: " << result.why;
    EXPECT_NE(result.why.find("Tuerca hexagonal"), std::string::npos)
        << "el motivo no dice qué receta se intentó: " << result.why;
}

TEST(MeasureRecipes, TheWasherRecipeDoesNotOfferSidesOnACurve) {
    // Para qué sirve la receta, medido: sobre la misma arandela, sin receta el
    // proponedor ofrece lo que sabe —incluidos lados y ángulos si el ajuste
    // poligonal cuela— y con la receta, solo lo que una pieza redonda tiene.
    const Sample ring = aPieceOfFamily(inspection::PieceFamily::Ring);
    if (!ring.ok) {
        GTEST_SKIP() << "sin banco de fotos";
    }
    const auto* washer = inspection::recipeNamed("Arandela");
    ASSERT_NE(washer, nullptr);

    const auto result = inspection::proposeWithRecipe(ring.image, ring.mask, ring.fixture,
                                                       *washer, 0.0);
    ASSERT_TRUE(result.applies) << result.why;
    std::printf("  [receta] arandela -> %d cotas:", static_cast<int>(result.proposals.size()));
    for (const auto& proposal : result.proposals) {
        std::printf(" %s", proposal.config.name.c_str());
    }
    std::printf("\n");

    EXPECT_GT(result.proposals.size(), 1U)
        << "la receta de la arandela no saca ninguna cota de una arandela";
    EXPECT_EQ(howManyNamed(result.proposals, "Lado"), 0)
        << "se propone el lado de una pieza redonda";
    EXPECT_EQ(howManyNamed(result.proposals, "Ángulo"), 0)
        << "se propone el ángulo de una esquina que no existe";
    // Y sí trae lo suyo: el diámetro exterior es la cota de la que cuelga todo
    // lo demás, y sin ella la receta no serviría de nada.
    EXPECT_GT(howManyNamed(result.proposals, "Ø"), 0)
        << "la receta de la arandela no propone ningún diámetro";
}

TEST(MeasureRecipes, TheGearRecipeIsJudgedByTheToolAndNotByTheClass) {
    // EL CASO QUE NO ENCAJA EN EL MOLDE, y por eso está escrito aparte.
    //
    // Para el clasificador una rueda dentada es «irregular»: en
    // `engranaje-1.png` le cuenta 115 lados rectos. O sea que si la receta de
    // engranaje exigiera una familia reconocida, no se podría aplicar NUNCA.
    // Quien dice si es un engranaje es la herramienta consiguiendo medirlo.
    const Sample gear = biggestPieceOf("engranaje-1.png");
    if (!gear.ok) {
        GTEST_SKIP() << "sin banco de fotos";
    }
    const auto* recipe = inspection::recipeNamed("Engranaje");
    ASSERT_NE(recipe, nullptr);
    ASSERT_EQ(inspection::familyOf(gear.shape), inspection::PieceFamily::Any)
        << "la rueda dentada ya se reconoce como una familia propia. Entonces esta receta "
           "puede pedir su familia como las demás, y este caso especial sobra";

    const auto result = inspection::proposeWithRecipe(gear.image, gear.mask, gear.fixture,
                                                       *recipe, 0.0);
    std::printf("  [receta] engranaje -> aplica=%d, %d cotas:",
                static_cast<int>(result.applies),
                static_cast<int>(result.proposals.size()));
    for (const auto& proposal : result.proposals) {
        std::printf(" %s", proposal.config.name.c_str());
    }
    std::printf(" | %s\n", result.why.c_str());
    EXPECT_TRUE(result.applies)
        << "la receta de engranaje no se puede aplicar a un engranaje, porque el "
           "clasificador lo llama irregular: entonces la receta es inútil";
}

TEST(MeasureRecipes, WithoutARecipeNothingChanges) {
    // La tercera condición, y la que impide que esto se convierta en ajustes
    // escondidos: la receta «Todas las cotas» tiene que dar EXACTAMENTE lo mismo
    // que no elegir ninguna. Si un día no coincide, es que alguien metió un
    // ajuste en la receta por defecto y cambió lo que recibe todo el mundo.
    const Sample any = aPieceOfFamily(inspection::PieceFamily::Ring);
    if (!any.ok) {
        GTEST_SKIP() << "sin banco de fotos";
    }
    const auto* everything = inspection::recipeNamed("Todas las cotas");
    ASSERT_NE(everything, nullptr);

    const auto withRecipe = inspection::proposeWithRecipe(any.image, any.mask, any.fixture,
                                                          *everything, 0.0);
    const auto without =
        inspection::proposeTools(any.image, any.mask, any.fixture, {}, 0.0, nullptr);
    ASSERT_TRUE(withRecipe.applies) << withRecipe.why;
    ASSERT_EQ(withRecipe.proposals.size(), without.size())
        << "la receta «todas» propone " << withRecipe.proposals.size()
        << " cotas y sin receta salen " << without.size();
    for (std::size_t i = 0; i < without.size(); ++i) {
        EXPECT_EQ(withRecipe.proposals[i].config.name, without[i].config.name)
            << "la cota " << i << " no es la misma con receta que sin ella";
    }
}
