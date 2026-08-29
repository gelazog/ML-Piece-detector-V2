// LA REFERENCIA QUE FALTA, DICHA AL DIBUJAR Y NO AL MEDIR.
//
// De las 32 herramientas, cinco no miden nada sin una referencia declarada:
// Orientación y Desviación de centros necesitan un datum, el Punto y la Recta
// construidos necesitan de qué construirse, y el Patrón de agujeros necesita
// los agujeros. Todas lo dicen — el barrido `NoSilentTool` lo comprueba— pero
// lo dicen **cuando ya se ha medido**.
//
// Ese momento es tarde: el operador dibuja la herramienta, la ve dibujada,
// sigue trabajando, y se entera de que no mide cuando llega el veredicto.
//
// Lo que estas pruebas fijan es la regla de cuándo se resuelve sola, que es
// donde está el riesgo. **Una candidata: se pone. Varias: se pregunta. Ninguna:
// se dice qué falta.** El desempate inventado —«la primera», «la más cercana»—
// es la peor de las tres salidas: da una referencia plausible y equivocada, y
// entonces la herramienta mide contra un datum que nadie eligió y el número
// parece correcto.
//
// Y las candidatas salen de lo que las otras herramientas PRODUJERON al medir,
// no de una tabla de «qué ofrece cada tipo». Esa tabla acabaría discrepando del
// ejecutor, que es el fallo que este proyecto lleva arreglando desde el primer
// día: dos partes respondiendo a la misma pregunta por caminos distintos.

#include <gtest/gtest.h>

#include <cstdio>
#include <string>
#include <vector>

#include "inspection_editor/reference_advice.h"

using namespace pci;
using namespace pci::inspection;

namespace {

ToolRunResult gives(const char* name, DerivedKind kind) {
    ToolRunResult result;
    result.name = name;
    result.ok = true;
    result.derived.kind = kind;
    result.derived.point = {10.0F, 20.0F};
    result.derived.direction = {1.0F, 0.0F};
    result.derived.radius = 15.0;
    return result;
}

ToolConfig named(const char* name) {
    ToolConfig config;
    config.name = name;
    return config;
}

// Una Orientación: mide el giro de la pieza contra una RECTA declarada.
ToolGeometry anOrientation() {
    return OrientationGeometry{{5.0F, 5.0F}, {60.0F, 8.0F}, 16.0F, 60, 0.0F};
}

}  // namespace

TEST(ReferenceAdvice, WithASingleCandidateItResolvesItselfAndSaysWhichOne) {
    // El caso que el usuario pidió: «que se resuelva sola cuando hay una
    // evidente». Con una sola recta medida no hay nada que elegir.
    const std::vector<ToolRunResult> measured{gives("Cara de apoyo", DerivedKind::Line),
                                              gives("Ø agujero 1", DerivedKind::Circle)};
    const auto advice =
        adviseReference(named("Orientación 1"), anOrientation(), measured);
    std::printf("  [datum] %s\n", advice.why.c_str());

    ASSERT_TRUE(advice.needed);
    EXPECT_EQ(advice.first, "Cara de apoyo")
        << "con una sola recta disponible sigue sin ponerse: el operador tiene que ir al "
           "panel a elegir lo único que hay";
    // Y se DICE cuál se ha puesto: una referencia que aparece sola y en silencio
    // es peor que no ponerla, porque nadie sabe contra qué se está midiendo.
    EXPECT_NE(advice.why.find("Cara de apoyo"), std::string::npos) << advice.why;
}

TEST(ReferenceAdvice, WithTwoCandidatesItAsksInsteadOfGuessing) {
    // LA REGLA QUE PROTEGE EL NÚMERO. Con dos rectas, elegir por el operador
    // daría una medida contra un datum que él no eligió — y que parecería
    // correcta, que es lo que la hace peligrosa.
    const std::vector<ToolRunResult> measured{gives("Cara de apoyo", DerivedKind::Line),
                                              gives("Cara lateral", DerivedKind::Line)};
    const auto advice =
        adviseReference(named("Orientación 1"), anOrientation(), measured);
    std::printf("  [datum] %s\n", advice.why.c_str());

    EXPECT_TRUE(advice.first.empty())
        << "elige una de las dos por su cuenta: la herramienta mediría contra un datum que "
           "nadie eligió y el número parecería correcto";
    EXPECT_EQ(advice.candidatesFirst, 2);
    // Y el aviso nombra LAS DOS, para poder elegir sin ir a buscarlas.
    EXPECT_NE(advice.why.find("Cara de apoyo"), std::string::npos) << advice.why;
    EXPECT_NE(advice.why.find("Cara lateral"), std::string::npos) << advice.why;
}

