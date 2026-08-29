// UNA MEDIDA POR PIEZA QUE PASA, Y NO DOCE.
//
// Petición de uso: «la manera en la que lo toma en vídeo (grabado o en tiempo
// real), para que el usuario pueda definir un tiempo de espera entre que sale y
// entra una/varias piezas en el enfoque».
//
// Lo que había es un temporizador fijo que mide el fotograma que haya cada N ms,
// y sobre una cinta eso falla de tres formas, todas en silencio: mide piezas a
// medio entrar, mide la misma pieza doce veces mientras cruza, y una pieza
// rápida puede pasar entre dos disparos sin medirse nunca.
//
// Estas pruebas recorren un paso de pieza completo —entra, se para, sale— con
// instantes dados a mano, así que comprueban el comportamiento entero sin
// cámara, sin vídeo y sin esperar un solo segundo de reloj. Ese es el motivo de
// que la decisión viva aparte de la ventana.

#include <gtest/gtest.h>

#include <cstdio>
#include <vector>

#include "vision/pass_trigger.h"

using namespace pci;

namespace {

vision::SceneSnapshot at(std::int64_t ms, std::vector<cv::Point2f> centres,
                         bool touchingEdge = false) {
    vision::SceneSnapshot snapshot;
    snapshot.atMs = ms;
    snapshot.centres = std::move(centres);
    snapshot.someoneTouchesTheEdge = touchingEdge;
    return snapshot;
}

// Un paso de pieza como sale de una cinta: asoma por el borde, entra entera,
// se para, y se va. Devuelve cuántas veces se habría medido.
int measurementsDuringAPass(vision::PassTrigger& trigger, int stillFrames) {
    int measured = 0;
    std::int64_t clock = 0;
    const auto step = [&](const vision::SceneSnapshot& snapshot) {
        if (trigger.observe(snapshot).decision == vision::PassDecision::Measure) {
            ++measured;
        }
    };
    // Asomando por el borde, moviéndose.
    for (int i = 0; i < 4; ++i) {
        step(at(clock += 100, {{static_cast<float>(10 + 20 * i), 100.0F}}, true));
    }
    // Ya entera, todavía avanzando.
    for (int i = 0; i < 3; ++i) {
        step(at(clock += 100, {{static_cast<float>(120 + 30 * i), 100.0F}}));
    }
    // Parada en el sitio.
    for (int i = 0; i < stillFrames; ++i) {
        step(at(clock += 100, {{210.0F, 100.0F}}));
    }
    // Se va, y el encuadre se queda vacío un rato.
    for (int i = 0; i < 3; ++i) {
        step(at(clock += 100, {{static_cast<float>(240 + 40 * i), 100.0F}}, true));
    }
    for (int i = 0; i < 6; ++i) {
        step(at(clock += 100, {}));
    }
    return measured;
}

}  // namespace

TEST(PassTrigger, APieceThatCrossesIsMeasuredExactlyOnce) {
    vision::PassTriggerOptions options;
    options.settleMs = 400;
    options.rearmMs = 300;
    vision::PassTrigger trigger(options);

    const int first = measurementsDuringAPass(trigger, 8);
    std::printf("  [paso] primera pieza: %d medida(s)\n", first);
    EXPECT_EQ(first, 1)
        << "una pieza que cruza se mide " << first
        << " veces: cada una cuenta como una inspección distinta en el histórico, y todas "
           "son de la misma pieza";

    // Y la SIGUIENTE también, que es la otra mitad: un disparador que se queda
    // desarmado mide la primera pieza del turno y ninguna más.
    const int second = measurementsDuringAPass(trigger, 8);
    std::printf("  [paso] segunda pieza: %d medida(s)\n", second);
    EXPECT_EQ(second, 1)
        << "la segunda pieza no se mide: el disparo no se rearmó al vaciarse el encuadre, "
           "así que el turno entero se inspecciona con la primera";
}

