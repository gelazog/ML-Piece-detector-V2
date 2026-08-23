#pragma once

#include <opencv2/core.hpp>

#include <optional>
#include <string>
#include <vector>

#include "core/result.h"

namespace pci::vision {

// CORRECCIÓN DE LA DISTORSIÓN DE LA LENTE.
//
// Toda lente curva las rectas. En una lente barata o angular, una regla apoyada
// en el borde del encuadre sale abombada, y una pieza medida ahí sale más
// pequeña de lo que es.
//
// CUÁNTO, medido antes de construir esto (`tests/test_lens_distortion.cpp`).
// Con una lente de gama de consumo —k1 = -0,25, barril moderado, del orden de lo
// que trae una webcam corriente— y un disco del mismo tamaño real puesto en
// distintos sitios del encuadre:
//
//     a   0 px del centro:  178,54 px  (-0,30 %)
//     a 358 px del centro:  165,60 px  (-7,53 %)
//     a 613 px del centro:  145,87 px  (-18,54 %)
//
// La misma pieza mide un 18,5 % menos en una esquina que en el centro. El
// programa no lo corregía, así que daba una medida en el centro de la mesa y
// otra distinta en la esquina, sin que nada en pantalla dijera cuál era la
// buena.
//
// Esto NO es lo mismo que la escala en mm ni que el marcador ArUco, y conviene
// tenerlo claro porque los tres se llaman «calibrar»:
//
//   - La escala (`domain/calibration.h`) dice CUÁNTOS milímetros mide un píxel.
//     Es un número, y es el mismo en todo el encuadre.
//   - El marcador ArUco (`vision/plane_scale.h`) corrige la PERSPECTIVA: que la
//     cámara no esté perpendicular a la mesa. Es una homografía, y sigue siendo
//     una transformación de rectas en rectas.
//   - Esto corrige la LENTE, que no lleva rectas a rectas. Ninguna homografía
//     puede deshacerlo, así que el marcador no lo arregla por mucho que se
//     afine. Hace falta un modelo aparte y una toma aparte.
//
// El orden importa: primero se endereza la lente y después se mide. Medir sobre
// una imagen distorsionada y luego «corregir el número» no funciona, porque el
// factor depende de dónde estaba la pieza.

// El tablero de ajedrez impreso que se le pone delante a la cámara.
//
// Se cuentan las esquinas INTERIORES, no los cuadros: un tablero de 10x7
// cuadros tiene 9x6 esquinas interiores. Es la convención de OpenCV y la fuente
// del error más común al calibrar, así que los nombres lo dicen.
struct BoardSpec {
    int innerCols = 9;
    int innerRows = 6;
    double squareMm = 20.0;

    [[nodiscard]] bool isValid() const;
    [[nodiscard]] int cornerCount() const { return innerCols * innerRows; }
};

// Un tablero encontrado en una imagen. Guarda solo las esquinas: la imagen
// entera no hace falta para calibrar y ocupa mil veces más.
struct BoardView {
    std::vector<cv::Point2f> corners;
    cv::Size imageSize;
    // Fracción del encuadre que ocupa la envolvente del tablero (0..1).
    //
    // ES INFORMATIVA, y esto está dicho porque antes prometía otra cosa: decía
    // que servía «para exigir variedad», y no la usaba nadie. La variedad la
    // comprueba `coverageOf`, que mira DÓNDE cae cada toma y no cuánto ocupa.
    //
    // Al ir a darle ese papel se midió si el tamaño importaba, y la respuesta
    // fue que ya está cubierto: con el tablero lo bastante lejos como para que
    // la toma menor ocupe el 0,4 % del encuadre, el error de reproyección sube a
    // 3,85 px y `calibrateLens` se niega por su cuenta. Una regla de tamaño
    // aparte sería un segundo guardián para lo mismo, y con un umbral inventado
    // en vez de medido. Se queda el dato, sin la promesa.
    double coverage = 0.0;
    // Centro de la envolvente, en fracción del encuadre (0..1 en cada eje).
    cv::Point2f center;
};

// Busca el tablero en una imagen. nullopt si no aparece entero.
//
// Se exige entero a propósito: media rejilla da esquinas válidas pero ancla el
// modelo a una zona del encuadre, y la distorsión es justo lo que cambia de una
// zona a otra.
std::optional<BoardView> findBoard(const cv::Mat& image, const BoardSpec& spec);

// El modelo de la lente. `cameraMatrix` es la K de 3x3 y `distortion` los cinco
// coeficientes (k1, k2, p1, p2, k3) de OpenCV.
struct LensCalibration {
    cv::Mat cameraMatrix;
    cv::Mat distortion;
    cv::Size imageSize;
    // Error de reproyección RMS, en píxeles. Es la única cifra honesta de
    // calidad que sale de calibrar, y por eso viaja con el modelo en vez de
    // quedarse en un log.
    double reprojectionError = 0.0;
    int views = 0;