TEST(ReferenceAdvice, WithNoCandidateItSaysWhatIsMissing) {
    // La tercera salida, y la que responde a «esta herramienta no muestra
    // medida»: no hay nada que poner, y hay que decir QUÉ hace falta dibujar.
    const std::vector<ToolRunResult> measured{gives("Ø agujero 1", DerivedKind::Circle)};
    const auto advice =
        adviseReference(named("Orientación 1"), anOrientation(), measured);
    std::printf("  [datum] %s\n", advice.why.c_str());

    EXPECT_TRUE(advice.first.empty());
    EXPECT_EQ(advice.candidatesFirst, 0);
    EXPECT_FALSE(advice.why.empty())
        << "no hay referencia posible y no se dice nada: el operador dibuja la herramienta "
           "y se entera de que no mide cuando llega el veredicto";
    EXPECT_NE(advice.why.find("recta"), std::string::npos)
        << "el aviso no dice QUÉ hace falta: " << advice.why;
}

TEST(ReferenceAdvice, AToolWithoutReferencesIsLeftAlone) {
    // Las 27 restantes no llevan referencia, y tocarles `reference` sería
    // inventarles una dependencia que no tienen — y que el ejecutor ignoraría,
    // dejando un campo escrito que no significa nada.
    const std::vector<ToolRunResult> measured{gives("Cara de apoyo", DerivedKind::Line)};
    const auto advice = adviseReference(named("Regla 1"),
                                        RulerGeometry{{0.0F, 0.0F}, {50.0F, 0.0F}}, measured);
    EXPECT_FALSE(advice.needed);
    EXPECT_TRUE(advice.first.empty());
    EXPECT_TRUE(advice.why.empty());
}

TEST(ReferenceAdvice, ItNeverReferencesItselfNorSomethingThatDidNotMeasure) {
    // Dos formas de dar una referencia que rompe la cadena en silencio:
    //
    //   - a sí misma, que es una dependencia circular con aspecto de datum;
    //   - una herramienta que en este frame NO produjo nada: proponerla dejaría
    //     la cadena rota igual, pero con la culpa repartida entre dos.
    ToolRunResult itself = gives("Orientación 1", DerivedKind::Line);
    ToolRunResult failed;
    failed.name = "Cara de apoyo";
    failed.ok = false;
    failed.detail = "No se detectó ningún borde en el escaneo";

    const auto advice =
        adviseReference(named("Orientación 1"), anOrientation(), {itself, failed});
    std::printf("  [datum] %s\n", advice.why.c_str());
    EXPECT_TRUE(advice.first.empty())
        << "se ha referenciado a sí misma o a una herramienta que no midió";
    EXPECT_EQ(advice.candidatesFirst, 0);
}

TEST(ReferenceAdvice, TheSecondOperandIsNotTheSameToolAsTheFirst) {
    // Un punto medio «entre A y A» es A. Con una sola candidata para los dos
    // operandos, poner la misma en los dos daría una construcción que se mide
    // sin fallar y no significa nada — la peor clase de resultado.
    const std::vector<ToolRunResult> measured{gives("Ø agujero 1", DerivedKind::Circle)};
    const auto advice =
        adviseReference(named("Punto medio"),
                        ConstructedPointGeometry{PointConstruction::Midpoint, {0.0F, 0.0F}},
                        measured);
    std::printf("  [datum] %s\n", advice.why.c_str());
    ASSERT_TRUE(advice.needed);
    EXPECT_EQ(advice.first, "Ø agujero 1");
    EXPECT_TRUE(advice.second.empty())
        << "la segunda referencia es la misma herramienta que la primera: un punto medio "
           "entre A y A es A";
    EXPECT_NE(advice.why.find("segunda"), std::string::npos)
        << "no se dice que falta la segunda: " << advice.why;
}
