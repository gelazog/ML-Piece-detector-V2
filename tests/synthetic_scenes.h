#pragma once

// ESCENAS DIBUJADAS A PARTIR DE SUS COTAS.
//
// Las fotos reales dicen cómo se COMPORTA el programa; solo una imagen
// construida dice si el número es CORRECTO. En una foto de un engranaje no se
// sabe cuánto mide de verdad su agujero: se puede medir dos veces y comparar,
// pero no se puede afirmar que el resultado sea el bueno.
//
// Aquí es al revés: la cota va primero y la imagen se dibuja a partir de ella,
// así que la verdad de campo es exacta por construcción y no algo medido sobre
// el resultado. Eso es lo que permite escribir «esto tiene que dar 62,5 mm» en
// vez de «esto tiene que dar lo mismo que la vez anterior».
//
// Las figuras reproducen las piezas reales que usa el usuario para probar —un
// tablero acotado, un engranaje con dientes y agujero, tres tornillos de
// longitudes distintas— porque cada una rompe el pipeline por un sitio
// distinto: el tablero por la escala, el engranaje por los agujeros y el
// dentado, y los tornillos por el recuento y el orden de lectura.
//
// TODO se dibuja con `cv::LINE_8` (sin antialias) a propósito: con bordes
// suavizados el recuento de píxeles depende del umbral y deja de ser exacto,
// que es justo lo que se viene a evitar.

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <cmath>
#include <vector>

namespace pci::testing_support {

// Niveles de gris de las escenas. Separados de sobra para que ningún umbral
// razonable falle por contraste: lo que se prueba aquí es la MEDIDA, no la
// segmentación, y una escena al límite mezclaría los dos fallos.
inline constexpr int kSceneBackground = 30;
inline constexpr int kScenePiece = 225;

// ---------------------------------------------------------------------------
// Tablero de ajedrez acotado
// ---------------------------------------------------------------------------

// Reproduce la foto de producto del usuario: un tablero cuadrado con una medida
// conocida de lado a lado. Ahí la cota que se conoce es la del TABLERO ENTERO
// («50 CM»), no la de una casilla — y de ahí sale la casilla por división.
//
// Es el caso de uso literal del asistente de escala: marcas dos puntos sobre
// algo cuyo tamaño real conoces y el programa deduce los milímetros por píxel.
struct BoardScene {
    cv::Mat gray;
    // Casillas por lado (8 en un tablero de ajedrez).
    int squares = 8;
    // Esquinas INTERNAS, que es lo que busca cv::findChessboardCorners: una
    // menos que casillas por lado.
    int innerCorners = 7;
    // Lado de casilla en píxeles. Entero a propósito: así el tablero cae en la
    // rejilla sin redondeos y su ancho en píxeles es exacto.
    int squarePx = 60;
    // La zona jugable (las 8x8 casillas), en píxeles.
    cv::Rect playing;
    // La escala con la que se dibujó. Es LA VERDAD: cualquier calibración que
    // haga el programa a partir de esta imagen tiene que devolver esto.
    double mmPerPixel = 1.0;
    // Cotas derivadas, exactas por construcción.
    [[nodiscard]] double playingWidthPx() const {
        return static_cast<double>(squares) * squarePx;
    }
    [[nodiscard]] double playingWidthMm() const { return playingWidthPx() * mmPerPixel; }
    [[nodiscard]] double squareMm() const { return squarePx * mmPerPixel; }
};

// `mmPerPixel` es la escala que se quiere reproducir. Con 8 casillas de 60 px y
// 1,0417 mm/px sale un tablero de 500 mm, que es el de la foto real.
inline BoardScene chessboard(int squares = 8, int squarePx = 60,
                             double mmPerPixel = 500.0 / (8.0 * 60.0), int margin = 40) {
    BoardScene scene;
    scene.squares = squares;
    scene.innerCorners = squares - 1;
    scene.squarePx = squarePx;
    scene.mmPerPixel = mmPerPixel;

    const int side = squares * squarePx;
    const int total = side + 2 * margin;
    // Fondo CLARO alrededor, como la foto de producto: el tablero no es «la
    // pieza sobre una mesa oscura», es una lámina blanca con casillas negras.
    scene.gray = cv::Mat(total, total, CV_8UC1, cv::Scalar(kScenePiece));
    scene.playing = cv::Rect(margin, margin, side, side);

    for (int row = 0; row < squares; ++row) {
        for (int col = 0; col < squares; ++col) {
            if ((row + col) % 2 == 0) {
                continue;  // las claras ya están pintadas por el fondo
            }
            const cv::Rect cell(margin + col * squarePx, margin + row * squarePx, squarePx,
                                squarePx);
            cv::rectangle(scene.gray, cell, cv::Scalar(kSceneBackground), cv::FILLED,
                          cv::LINE_8);
        }
    }
    return scene;
}

// ---------------------------------------------------------------------------
// Engranaje
// ---------------------------------------------------------------------------

// Un engranaje rompe el pipeline por dos sitios que ninguna otra figura toca:
//
//  · Tiene un AGUJERO. El contorno exterior relleno da un área; el área de la
//    pieza de verdad es esa menos el agujero. Confundirlas es un error del
//    tamaño del agujero, y en un engranaje el agujero no es pequeño.
//  · Tiene DIENTES, que son el caso extremo del dentado del contorno: el
//    perímetro se dispara mientras el área apenas cambia.
struct GearScene {
    cv::Mat gray;
    cv::Point centre;
    // Radio del cuerpo, sin dientes.
    double bodyRadiusPx = 0.0;
    // Cuánto sobresale cada diente por encima del cuerpo.
    double toothHeightPx = 0.0;
    // Radio del agujero central.
    double boreRadiusPx = 0.0;
    int teeth = 0;