TEST(PassTrigger, APieceTouchingTheEdgeIsNeverMeasured) {
    // LO PRIMERO QUE HAY QUE IMPEDIR. Media pieza da cotas cortas y un NG que no
    // es de la pieza sino del momento en que se miró.
    //
    // Y se comprueba con la pieza QUIETA en el borde —la cinta parada, la pieza
    // asomando—, porque ése es el caso que se cuela: el asentamiento se cumple y
    // sin esta regla se mediría.
    vision::PassTriggerOptions options;
    options.settleMs = 200;
    vision::PassTrigger trigger(options);

    std::int64_t clock = 0;
    for (int i = 0; i < 20; ++i) {
        const auto verdict = trigger.observe(at(clock += 100, {{20.0F, 100.0F}}, true));
        ASSERT_EQ(verdict.decision, vision::PassDecision::Wait)
            << "se mide una pieza que toca el borde, y encima llevaba " << verdict.stillMs
            << " ms parada ahí: sus cotas son un límite inferior";
    }
    // En cuanto entra entera y se queda quieta, sí.
    bool measured = false;
    for (int i = 0; i < 6; ++i) {
        measured = measured || trigger.observe(at(clock += 100, {{200.0F, 100.0F}}))
                                       .decision == vision::PassDecision::Measure;
    }
    EXPECT_TRUE(measured) << "la pieza ya entra entera y quieta y sigue sin medirse";
}

TEST(PassTrigger, TheSettleTimeIsRespectedAndItIsNotJustOneFrame) {
    // El asentamiento con su número: con 500 ms pedidos, medir a los 200 sería
    // medir el arrastre. Se comprueba el instante EXACTO en el que dispara.
    vision::PassTriggerOptions options;
    options.settleMs = 500;
    vision::PassTrigger trigger(options);

    std::int64_t clock = 1000;
    (void)trigger.observe(at(clock, {{300.0F, 200.0F}}));  // primera vez: empieza a contar
    int firedAt = -1;
    for (int i = 1; i <= 10 && firedAt < 0; ++i) {
        const auto verdict = trigger.observe(at(clock + 100 * i, {{300.0F, 200.0F}}));
        if (verdict.decision == vision::PassDecision::Measure) {
            firedAt = 100 * i;
        }
    }
    std::printf("  [paso] con 500 ms pedidos, dispara a los %d ms quieta\n", firedAt);
    EXPECT_EQ(firedAt, 500) << "dispara a los " << firedAt
                            << " ms de estar quieta y se le pidieron 500";
}

TEST(PassTrigger, ANudgeRestartsTheCountInsteadOfAddingUp) {
    // Lo que separa «quieta 500 ms» de «500 ms mirando»: si la pieza se mueve a
    // mitad de la cuenta, la cuenta vuelve a empezar. Sumando trozos, una cinta
    // que avanza a tirones dispararía entre dos tirones.
    vision::PassTriggerOptions options;
    options.settleMs = 400;
    options.stillnessPx = 2.0;
    vision::PassTrigger trigger(options);

    std::int64_t clock = 0;
    (void)trigger.observe(at(clock += 100, {{100.0F, 100.0F}}));
    (void)trigger.observe(at(clock += 100, {{100.0F, 100.0F}}));
    (void)trigger.observe(at(clock += 100, {{100.0F, 100.0F}}));  // 200 ms quieta
    // Un tirón de 20 px: eso no es ruido de segmentación.
    auto verdict = trigger.observe(at(clock += 100, {{120.0F, 100.0F}}));
    EXPECT_EQ(verdict.stillMs, 0) << "la cuenta de quietud no se reinició con el tirón";
    verdict = trigger.observe(at(clock += 100, {{120.0F, 100.0F}}));
    verdict = trigger.observe(at(clock += 100, {{120.0F, 100.0F}}));
    EXPECT_EQ(verdict.decision, vision::PassDecision::Wait)
        << "ha disparado a los 300 ms de la parada nueva sumando los 200 de la anterior: "
           "una cinta a tirones se mediría en marcha";

    // Y un temblor de un píxel NO reinicia: eso es el ruido del contorno, y con
    // tolerancia cero no dispararía nunca sobre una imagen real.
    verdict = trigger.observe(at(clock += 100, {{120.8F, 100.4F}}));
    EXPECT_GT(verdict.stillMs, 0)
        << "un temblor de menos de un píxel reinicia la cuenta: sobre vídeo real el "
           "contorno baila así siempre, y no se mediría nunca";
}

