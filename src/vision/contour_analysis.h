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
[[nodiscard]] std::vector<PieceContour> findPieceContours(const cv::Mat& mask,
                                                          double minAreaFraction = 0.005,
                                                          double maxAreaFraction = 0.9,
                                                          int maxCount = 64);

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

}  // namespace pci::vision
