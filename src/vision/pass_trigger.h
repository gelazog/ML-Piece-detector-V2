#pragma once

#include <opencv2/core.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace pci::vision {

// CUÁNDO MEDIR EN UN VÍDEO: UNA VEZ POR PIEZA QUE PASA.
//
// Petición de uso: «la manera en la que lo toma en vídeo (ya sea grabado o en
// tiempo real), para que el usuario pueda definir un tiempo de espera entre que
// sale y entra una/varias piezas en el enfoque».
//
// Lo que había: la auto-inspección es un TEMPORIZADOR FIJO que mide el
// fotograma que haya cada N ms. Sobre una cinta eso hace tres cosas mal, y las
// tres en silencio:
//
//   - mide piezas a medio entrar —media pieza da cotas cortas y un NG que no
//     es de la pieza, sino del momento en que se miró—;
//   - mide la misma pieza doce veces mientras cruza, y cada medida cuenta como
//     una inspección distinta en el histórico;
//   - y una pieza que pasa rápida entre dos disparos no se mide NUNCA.
//
// Así que el disparo deja de ser el reloj y pasa a ser la ESCENA. Tres
// condiciones, y cada una responde a una de las tres:
//
//   1. **Nada a medio entrar**: si alguna pieza toca el borde del encuadre, no
//      se mide. Una pieza cortada da cotas que son un límite inferior, y eso ya
//      lo sabe decir el informe.
//   2. **Asentamiento**: la escena tiene que estar QUIETA —mismo recuento y los
//      centroides sin moverse— durante el tiempo que fije el operador. Sin esto
//      se mide el arrastre.
//   3. **Rearme al vaciarse**: después de medir no se vuelve a medir hasta que
//      el encuadre se quede VACÍO durante el tiempo que fije el operador. Es lo
//      que convierte «doce medidas mientras cruza» en «una por pieza».
//
// La tercera es una decisión del dueño del proyecto entre tres opciones, y la
// que menos se equivoca: sobre una cinta con piezas sueltas, «el encuadre se
// vació» es un hecho, mientras que «ha cambiado bastante lo que veo» es una
// estimación que falla justo cuando dos piezas se parecen.
//
// Esto NO mide ni segmenta: recibe lo que ya se analizó y responde sí o no. Vive
// aparte de la ventana para poder probarlo sin cámara, sin vídeo y sin esperar
// segundos de reloj — se le dan instantes y él responde.

struct SceneSnapshot {
    // Instante de esta observación, en milisegundos de un reloj monótono. Se
    // pasa desde fuera —no se lee el reloj aquí dentro— para poder probar un
    // paso de pieza entero en microsegundos.
    std::int64_t atMs = 0;
    // Los centroides de las piezas que se ven, en píxeles de imagen. El recuento
    // sale de aquí: `size()`.
    std::vector<cv::Point2f> centres;
    // Si alguna toca el borde del encuadre. Se pasa hecho porque quien analiza
    // ya lo sabe —el informe de pieza avisa de ello— y recalcularlo aquí sería
    // una segunda respuesta a la misma pregunta.
    bool someoneTouchesTheEdge = false;
};

struct PassTriggerOptions {
    // Cuánto tiene que estar quieta la escena antes de medir.
    int settleMs = 400;
    // Cuánto tiene que estar vacío el encuadre para volver a armar el disparo.
    int rearmMs = 300;
    // Cuánto puede moverse un centroide y seguir considerándose quieto. En
    // píxeles: por debajo de esto es el ruido de la segmentación, no la pieza
    // avanzando.
    double stillnessPx = 2.0;
};

// Qué hacer con el fotograma que se acaba de observar.
enum class PassDecision {
    Wait,     // todavía no
    Measure,  // ahora: la escena está quieta y el disparo estaba armado
};

struct PassVerdict {
    PassDecision decision = PassDecision::Wait;
    // Por qué se espera, en una frase, para poder enseñarlo. Un disparador que
    // no dispara y no dice por qué se vive como «la auto-inspección no
    // funciona», y lo que pasa casi siempre es que la cinta no para o que la
    // pieza asoma por el borde.
    std::string why;
    // Cuánto lleva quieta la escena, para poder enseñar la cuenta atrás.
    int stillMs = 0;
};

// El disparador. Se le llama con cada análisis y responde qué hacer.
//
// Guarda estado —lo quieta que está la escena y si ya se midió esta pieza—, así
// que hay uno por fuente de vídeo y se reinicia al cambiar de fuente: los
// milisegundos de otro vídeo no dicen nada de éste.
class PassTrigger {
public:
    explicit PassTrigger(PassTriggerOptions options = {}) : options_(options) {}

    void setOptions(const PassTriggerOptions& options) { options_ = options; }
    [[nodiscard]] const PassTriggerOptions& options() const { return options_; }

    // Vuelve al estado de recién abierto: sin nada visto y con el disparo
    // armado. Se llama al cambiar de vídeo o de cámara.
    void reset();

    [[nodiscard]] PassVerdict observe(const SceneSnapshot& snapshot);

    // Si el disparo está armado, o sea si la próxima pieza quieta se medirá.
    // Falso justo después de medir, hasta que el encuadre se vacíe.
    [[nodiscard]] bool armed() const { return armed_; }

private:
    PassTriggerOptions options_;
    bool armed_ = true;
    bool hadPrevious_ = false;
    std::vector<cv::Point2f> previous_;
    std::int64_t stillSince_ = 0;   // desde cuándo no se mueve
    std::int64_t emptySince_ = 0;   // desde cuándo no hay nada
    bool isEmpty_ = false;
};

}  // namespace pci::vision
