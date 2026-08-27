#pragma once

#include <opencv2/core.hpp>

#include <vector>

#include "core/result.h"
#include "vision/types.h"

namespace pci::vision {

// Busca el contorno externo de mayor área en una máscara binaria.
// minAreaFraction descarta ruido/escena vacía; maxAreaFraction descarta
// segmentaciones degeneradas (p. ej. iluminación fallida que marca todo).
core::Result<PieceContour> findLargestContour(const cv::Mat& mask,
                                              double minAreaFraction = 0.005,
                                              double maxAreaFraction = 0.9);

// Todos los contornos externos que pasan el filtro de área, **en orden de
// lectura**: por filas de arriba abajo, y dentro de cada fila de izquierda a
// derecha. Es lo que hace falta para contar piezas: hasta ahora la aplicacion se
// quedaba con el mayor y borraba el resto en silencio, asi que una bandeja con
// cinco tornillos y otra con seis daban el mismo resultado.
//
// EL ORDEN ERA POR AREA, Y ESE ERA EL PROBLEMA.
//
// La queja que trajo este cambio es de uso: «las detecta aleatoriamente». Y era
// literal. Se ordenaba por area con un `std::sort` —que ni siquiera es estable—,
// asi que con seis tornillos iguales el orden relativo era arbitrario. Peor: el
// area de cada pieza baila unos pixeles de un fotograma al siguiente por el
// umbral y la morfologia, de modo que dos piezas casi iguales se intercambiaban
// el puesto continuamente. «Pieza 2» era un tornillo distinto cada vez, y el
// informe que la nombraba no significaba nada.
//
// El orden de lectura arregla eso porque no depende de una medida que fluctua,
// sino de DONDE ESTA la pieza. Es ademas el orden que un operador da por
// supuesto cuando mira una bandeja y dice «la tercera».
//
// Lo que este orden NO promete, y conviene decirlo: si una pieza se mueve de
// sitio, su numero cambia. Es correcto —el numero describe una posicion, no una
// identidad— pero significa que no sirve para seguir una pieza que se desplaza.
//
// `maxCount` acota el destrozo cuando la segmentacion se va de las manos: con
// una iluminacion mala pueden salir cientos de manchas, y analizarlas todas
// costaria mas que decir que la escena no sirve. Se queda con las MAYORES —que
// son las candidatas a ser piezas— y solo despues las pone en orden de lectura.
//
// EL TOPE ERA 64 Y TRUNCABA EN SILENCIO. Lo destapo un caso de uso: una bandeja
// de 100 tuercas. La deteccion las encontraba TODAS —las 100 pasaban el filtro
// de area, cada una con su contorno bien trazado— y esta funcion devolvia 64 sin
// decir nada. El operador veia «64 piezas» y no tenia forma de saber que le
// faltaban 36.
//
// Y el motivo por el que 64 era demasiado bajo se puede razonar, no hace falta
// elegirlo a ojo: con `minAreaFraction` en 0,005, en un encuadre no caben mas de
// 200 piezas. O sea que el tope estaba POR DEBAJO del limite natural que ya
// impone el filtro de area, y lo unico que podia recortar eran escenas
// legitimas. Puesto por encima de ese limite, protege de la segmentacion
// degenerada —que es para lo que esta— sin cortar nada real.
//
// El coste que supuestamente justificaba el tope tambien se midio: encontrar los
// contornos de esas 100 tuercas cuesta 1,86 ms.
//
// `discarded`, si se pasa, recibe cuantas se quedaron fuera. Truncar en silencio
// es el fallo que trajo todo esto, asi que quien recorte tiene ahora como
// decirlo.
inline constexpr int kMaxPieces = 256;

// Cuánto hay que adentrarse en una mancha para dar con su corazón, en fracción
// de su radio máximo.
//
// Barrido medido, con dos discos de 260 px de diámetro solapándose y con las
// imágenes reales (piezas correctas marcadas):
//
//              r=0,45  r=0,50  r=0,55  r=0,60  r=0,65
//   solape 20      1      2✓      2✓       1       1
//   solape 35      1       1      2✓       1       1
//   solape 50      1       1       1       1       1
//   engranajes     2✓     2✓      2✓      2✓       1
//   bandeja 100  100✓   100✓    100✓    100✓    100✓
//   tornillos 3    3✓     3✓      3✓      3✓      3✓
//
// 0,55 es el único que aguanta 35 píxeles de solape, y no cuesta nada en los
// demás casos. Por debajo los corazones crecen y se funden antes; por encima se
// quedan tan pequeños que los dos engranajes vuelven a contar como uno.
inline constexpr double kTouchingCoreRatio = 0.55;
// Cuánto tiene que pesar un corazón para contar como pieza, en fracción del
// área de su mancha. Sin esto, un pico de ruido en la punta de un diente de
// engranaje contaría como una pieza más.
inline constexpr double kTouchingCoreMinFraction = 0.02;

// SEPARAR LAS PIEZAS QUE SE TOCAN, en una máscara ya segmentada.
//
// `RETR_EXTERNAL` devuelve una sola mancha cuando dos piezas se rozan. El
// operador ve dos piezas y el programa cuenta una — y entonces no hay nada que
// recorrer con las flechas ni que enseñar en el mosaico.
//
// Cada mancha se mira POR DENTRO: se calcula su transformada de distancia y se
// cuentan los «corazones», las zonas más alejadas del fondo. Dos piezas pegadas
// tienen dos corazones separados por un cuello estrecho; una pieza sola tiene
// uno. El umbral va relativo al radio de ESA mancha, así que vale igual para
// una tuerca pequeña que para un engranaje grande — que es justo lo que un
// umbral global sobre la imagen entera no consigue: medido, el valor que separa
// los engranajes destroza la bandeja de cien tuercas (de 100 piezas a 0).
//
// Devuelve la máscara con las piezas separadas por una línea de fondo de un
// píxel. Si no encuentra nada que separar devuelve la máscara tal cual.
[[nodiscard]] cv::Mat splitTouchingPieces(const cv::Mat& mask,
                                          double coreRatio = kTouchingCoreRatio);


// `discarded` son las que sobran del TOPE de piezas; `belowMinArea`, las que se
// caen por debajo del área mínima. Son dos cosas distintas y llevan a dos
// arreglos distintos —subir el tope, o bajar el mínimo—, así que se cuentan
// aparte.
//
// El segundo nació porque no existía: esas manchas se descartaban con un
// `continue` y no las contaba nadie. Medido sobre `arandelas-2`, una foto de
// catálogo con dieciséis arandelas graduadas, el mínimo de fábrica deja UNA — y
// el operador solo veía «1 pieza», sin motivo y sin nada que tocar.
[[nodiscard]] std::vector<PieceContour> findPieceContours(const cv::Mat& mask,
                                                          double minAreaFraction = 0.005,
                                                          double maxAreaFraction = 0.9,
                                                          int maxCount = kMaxPieces,
                                                          int* discarded = nullptr,
                                                          int* belowMinArea = nullptr);

// Pone en orden de lectura una lista ya encontrada. Publica porque el orden es
// una decision que hay que poder comprobar por separado de la deteccion.
//
// La tolerancia de fila sale de la ALTURA MEDIANA de las piezas y no de un
// numero de pixeles fijo: acercar o alejar la camara cambia el tamaño de todo en
// la imagen, y una tolerancia en pixeles que valia a 30 cm parte las filas por la
// mitad a 60.
void orderPiecesForReading(std::vector<PieceContour>& pieces);

// La pieza de mayor area de una lista, o `nullptr` si esta vacia.
//
// Existe porque el orden dejo de ser por area: quien necesitaba la mayor la
// cogia con `front()`, y eso ahora devolveria la de arriba a la izquierda. Un
// cambio silencioso de que pieza se mide es exactamente el fallo que no se ve
// hasta que alguien compara dos informes.
[[nodiscard]] const PieceContour* largestPiece(const std::vector<PieceContour>& pieces);

// Lo mismo sobre una lista de piezas ya ANALIZADAS, devolviendo su indice. 0 si
// la lista esta vacia.
//
// Existe porque `largestPiece` no encajaba donde hacia falta —los llamadores
// tienen `PieceAnalysis` y no `PieceContour`— y el resultado fue que los dos
// sitios que necesitaban «la mayor» escribieran el bucle a mano. Dos copias de
// la misma decision en dos ficheros distintos es una que se puede cambiar sin
// la otra, y «que pieza se mide» es exactamente la decision que no se puede
// permitir divergir en silencio: en vivo se veria una y el informe traeria la
// de la otra.
[[nodiscard]] std::size_t largestPieceIndex(const std::vector<PieceAnalysis>& pieces);

// CUÁL ES «LA PIEZA QUE SE MIDE», con el navegador de piezas de por medio.
//
// `wanted` va en ORDEN DE LECTURA empezando por 1. El cero es un estado con
// nombre —«la que decidas tú»— y significa la mayor, que es lo que la
// aplicación ha hecho siempre.
//
// Existe por el mismo motivo que `largestPieceIndex` y un escalón más arriba.
// Aquella nació porque dos sitios habían copiado el bucle de «la mayor», y su
// comentario avisa de que qué pieza se mide no puede divergir en silencio
// porque «en vivo se vería una y el informe traería la de la otra».
//
// Era literalmente lo que pasaba. El navegador solo lo entendía el camino del
// vídeo; «Medir pieza» y la medición automática llamaban a `analyzeFrame`, que
// devuelve la mayor y no sabe nada de navegadores. Así que el operador ponía el
// selector en la pieza 3, veía sus cotas dibujadas encima de ella, pulsaba
// «Medir pieza» — y recibía el informe de otra, sin que nada lo dijera.
//
// Si el número señalado se sale —las piezas cambiaron de sitio, desapareció
// una— se vuelve a la mayor en vez de no medir nada. Un encuadre que deja de
// dar cotas porque falta la pieza 5 es peor que uno que mide la que hay.
[[nodiscard]] std::size_t measuredPieceIndex(const std::vector<PieceAnalysis>& pieces,
                                             int wanted);

}  // namespace pci::vision