TEST(PassTrigger, ItDoesNotRearmUntilTheFrameHasBeenEmptyLongEnough) {
    // El tiempo de rearme, que es el que el operador pidió poder definir: entre
    // que sale una y entra la siguiente. Con 400 ms pedidos, un hueco de 200 —el
    // que deja una pieza que rebota, o dos piezas pegadas— no puede rearmar.
    vision::PassTriggerOptions options;
    options.settleMs = 200;
    options.rearmMs = 400;
    vision::PassTrigger trigger(options);

    std::int64_t clock = 0;
    // Primera pieza: entra, se para, se mide.
    for (int i = 0; i < 4; ++i) {
        (void)trigger.observe(at(clock += 100, {{200.0F, 100.0F}}));
    }
    ASSERT_FALSE(trigger.armed()) << "la primera pieza no llegó a medirse";

    // El encuadre se vacía. El hueco se cuenta desde ESTE instante, que es
    // cuando dejó de haber piezas.
    const std::int64_t emptyFrom = clock += 100;
    (void)trigger.observe(at(emptyFrom, {}));

    (void)trigger.observe(at(emptyFrom + 200, {}));
    EXPECT_FALSE(trigger.armed())
        << "se rearma con 200 ms de hueco y se le pidieron 400: dos piezas pegadas se "
           "medirían como dos pasos, o la misma pieza se mediría dos veces";
    (void)trigger.observe(at(emptyFrom + 399, {}));
    EXPECT_FALSE(trigger.armed()) << "se rearma un milisegundo antes de lo pedido";

    (void)trigger.observe(at(emptyFrom + 400, {}));
    EXPECT_TRUE(trigger.armed()) << "no se rearma ni con 400 ms de encuadre vacío";
}

TEST(PassTrigger, ItSaysWhyItIsNotMeasuring) {
    // Un disparador que no dispara y no dice por qué se vive como «la
    // auto-inspección no funciona». Casi siempre la causa es una de dos, y las
    // dos llevan a hacer algo distinto: parar la cinta un poco más, o recolocar
    // la cámara para que la pieza entre entera.
    vision::PassTrigger trigger;
    const auto moving = trigger.observe(at(100, {{50.0F, 50.0F}}));
    const auto edge = trigger.observe(at(200, {{50.0F, 50.0F}}, true));
    const auto empty = trigger.observe(at(300, {}));
    std::printf("  [paso] «%s» / «%s» / «%s»\n", moving.why.c_str(), edge.why.c_str(),
                empty.why.c_str());
    EXPECT_FALSE(moving.why.empty());
    EXPECT_NE(edge.why.find("borde"), std::string::npos)
        << "no se dice que lo que estorba es una pieza en el borde: " << edge.why;
    EXPECT_FALSE(empty.why.empty());
}

TEST(PassTrigger, ChangingVideoStartsFromScratch) {
    // Los milisegundos de otro vídeo no dicen nada de éste. Sin reiniciar, la
    // primera pieza del vídeo nuevo se compararía con la última del anterior y
    // podría medirse en marcha —o no medirse nunca, si el disparo quedó
    // desarmado al cerrar el anterior con una pieza dentro—.
    vision::PassTriggerOptions options;
    options.settleMs = 200;
    vision::PassTrigger trigger(options);
    std::int64_t clock = 0;
    for (int i = 0; i < 4; ++i) {
        (void)trigger.observe(at(clock += 100, {{200.0F, 100.0F}}));
    }
    ASSERT_FALSE(trigger.armed());

    trigger.reset();
    EXPECT_TRUE(trigger.armed())
        << "al cambiar de vídeo el disparo sigue desarmado: la primera pieza del vídeo "
           "nuevo no se mediría hasta que el encuadre se vaciara";
}
