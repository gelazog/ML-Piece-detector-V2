#pragma once

#include <opencv2/core.hpp>

#include <array>

namespace pci::vision {

// REALZAR LO QUE SE VE, SIN TOCAR LO QUE SE MIDE.
//
// Viene de una queja de uso: «si la pieza es negra, y el demás cuadro es negro
// no se alcanza a ver correctamente». Y es literal — una pieza mate oscura sobre
// un fondo oscuro ocupa treinta niveles de gris de los 256 que hay, así que en
// pantalla es una mancha negra dentro de otra mancha negra. La detección puede
// estar funcionando perfectamente y el operador no tiene forma de saberlo.
//
// LA REGLA QUE NO SE PUEDE ROMPER: esto es para el RENDER y solo para el render.
//
// Ya existe una forma de subir el brillo en el programa —los controles de la
// cámara, en «Cámara e imagen»— y esa cambia el fotograma que se analiza: mueve
// el umbral de Otsu, mueve la polaridad y mueve todas las cotas. Es lo correcto
// para arreglar una iluminación mala, y es exactamente lo que NO se quiere aquí:
// subir el brillo para poder ver no puede cambiar cuánto mide la pieza. Si lo
// hiciera, las medidas se moverían por mirar.
//
// Por eso esto devuelve una TABLA y no una imagen retocada: quien la aplique lo
// hace sobre la copia que se pinta, y el `cv::Mat`/`QImage` del que salen las
// medidas no se toca en ningún momento.

// Tabla de 256 entradas que estira el rango realmente usado a la escala
// completa, más lo que hizo falta saber para construirla.
struct ContrastStretch {
    std::array<unsigned char, 256> lut{};
    // Los dos extremos del rango útil que se encontraron.
    int low = 0;
    int high = 255;
    // false = la imagen ya usa casi toda la escala y estirarla no cambiaría nada
    // que se pueda ver. Se dice en vez de aplicar una tabla que es la identidad,
    // porque el llamador puede ahorrarse la copia entera del fotograma — y
    // porque al operador hay que poder decirle «esta imagen no necesita realce»
    // en lugar de dejarle dudando de si el interruptor funciona.
    bool useful = false;

    [[nodiscard]] unsigned char map(unsigned char value) const { return lut[value]; }
};

// Por debajo de este recorrido, la imagen ya usa bastante escala y realzarla no
// aporta. 200 de 255: deja pasar el caso oscuro de verdad y no se dispara con
// una imagen normal que simplemente no llega al blanco puro.
inline constexpr int kAlreadyWideRange = 200;

// Menos recorrido que esto no se puede estirar sin convertir el ruido del sensor
// en bandas de color: con ocho niveles útiles, cada nivel pasa a ser un salto de
// 32 y lo que sale es un mapa de manchas, no una pieza.
inline constexpr int kUnstretchableRange = 8;

// `tailFraction` es qué parte de los píxeles se deja fuera por cada extremo.
//
// Se usan PERCENTILES y no el mínimo y el máximo, y es la decisión que hace que
// esto sirva de algo: basta un píxel muerto en negro y un reflejo especular
// quemado en blanco —los dos, cosas normales en una mesa de inspección— para que
// el rango vaya de 0 a 255 y el estirado no haga absolutamente nada. Justo en
// las escenas difíciles, que es cuando se enciende.
[[nodiscard]] ContrastStretch autoContrastLut(const cv::Mat& image,
                                              double tailFraction = 0.01);

// Aplica la tabla a una copia. Nunca modifica `image`.
//
// Sobre color se aplica la MISMA tabla a los tres canales: una tabla por canal
// equilibraría los blancos y de paso cambiaría el color de la pieza, y el color
// es una de las cosas por las que el operador la reconoce.
[[nodiscard]] cv::Mat applyStretch(const cv::Mat& image, const ContrastStretch& stretch);

}  // namespace pci::vision
