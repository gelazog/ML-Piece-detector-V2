#pragma once

#include <string>
#include <vector>

#include "inspection_editor/execution/tool_executor.h"
#include "inspection_editor/tools/tool_geometry.h"
#include "inspection_editor/tools/tool_types.h"

namespace pci::inspection {

// LA REFERENCIA QUE FALTA, DICHA AL DIBUJAR Y NO AL MEDIR.
//
// Cinco herramientas no miden nada sin una referencia declarada: Orientación y
// Desviación de centros necesitan un datum, el Punto y la Recta construidos
// necesitan de qué construirse, y el Patrón de agujeros necesita los agujeros.
// Medido sobre las 32, son cinco de las que no dan número, y todas dicen por
// qué… **cuando ya se ha medido**.
//
// Ese momento es tarde. El operador dibuja la herramienta, la ve dibujada, sigue
// trabajando, y se entera de que no mide cuando llega el veredicto — o cuando
// mira la tabla. Lo que hay que decirle es lo mismo, pero en el instante en que
// la dibuja, que es cuando todavía tiene el gesto en la mano.
//
// Y CUANDO LA RESPUESTA ES OBVIA, NO SE PREGUNTA. Si la pieza tiene una sola
// recta medida y se dibuja una Orientación —que se mide contra una recta—, no
// hay nada que elegir: se pone y se dice que se ha puesto. Con dos rectas sí hay
// elección, y ahí elegir por el operador sería peor que preguntarle: la
// herramienta mediría contra un datum que él no eligió y el número parecería
// correcto.
//
// La regla es exactamente ésa —**se resuelve sola cuando hay UNA candidata**— y
// no «la más cercana» ni «la primera»: un desempate inventado da una referencia
// plausible y equivocada, que es la peor de las tres salidas.

struct ReferenceAdvice {
    // Si esta herramienta lleva referencia. Falso en las 27 que no.
    bool needed = false;
    // La candidata única para `reference` y `reference2`, si la hay. Vacío
    // significa «no se elige sola», y el motivo está en `why`.
    std::string first;
    std::string second;
    // Qué falta, o por qué hay que elegir, en una frase. Va a la barra de
    // estado en el momento de dibujar.
    std::string why;
    // Cuántas candidatas había para cada operando. Se publica para poder
    // distinguir «no hay ninguna» de «hay varias»: son dos situaciones que
    // llevan a hacer cosas distintas —dibujar la que falta, o elegir—.
    int candidatesFirst = 0;
    int candidatesSecond = 0;
};

// Qué referencia le vendría bien a esta herramienta, mirando lo que las demás
// han producido en la última medición.
//
// Se parte de los RESULTADOS y no del tipo de las otras herramientas, y esa es
// la decisión que evita el fallo de siempre: qué elemento produce cada
// herramienta lo decide su ejecución —un Círculo da su centro, una Regla su
// recta— y una tabla escrita aparte que dijera lo mismo acabaría discrepando.
// Aquí se pregunta al resultado, que es la única fuente que no puede mentir.
[[nodiscard]] ReferenceAdvice adviseReference(const ToolConfig& config,
                                              const ToolGeometry& geometry,
                                              const std::vector<ToolRunResult>& measured);

}  // namespace pci::inspection
