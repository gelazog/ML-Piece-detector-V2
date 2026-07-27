#pragma once

#include <opencv2/core.hpp>

#include <string_view>

#include "vision/types.h"

namespace pci::vision {

// Tablero de referencia centrado: un sistema de coordenadas con el CERO en un
// origen declarado, para medir *posición* (cuánto se desvía algo del centro) en
// vez de solo distancias entre puntos sueltos. Es el "datum" de la metrología
// dimensional llevado a la pantalla.
//
// Convenios (elegidos para que coincidan con lo que espera un operador y con
// la metrología, NO con la imagen):
//   * +X a la derecha, **+Y hacia ARRIBA** (la y de la imagen crece hacia
//     abajo: aquí se invierte).
//   * Ángulo en grados, 0 = +X, **positivo en sentido antihorario** visual,
//     rango (-180, 180].
// Todo aquí es lógica pura (sin Qt y sin estado): el lienzo solo dibuja lo que
// estas funciones calculan.

enum class BoardOrigin {
    PieceCenter,  // sigue a la pieza (Fixture.origin): desviaciones internas
    ImageCenter,  // fijo en pantalla: cuánto se desvía la pieza del campo de visión
    FixedPoint,   // punto marcado a mano por el operador (coords de imagen)
};

// Elección del operador, persistible con la pieza.
struct BoardConfig {
    BoardOrigin origin = BoardOrigin::PieceCenter;
    // Solo se usa con origin == FixedPoint; en coordenadas de imagen.
    cv::Point2f fixedPoint{0.0F, 0.0F};
    // Si es true los ejes giran con la pieza (mide en el marco de la pieza); si
    // es false quedan alineados con la imagen (mide en el marco de la máquina).
    bool followPieceAngle = false;
};

// Tablero ya resuelto para un frame concreto.
struct BoardFrame {
    cv::Point2f origin{0.0F, 0.0F};
    // Ángulo del eje +X del tablero en coordenadas de imagen (y hacia abajo),
    // mismo convenio que Fixture::angleDeg.
    double angleDeg = 0.0;
};

// Lectura de un punto respecto al tablero. Las unidades son las de entrada
// (px, o mm si ya se aplicó la escala).
struct BoardReading {
    double dx = 0.0;        // + a la derecha
    double dy = 0.0;        // + hacia arriba
    double radius = 0.0;    // distancia al origen
    double angleDeg = 0.0;  // (-180, 180], 0 = +X, antihorario positivo
};

// Resuelve el tablero para el frame actual. Si el origen depende de la pieza y
// la pieza NO se detectó, cae al centro de la imagen: el tablero se sigue
// pudiendo dibujar y leer en vez de desaparecer justo cuando falla la
// detección (que es cuando el operador más lo necesita).
[[nodiscard]] BoardFrame resolveBoardFrame(const BoardConfig& config, const Fixture& fixture,
                                           bool pieceFound, const cv::Size& imageSize);

// Punto de imagen -> lectura en el tablero, y su inversa exacta.
[[nodiscard]] BoardReading readPoint(const BoardFrame& frame, const cv::Point2f& imagePoint);
[[nodiscard]] cv::Point2f toImagePoint(const BoardFrame& frame, double dx, double dy);

// Desviación de la pieza respecto al tablero: posición de su centro más cuánto
// está girada respecto a los ejes (en (-180, 180]).
[[nodiscard]] BoardReading readPiece(const BoardFrame& frame, const Fixture& fixture);
[[nodiscard]] double pieceAngleOffset(const BoardFrame& frame, const Fixture& fixture);

// Misma lectura expresada en milímetros. Con mmPerPixel <= 0 (sin calibrar)
// devuelve la lectura tal cual, en píxeles: nunca inventa milímetros.
[[nodiscard]] BoardReading toMillimeters(const BoardReading& reading, double mmPerPixel);

// Paso de grilla "redondo" (…1, 2, 5, 10, 20, 50…) para que un tramo visible de
// `span` unidades quede dividido en del orden de `targetDivisions` casillas.
// Así la grilla no satura al alejar ni desaparece al acercar.
[[nodiscard]] double niceGridStep(double span, int targetDivisions = 10);

// Normaliza un ángulo a (-180, 180].
[[nodiscard]] double normalizeAngle(double degrees);

// Claves estables para persistir la elección (BD/Settings).
[[nodiscard]] std::string_view originKey(BoardOrigin origin);
[[nodiscard]] BoardOrigin originFromKey(std::string_view key,
                                        BoardOrigin fallback = BoardOrigin::PieceCenter);

}  // namespace pci::vision
