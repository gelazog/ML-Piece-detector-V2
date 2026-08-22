#pragma once

#include <opencv2/core.hpp>

#include "domain/capture_quality.h"
#include "vision/types.h"

namespace pci::vision {

// Calcula las métricas de calidad de una captura. `analysis` es el resultado
// de analyzeFrame sobre el mismo frame (nullptr si no se encontró pieza).
domain::QualityMetrics computeQualityMetrics(const cv::Mat& image,
                                             const PieceAnalysis* analysis);

// Nitidez (varianza del Laplaciano) de una región. `roi` vacío o fuera de la
// imagen = la imagen entera. Más alto = más nítido; no tiene tope, así que el
// número solo sirve **comparado consigo mismo** — que es justo lo que se hace
// al enfocar: buscar el máximo.
//
// Existe aparte de `computeQualityMetrics` a propósito. Aquella mide sobre el
// frame COMPLETO y su umbral de aceptación (`QualityCriteria::minSharpness`)
// está ajustado contra ese número; moverle la medida debajo cambiaría en
// silencio qué capturas se aceptan al registrar.
[[nodiscard]] double sharpnessOf(const cv::Mat& image, const cv::Rect& roi = {});

// Cuánto más largo es el contorno de lo que sería si la pieza fuera redonda.
//
// Se devuelve `perimetro / (2*raiz(pi*area))`, que vale **1 para un círculo** y
// crece con lo dentado o lo sucio que esté el borde. No depende de la escala:
// una pieza y la misma pieza al doble de tamaño dan el mismo número, que es lo
// que lo hace utilizable sin calibrar nada.
//
// Para qué sirve: distinguir «esta pieza tiene forma complicada» de «la
// detección está siguiendo el dibujo de la superficie en vez del borde». Medido
// sobre el corpus de fotografías reales, POR EL CAMINO QUE USA EL PROGRAMA —con
// su suavizado y su morfología— y no con un script aparte, que da otros números:
//
//   bola de 20 mm ................  1,59   contorno limpio
//   tres bolas sobre negro .......  1,72   contorno limpio
//   tuerca hexagonal .............  2,42   forma real con esquinas
//   ---------------------------------------------------------------
//   montón de piñones ............  5,88   no hay borde que seguir
//   arandelas en una bolsa .......  6,26   etiqueta y plástico
//   moneda con relieve grabado ... 10,03   sigue el grabado
//   bola junto a una regla ....... 21,96   sigue las marcas de la regla
//
// Cuando este número se dispara, el PERÍMETRO deja de significar lo que dice, y
// con él todo lo que se derive de él. Merece decírselo al operador: es la
// diferencia entre una medida mala y una medida mala que nadie ve.
[[nodiscard]] double contourRaggedness(double areaPx, double perimeterPx);

// A partir de cuánto conviene avisar.
//
// El umbral es un JUICIO y por eso está aquí, con nombre, en vez de repartido
// por el código.
//
// Sale de la BANDA VACÍA de la tabla de arriba: el contorno legítimo más
// dentado del corpus da 2,42 y el caso sospechoso más suave da 5,88. Poner el
// aviso en 3,0 deja margen a los dos lados en vez de rozar un caso bueno.
//
// Y el aviso dice las DOS posibilidades en vez de acusar a una: una pieza de
// verdad dentada —un piñón bien detectado— pasaría de 3 con toda la razón.
inline constexpr double kRaggedContourWarning = 3.0;
[[nodiscard]] bool contourLooksRagged(double areaPx, double perimeterPx);

}  // namespace pci::vision