    // Área del cuerpo SIN dientes y SIN agujero, exacta.
    [[nodiscard]] double bodyAreaPx2() const {
        return CV_PI * (bodyRadiusPx * bodyRadiusPx - boreRadiusPx * boreRadiusPx);
    }
    // Área encerrada por el contorno exterior del cuerpo, agujero incluido.
    // Es lo que mide quien rellena el contorno.
    [[nodiscard]] double filledBodyAreaPx2() const {
        return CV_PI * bodyRadiusPx * bodyRadiusPx;
    }
    [[nodiscard]] double boreAreaPx2() const { return CV_PI * boreRadiusPx * boreRadiusPx; }
    [[nodiscard]] double boreDiameterPx() const { return 2.0 * boreRadiusPx; }
    [[nodiscard]] double outerDiameterPx() const {
        return 2.0 * (bodyRadiusPx + toothHeightPx);
    }
};

inline GearScene gear(int size = 400, double bodyRadiusPx = 130.0,
                      double toothHeightPx = 18.0, double boreRadiusPx = 34.0,
                      int teeth = 28) {
    GearScene scene;
    scene.centre = {size / 2, size / 2};
    scene.bodyRadiusPx = bodyRadiusPx;
    scene.toothHeightPx = toothHeightPx;
    scene.boreRadiusPx = boreRadiusPx;
    scene.teeth = teeth;
    scene.gray = cv::Mat(size, size, CV_8UC1, cv::Scalar(kSceneBackground));

    // Cuerpo.
    cv::circle(scene.gray, scene.centre, static_cast<int>(std::lround(bodyRadiusPx)),
               cv::Scalar(kScenePiece), cv::FILLED, cv::LINE_8);

    // Dientes: trapecios apoyados en el cuerpo, repartidos por el contorno.
    const double step = 2.0 * CV_PI / teeth;
    const double halfWidth = step * 0.28;  // el diente ocupa poco más de medio hueco
    for (int i = 0; i < teeth; ++i) {
        const double angle = i * step;
        const auto at = [&](double a, double r) {
            return cv::Point(
                static_cast<int>(std::lround(scene.centre.x + r * std::cos(a))),
                static_cast<int>(std::lround(scene.centre.y + r * std::sin(a))));
        };
        const std::vector<cv::Point> tooth = {
            at(angle - halfWidth, bodyRadiusPx - 2.0),
            at(angle - halfWidth * 0.55, bodyRadiusPx + toothHeightPx),
            at(angle + halfWidth * 0.55, bodyRadiusPx + toothHeightPx),
            at(angle + halfWidth, bodyRadiusPx - 2.0)};
        cv::fillConvexPoly(scene.gray, tooth, cv::Scalar(kScenePiece), cv::LINE_8);
    }

    // Y el agujero central, del color del fondo.
    cv::circle(scene.gray, scene.centre, static_cast<int>(std::lround(boreRadiusPx)),
               cv::Scalar(kSceneBackground), cv::FILLED, cv::LINE_8);
    return scene;
}

// ---------------------------------------------------------------------------
// Tornillos
// ---------------------------------------------------------------------------

// Tres tornillos de la MISMA cabeza y DISTINTA longitud, en fila. Reproduce la
// foto real del usuario y sirve para tres cosas a la vez: el recuento, el orden
// de lectura (tienen que salir numerados de izquierda a derecha) y una cota que
// distingue unas piezas de otras (la longitud).
struct ScrewsScene {
    cv::Mat gray;
    // Longitudes totales en píxeles, EN ORDEN DE LECTURA (izquierda a derecha).
    std::vector<double> lengthsPx;
    double headWidthPx = 0.0;
    double shaftWidthPx = 0.0;
    // Centros de cada tornillo, en el mismo orden.
    std::vector<cv::Point> centres;
};

inline ScrewsScene screws(int width = 800, int height = 800,
                          std::vector<double> lengths = {320.0, 440.0, 560.0},
                          double headWidthPx = 130.0, double shaftWidthPx = 78.0) {
    ScrewsScene scene;
    scene.lengthsPx = lengths;
    scene.headWidthPx = headWidthPx;
    scene.shaftWidthPx = shaftWidthPx;
    scene.gray = cv::Mat(height, width, CV_8UC1, cv::Scalar(kSceneBackground));

    const int count = static_cast<int>(lengths.size());
    const int slot = width / std::max(1, count);
    // La cabeza abajo y el vástago hacia arriba, como en la foto real. Todas
    // apoyadas en la misma línea para que el orden de lectura sea inequívoco:
    // con alturas escalonadas, el agrupado por filas podría partirlas en dos
    // filas y el orden dejaría de ser el que ve una persona.
    const int baseline = height - 90;
    const int headHeight = static_cast<int>(std::lround(headWidthPx * 0.55));

    for (int i = 0; i < count; ++i) {
        const int cx = slot / 2 + i * slot;
        const double total = lengths[static_cast<std::size_t>(i)];
        const int shaftHeight = static_cast<int>(std::lround(total)) - headHeight;

        const cv::Rect head(cx - static_cast<int>(std::lround(headWidthPx / 2.0)),
                            baseline - headHeight,
                            static_cast<int>(std::lround(headWidthPx)), headHeight);
        const cv::Rect shaft(cx - static_cast<int>(std::lround(shaftWidthPx / 2.0)),
                             head.y - shaftHeight,
                             static_cast<int>(std::lround(shaftWidthPx)), shaftHeight);
        cv::rectangle(scene.gray, head, cv::Scalar(kScenePiece), cv::FILLED, cv::LINE_8);
        cv::rectangle(scene.gray, shaft, cv::Scalar(kScenePiece), cv::FILLED, cv::LINE_8);
        scene.centres.push_back({cx, baseline - static_cast<int>(std::lround(total / 2.0))});
    }
    return scene;
}

}  // namespace pci::testing_support