    [[nodiscard]] bool isValid() const;
};

// Menos vistas que esto y el ajuste tiene más parámetros que datos fiables.
// OpenCV acepta tres; con tres sale un modelo que reproyecta precioso sobre sus
// propias tres tomas y falla en cualquier otra.
inline constexpr int kMinimumViews = 8;

// Por encima de este error el modelo no se debe usar: casi siempre significa
// que alguna toma tenía el tablero movido o mal detectado, y un modelo malo
// deforma las medidas en vez de arreglarlas.
inline constexpr double kMaxUsableReprojectionError = 1.0;

// Por debajo de este desplazamiento, corregir no cambia ninguna medida que el
// operador pueda ver, y encender la corrección solo añade un remap por frame y
// una cosa más que puede estar mal configurada.
//
// Un píxel es el umbral porque es la resolución con la que el propio contorno
// sale del umbralizado: por debajo de eso la corrección se pierde dentro del
// ruido de la segmentación.
inline constexpr double kNegligibleDistortionPx = 1.0;

// CUANTO DEL ENCUADRE HAN CUBIERTO LAS TOMAS.
//
// Esto no es una comprobacion de cortesia: es la diferencia entre una
// calibracion que sirve y una que no, y se midio.
//
// Con las tomas rondando el centro, el ajuste sale con un aspecto estupendo
// —distancia focal recuperada 901,8 de 900, error de reproyeccion 0,4 px— y sin
// embargo k1 sale -0,2329 en vez de -0,2500. Con ese 7 % de error, corregir deja
// un residuo del 3,4 % en el borde. Repartiendo las mismas doce tomas por los
// cuatro rincones, k1 sale -0,2494 y el residuo baja al 0,11 %.
//
// El motivo es que la distorsion radial CRECE con el radio: si al ajuste no se
// le enseña nunca el borde, lo extrapola. Y lo peor es que no se queja — el
// error de reproyeccion es igual de bueno en los dos casos, porque mide lo bien
// que el modelo explica las tomas QUE SE LE DIERON.
//
// Por eso hay que mirar la cobertura aparte, y por eso el asistente tiene que
// pedirle al operador que lleve el tablero a las esquinas.
struct BoardCoverage {
    // El encuadre se parte en una rejilla de 3x3 y se marca en que celda cayo el
    // centro de cada toma.
    int cellsTouched = 0;
    int cornersTouched = 0;  // de las cuatro celdas de las esquinas
    bool touched[9] = {};

    [[nodiscard]] bool goodEnough() const;
    // Que falta, en castellano y para enseñarselo a quien esta con el tablero en
    // la mano. Vacio si ya esta bien cubierto.
    [[nodiscard]] std::string advice() const;
};

[[nodiscard]] BoardCoverage coverageOf(const std::vector<BoardView>& views);

core::Result<LensCalibration> calibrateLens(const std::vector<BoardView>& views,
                                            const BoardSpec& spec);

// Cuánto mueve la lente el punto peor del encuadre, en píxeles.
//
// Es LA cifra que decide si esto merece la pena, y la que hay que enseñarle al
// operador: «tu lente desplaza hasta 34 px en las esquinas» se entiende, y
// «k1 = -0,2478» no se entiende.
[[nodiscard]] double worstDisplacementPx(const LensCalibration& calibration);

// Si la lente es lo bastante recta como para que corregirla no cambie nada.
[[nodiscard]] bool distortionIsNegligible(const LensCalibration& calibration);

// Los mapas de remap, construidos una vez y reutilizados en cada frame.
//
// Construirlos cuesta ~30 ms en 1280x960 y aplicarlos ~2 ms, así que hacerlo
// por frame convertiría una mejora de exactitud en una pérdida de cadencia.
class LensCorrector {
public:
    LensCorrector() = default;
    explicit LensCorrector(const LensCalibration& calibration);

    [[nodiscard]] bool isReady() const { return !mapX_.empty(); }
    [[nodiscard]] const LensCalibration& calibration() const { return calibration_; }

    // Devuelve el frame enderezado. Si el corrector no está listo, o si el
    // frame no es del tamaño con el que se calibró, devuelve el frame TAL CUAL.
    //
    // Devolver el original y no fallar es deliberado: un cambio de resolución no
    // debe apagar la cámara. Pero tampoco debe corregir con un modelo que no le
    // corresponde, que es lo que pasaría escalando la K a ojo — y saldrían
    // medidas creíbles y equivocadas, que es el peor de los dos males.
    [[nodiscard]] cv::Mat apply(const cv::Mat& frame) const;

    // Si `apply` va a corregir de verdad este frame o a devolverlo intacto.
    [[nodiscard]] bool appliesTo(const cv::Size& size) const;

private:
    LensCalibration calibration_;
    cv::Mat mapX_;
    cv::Mat mapY_;
};

// --- Persistencia -----------------------------------------------------------
//
// El modelo se guarda como texto plano y no como YAML de OpenCV porque el resto
// de los ajustes del programa ya viven en la tabla de settings, y un fichero
// suelto al lado se pierde al copiar la instalación a otra máquina.

[[nodiscard]] std::string serializeCalibration(const LensCalibration& calibration);
[[nodiscard]] std::optional<LensCalibration> parseCalibration(const std::string& text);

}  // namespace pci::vision
