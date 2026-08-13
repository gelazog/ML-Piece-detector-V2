#pragma once

#include <opencv2/core.hpp>

namespace pci::vision {

// Zona de trabajo automática: el rectángulo dentro del cual merece la pena
// buscar la pieza en el próximo frame.
//
// Hoy cada frame se segmenta entero aunque la pieza ocupe un 6 % de la imagen.
// El mecanismo para evitarlo **ya existe** —`PipelineConfig::roi` recorta y
// `analyzeFrame` devuelve las coordenadas en el marco completo—; lo que faltaba
// era quién calcula ese rectángulo y lo mueve con la pieza. Eso es esta clase.
//
// La regla que la gobierna: **ante la duda, la imagen entera**. Un recorte que
// se equivoca es peor que no tenerlo, porque la aplicación mediría con
// confianza dentro de un rectángulo donde ya no hay nada. Volver al frame
// completo cuesta un frame; medir en el sitio equivocado no se nota.

// Qué zona de la imagen se procesa en cada frame. Se persiste por nombre
// (`workingZoneModeKey`) para que añadir modos no rompa lo guardado.
enum class WorkingZoneMode {
    Off,        // la imagen entera, siempre
    Automatic,  // el recorte que sigue a la pieza
    Fixed,      // la zona de detección dibujada a mano
};

[[nodiscard]] const char* workingZoneModeKey(WorkingZoneMode mode);
[[nodiscard]] WorkingZoneMode workingZoneModeFromKey(const char* key);

struct AutoRoiOptions {
    // Margen alrededor de la envolvente de la pieza, como fracción de su
    // tamaño. Es lo que permite que la pieza se mueva entre frames sin salirse
    // del recorte antes de que este la siga.
    double marginFraction = 0.35;
    // Cuánto se resiste el recorte a ENCOGER (0 = encoge de golpe, 1 = nunca).
    // Crecer es inmediato; encoger, lento. Así el rectángulo no late al ritmo
    // del ruido de la segmentación, y nunca recorta a la pieza por ir con
    // retraso.
    double shrinkInertia = 0.6;
    // Frames seguidos sin pieza que se toleran antes de rendirse.
    int lostFramesAllowed = 2;
    // Cambio brusco del área de la pieza que se interpreta como "han cambiado
    // la pieza": relación entre el área nueva y la anterior, en cualquier
    // sentido. 2.0 = se rinde si dobla o si se reduce a la mitad.
    double areaJumpRatio = 2.0;
    // Por debajo de este tamaño no se recorta: el ahorro no compensa el riesgo.
    int minRoiSizePx = 48;
};

// Por qué el seguimiento volvió al frame completo. Se expone para poder
// decírselo al operador: un recorte que desaparece sin explicación parece un
// fallo.
enum class AutoRoiGiveUp {
    None,
    PieceLost,      // no se encontró pieza durante varios frames
    PieceEscaping,  // la pieza tocó el borde del recorte
    AreaJumped,     // el área cambió de golpe: probablemente es otra pieza
};

class AutoRoiTracker {
public:
    explicit AutoRoiTracker(AutoRoiOptions options = {}) : options_(options) {}

    // Rectángulo con el que analizar el PRÓXIMO frame. Vacío = imagen entera,
    // que es lo que `PipelineConfig::roi` ya entiende como "sin zona".
    [[nodiscard]] cv::Rect roi() const { return roi_; }
    [[nodiscard]] bool tracking() const { return roi_.area() > 0; }
    [[nodiscard]] AutoRoiGiveUp lastGiveUp() const { return giveUp_; }

    // Alimenta el resultado del análisis del frame actual. `pieceBounds` va en
    // coordenadas de la IMAGEN COMPLETA (que es lo que devuelve `analyzeFrame`
    // aunque se le haya pasado un recorte).
    void update(bool pieceFound, const cv::Rect& pieceBounds, const cv::Size& frameSize);

    // Vuelve al frame completo y olvida el historial. Lo llama quien cambia de
    // pieza, de cámara o de resolución.
    void reset();

private:
    AutoRoiOptions options_;
    cv::Rect roi_;
    double lastArea_ = 0.0;
    int lostFrames_ = 0;
    AutoRoiGiveUp giveUp_ = AutoRoiGiveUp::None;
};

// Texto en español del motivo, para el panel de estado. Cadena vacía si no hubo
// motivo (`None`).
[[nodiscard]] const char* giveUpReason(AutoRoiGiveUp reason);

// La zona con la que se analiza este frame, según el modo. Vacía = imagen
// entera, que es lo que `PipelineConfig::roi` ya entiende como «sin zona».
//
// Vive aquí y no en la ventana porque es la regla, no una pantalla: quién usa
// el rectángulo dibujado y quién el que sigue a la pieza.
//
// `countingPieces` es que alguien va a leer CUÁNTAS piezas se ven. Con eso
// puesto, el modo automático se apaga, y no es un caso particular: el recorte
// automático rodea a UNA pieza —la mayor— con su margen, así que contar dentro
// de él da 1 por construcción, con seis piezas en la mesa. La zona FIJA sí
// sigue recortando el recuento, porque ahí el operador dijo «mira solo aquí» y
// esa es su respuesta; la automática es una optimización, y una optimización
// que cambia la respuesta no es una optimización, es un fallo.
[[nodiscard]] cv::Rect effectiveWorkingZone(WorkingZoneMode mode, const cv::Rect& fixedZone,
                                            const cv::Rect& automaticZone,
                                            bool countingPieces = false);

// Modo que corresponde después de dibujar o de quitar la zona fija.
//
// Existe porque separarlos costó un fallo real: la zona dibujada se guardaba
// pero el modo seguía en «imagen entera», así que el botón decía «Quitar zona»,
// la barra de estado decía que estaba activa, y el rectángulo ni se pintaba ni
// se usaba. **Dibujar una zona es usarla** —nadie arrastra un recuadro de
// detección para luego no usarlo— y quitarla apaga el modo que la usaba, que si
// no quedaría apuntando a un rectángulo que ya no existe.
[[nodiscard]] WorkingZoneMode modeAfterFixedZoneChanged(WorkingZoneMode current,
                                                        bool hasFixedZone);

}  // namespace pci::vision
