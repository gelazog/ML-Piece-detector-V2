# Rendimiento, cámara e interfaz — que la estación vaya rápida, mida igual dos veces y se entienda

Tres temas que parecían tres y, medidos, resultaron ser uno con tres caras.

## Lo que se midió antes de escribir esto

No es una lista de sospechas. Se abrió la cámara real de esta máquina y se
cronometró, porque planificar rendimiento a ojo es como acotar a ojo.

**Cuántos fps da la cámara según cómo se abra** (DirectShow, `BUFFERSIZE=1`,
60 frames por medida, luz de habitación):

| Cómo se abre | Resolución | fps |
| --- | --- | --- |
| **Como la abre la app hoy** | 640×480 | **8,0** |
| Igual + MJPG | 640×480 | 8,0 |
| Pidiendo 1280×720, con y sin MJPG | 1280×720 | 8,0 |
| Pidiendo 1920×1080 (la cámara da 720p) | 1280×720 | 8,0 |
| **Exposición fija en −4** | 640×480 | **16,0** |
| **Exposición fija en −6 o −7** | 640×480 | **30,5** |

Tres cosas salen de esa tabla, y las tres mandan sobre el resto del plan:

1. **El cuello de hoy no es el análisis: es la exposición.** 8,0 fps clavados en
   toda resolución y todo formato no es ancho de banda, es el sensor integrando
   ~125 ms por frame. Fijar la exposición da **3,8×** sin tocar una línea de
   visión por computador. Ninguna optimización de código de este plan se acerca.
2. **MJPG no aporta nada aquí.** Era la sospecha razonable —en muchas webcams es
   la diferencia entre 30 y 5 fps— y se midió antes de escribir el ítem. No lo
   hay: 8,0 con y sin. Se deja escrito para que nadie lo intente otra vez.
3. **La app no fija la exposición en ningún sitio.** Solo reaplica lo que el
   operador guardó en la sesión anterior. En una instalación nueva la cámara
   arranca en automático, que es lo peor de los dos mundos: lenta *y* no
   repetible.

**Dónde se va el tiempo del análisis** (ya medido y documentado en
`ARQUITECTURA.md`, sección 2): 23,1 ms sobre 2560×1440, del que la segmentación
es hoy el 34 %. A 640×480 es mucho menos. Con la cámara a 30 fps (33 ms por
frame) el análisis **cabe**, pero sin margen holgado: por eso el bloque `R`
existe, pero va después del bloque `C`.

**Un aviso sobre el brillo, que es la contrapartida honesta**: con exposición
−6 el brillo medio de la imagen cayó a 2,8 sobre 255 en esta habitación. Una
exposición corta **exige luz**. Una estación de inspección se supone iluminada,
pero el programa no puede suponerlo: tiene que medirlo y decirlo. Eso es `C3`.

## Por qué ahora

El backlog de herramientas se cerró: hay 32 herramientas y todas miden lo que
dicen. Lo que queda por debajo es la estación: va a 8 fps, arranca con la
cámara en automático —o sea que **la misma pieza puede dar dos números** según
cuánta luz haya entrado— y el operador no tiene forma de enterarse de ninguna de
las dos cosas mirando la pantalla.

Ese es el orden de importancia y es también el orden del plan: primero que la
medida sea repetible (`C`), luego que vaya rápida (`R`), luego que se vea lo que
está pasando (`I`).

## Lo que NO cambia

- La arquitectura por capas. Nada de `camera/` sube a `ui/` ni al revés.
- El análisis sigue fuera del hilo de la interfaz (`QtConcurrent::run` con
  descarte del frame viejo). Ya está bien resuelto: no se toca.
- Ninguna medida cambia de valor. Todo lo de `R` es optimización de código que
  ya funciona, así que la prueba que vale no es «da un número razonable» sino
  **«da exactamente lo de antes»**, con implementación de referencia y
  comparación, como se hizo con `computeFixture` y `normalizePiece`.
- No se añade un modo nuevo a la interfaz para ganar velocidad. Esa puerta ya se
  cerró con la escala de trabajo adaptativa y sigue cerrada: si una optimización
  no se puede aplicar sola, no entra.
- La paleta de herramientas la cubre `PROMPT_PALETA_VISUAL.md` (P1–P8) y **este
  plan no la duplica**. El bloque `I` es lo que queda de interfaz aparte de eso.

---

## Bloque C — La cámara: qué valores por defecto y por qué

### - [ ] C1 — Un perfil de arranque para medir, no para videollamada

Los valores que trae una webcam están pensados para que una cara se vea bien:
automático todo, que la imagen se adapte sola. Para medir, «que se adapte sola»
es exactamente el defecto — significa que **el borde de la pieza se mueve
cuando cambia la luz de la nave**.

Añadir a `camera/camera_controls.{h,cpp}` un perfil declarado:

```cpp
// Los valores con los que se abre una cámara que no se ha configurado nunca.
// No son "los buenos": son los que hacen que dos medidas de la misma pieza
// den lo mismo, que es otra cosa y es la que importa aquí.
struct MeasurementDefaults { CameraProperty property; double value; };
[[nodiscard]] const std::vector<MeasurementDefaults>& measurementDefaults();
```

Qué lleva y el porqué de cada uno, que es lo que hay que dejar escrito:

| Ajuste | Valor | Por qué |
| --- | --- | --- |
| `AutoFocus` | apagado | Un reenfoque **cambia la magnificación**. Con la escala calibrada, eso cambia todas las cotas a la vez y no lo delata nada en pantalla. Es el ajuste más peligroso de los siete. |
| `AutoExposure` | apagado | La exposición automática mueve el umbral aparente del borde: la misma pieza sale más gorda o más fina según la luz. Además cuesta los 3,8× medidos arriba. |
| `Exposure` | el más corto que deje la imagen usable (ver `C3`) | Fija la geometría y sube los fps. |
| `Gain` | bajo | La ganancia alta mete ruido en el borde, que es donde se mide. Antes de subir ganancia hay que subir luz. |

Aplicarlos **solo en la primera apertura de esa cámara**, nunca encima de lo
que el operador haya guardado: `propertyKey()` ya da la clave estable, así que
«no hay valor guardado» es la condición.

Verificación:
- Una cámara sin ajustes guardados recibe los del perfil; una con ajustes
  guardados **no** los recibe (el perfil no pisa al operador nunca).
- Un ajuste que la cámara declara no ajustable (`supported == false` tras
  `probeControls`) se salta sin ruido y queda en el log.
- El perfil es un `switch` sin `default` sobre `CameraProperty`: una propiedad
  nueva **rompe la compilación** hasta que alguien decida su valor de arranque.

### - [ ] C2 — Apagar el automático de verdad, y no fiarse de que lo diga

Detalle medido que va a morder a quien lo implemente: `set(CAP_PROP_AUTO_EXPOSURE, x)`
devolvió `true` con 0.25, 0.0 y 1.0, y `get()` devolvió **−1,000 en los tres
casos** — o sea que la cámara acepta la escritura y luego no informa del estado.
Lo que apagó el automático de hecho fue **escribir `CAP_PROP_EXPOSURE`**.

De ahí la regla: **no se comprueba leyendo la propiedad, se comprueba
midiendo el efecto.** Tras aplicar el perfil, medir los fps unos segundos y
compararlos con los de antes. Si no suben y el brillo no cambia, el automático
sigue puesto y hay que decirlo en vez de dar por hecho que se apagó.

Verificación: un doble de cámara que acepta las escrituras y las ignora tiene
que acabar con el aviso puesto, no con un «configurado correctamente».

### - [ ] C3 — Exposición corta necesita luz, y el programa lo tiene que decir

Con exposición −6 esta cámara dio 30,5 fps y **brillo medio 2,8 sobre 255**. Una
imagen así no se segmenta: bajar la exposición sin luz cambia el problema de
sitio en vez de resolverlo.

Al aplicar el perfil, buscar la exposición **más corta que deje la imagen
utilizable** en vez de fijar un número a ciegas: subir desde la más corta hasta
que el histograma tenga recorrido suficiente para que Otsu separe, y parar ahí.
Y si ni con la más larga hay contraste, decir lo que pasa —«no hay luz
suficiente: sube la iluminación»— en vez de dejar 8 fps y un borde inventado.

Verificación: sobre imágenes sintéticas con tres niveles de iluminación, la
búsqueda elige la exposición más corta que aún separa, y en el caso oscuro se
rinde con motivo. La regla se extrae a función pura (como se hizo con
`effectiveWorkingZone`), porque el bucle de cámara no tiene banco de pruebas.

### - [ ] C4 — «Restaurar los valores de medición» y el aviso que falta

Dos cosas pequeñas y la segunda es la que más vale:

1. Un botón en la pestaña de cámara que reaplica el perfil de `C1`. Hoy, quien
   toca los deslizadores hasta perderse **no tiene vuelta atrás** salvo borrar
   los ajustes guardados a mano.
2. **Un aviso cuando hay calibración px→mm y algún automático está encendido.**
   Es la combinación que produce números creíbles y falsos: la escala se fijó
   con una magnificación y el autofoco la cambió. El aviso va donde se ve la
   escala, no enterrado en una pestaña.

Y la regla de siempre, que aquí decide el diseño: el aviso **solo** aparece con
calibración activa **y** automático encendido. Sin calibrar, el autofoco es una
comodidad legítima y avisar sería ruido que se aprende a ignorar.

Verificación: los cuatro cuadrantes (con/sin calibración × con/sin automático)
y el aviso solo en uno.

---

## Bloque R — Rendimiento: medir antes, y solo lo que sobreviva a la medida

### - [ ] R1 — Enseñar los fps que importan, que no son los que se enseñan

Hoy la barra de estado dice `640x480 — 8.0 fps`, y esos son los fps de
**captura**, contados en el hilo de la cámara. El análisis descarta frames
cuando no llega (`maybeStartAnalysis` se salta si el anterior sigue corriendo) y
**eso no se ve en ninguna parte**. Una cámara a 30 fps con el análisis a 8
parece perfecta en pantalla y mide uno de cada cuatro.

Contar y mostrar tres números: **captura**, **análisis** y **frames
descartados**. Con la forma corta cuando coinciden (`30 fps`) y la larga cuando
no (`30 fps · analiza 8 · descarta 22`), porque un indicador que siempre enseña
tres números se deja de leer.

Verificación: `FpsCounter` ya es testeable con reloj inyectado. Un escenario con
captura rápida y análisis lento tiene que dar la cuenta de descartes exacta.

### - [ ] R2 — Cronometrar las etapas dentro de la app, no en un banco aparte

Los 23,1 ms de `ARQUITECTURA.md` se midieron con un programa suelto. Eso vale
una vez; lo que hace falta para no volver a optimizar a ciegas es que la propia
app sepa decir dónde se le va el tiempo, bajo demanda y sin coste cuando no se
pide.

Un desglose por etapas (`segmentPiece`, `findLargestContour`, `computeFixture`,
`normalizePiece`, `runTools`) acumulado sobre las últimas N ejecuciones, visible
en la pestaña *Rendimiento*.

Ojo con lo que **no** hay que hacer: no meter un cronómetro por etapa que corra
siempre. Se activa desde la pestaña y, apagado, no cuesta ni una llamada al
reloj.

Verificación: con el desglose apagado, el tiempo de `analyzeFrame` no empeora de
forma medible respecto a la referencia; encendido, las etapas suman el total
dentro de un margen pequeño (si no suman, el desglose miente).

### - [ ] R3 — `runTools` con muchas herramientas: el coste que nadie ha medido

Todo el reparto conocido se midió **sin herramientas dibujadas**. Una plantilla
real lleva diez o veinte, cada una con su barrido de perfiles, y se ejecutan por
frame. Puede que sea despreciable y puede que sea el nuevo primer puesto: hoy no
se sabe, y ese es el problema.

Medir con 1, 5, 10 y 20 herramientas de las caras (Rosca, Engranaje, Perfil,
Ranura son las que más barren). **Solo si la medida lo justifica**, actuar; y la
acción sensata sería no volver a segmentar por herramienta cuando varias
comparten región, no paralelizar a lo loco.

Verificación: la tabla de tiempos, escrita en `ARQUITECTURA.md` esté como esté
el resultado. Un «se midió y no hacía falta» es un resultado que ahorra trabajo
al siguiente, exactamente como la escala de trabajo adaptativa.

### - [ ] R4 — La escala de trabajo, esta vez sin modo nuevo

`ARQUITECTURA.md` deja el cabo suelto explícito: la escala de trabajo valía
1,10× cuando se probó y valdría **~1,33×** sobre el reparto de hoy, y se
descartó porque «no justifica un modo nuevo en la interfaz». La objeción era al
**modo**, no a la técnica — y ya está demostrado que reducir **no mueve las
medidas** si el contorno se recupera a resolución completa (fixture a ±0,000 px).

Aplicarla **sola**, sin ajuste ni casilla, y únicamente cuando la pieza es lo
bastante grande para que compense. El umbral sale de medir, no de elegirlo.

Este ítem es el que más fácil se cae, y está bien que se caiga: **si con `C1`
puesto la cámara da 30 fps y el análisis va sobrado, 1,33× no le sirve a nadie**
y meter una ruta de código nueva a cambio de nada es un mal negocio. Decidir con
la medida de `R2` delante, y si se descarta, escribir por qué.

Verificación (si entra): el fixture y todas las cotas idénticas a la ruta sin
reducir, con implementación de referencia. Ni «parecidas» ni «dentro de
tolerancia»: idénticas.

---

## Bloque I — Interfaz: lo que queda aparte de la paleta

La paleta de herramientas **no está aquí**: la cubre `PROMPT_PALETA_VISUAL.md`
(P1–P8), sin empezar. Este bloque es lo demás.

### - [ ] I1 — Que el estado de la estación se lea de un vistazo

Hoy hay que abrir *Configurar* y recorrer pestañas para saber si estás midiendo
en condiciones. Los cuatro datos que deciden si una medida vale —**escala
calibrada, exposición fija, enfoque fijo y zona de trabajo**— tienen que estar
visibles sin abrir nada.

Una tira de estado con cuatro indicadores, cada uno en verde/ámbar con el motivo
en el tooltip, y un clic que lleva a la pestaña que lo arregla.

La regla de diseño que la gobierna: **ámbar solo cuando de verdad afecta a la
medida**. Sin calibración, el enfoque automático es ámbar; con calibración es
rojo. Cuatro luces siempre encendidas serían cuatro luces que nadie mira.

### - [ ] I2 — El panel «Configurar» ordenado por lo que decide, no por lo que es

Las pestañas de hoy (*Cámara e imagen*, *Detección*, *Rendimiento*, *Piezas*,
*Preferencias*) agrupan por **componente del programa**. El operador no piensa
en componentes: piensa en «no me detecta la pieza» o «va lento».

Con `C` y `R` dentro habrá más ajustes, y el momento de repensar el orden es
antes de añadirlos, no después. Reagrupar por síntoma y dejar los nombres de
componente como subtítulo.

Ojo: esto es lo que más fácil se convierte en mover cosas de sitio porque sí. La
prueba de que merece la pena es concreta — **para cada uno de los cinco síntomas
más comunes, el ajuste que lo arregla está a un clic** — y si no se cumple con
el orden nuevo, no se cambia nada.

### - [ ] I3 — Que el primer arranque no empiece en blanco

Una instalación nueva abre con la cámara en automático, sin calibrar y sin
plantilla, y no dice por dónde empezar. Con `C1` puesto, la mitad del problema
se resuelve sola; la otra mitad es decir los tres pasos —**enfocar, calibrar,
registrar la pieza**— una vez y no volver a molestar.

No un asistente modal de esos que se cierran sin leer: la tira de `I1` con los
tres primeros indicadores en ámbar ya lo dice, y basta con que el primer arranque
la señale.

---

## Procedimiento por ítem (el de siempre)

1. Implementar.
2. `cmake --build --preset mingw-release` limpio bajo `-Werror`.
3. `ctest --preset mingw-release` verde.
4. Humo real de la app.
5. Marcar la casilla con una nota de una línea de lo que se aprendió si hubo algo
   que medir.
6. Actualizar `README.md` / `ARQUITECTURA.md` si el ítem lo toca.
7. Commit atómico **sin firma** y push a `main`.
8. Cuando estén las casillas todas marcadas, **borrar este archivo**.

Lo que toca cámara real no se puede probar en `ctest`: la regla se extrae a
**función pura** y se prueba esa, y el bucle de cámara queda como cableado
delgado. Es lo que se hizo con `effectiveWorkingZone` y `modeAfterFixedZoneChanged`
cuando la zona de detección falló, y funcionó.

## Orden recomendado

`C1 → C2 → C3 → C4 → R1 → R2 → R3 → R4 → I1 → I2 → I3`

`C` va primero porque es donde está el 3,8× y porque una medida no repetible es
un problema peor que una medida lenta. `C3` pegado a `C1` y `C2`: aplicar el
perfil sin la comprobación de luz deja la estación a oscuras, que es cambiar un
fallo por otro.

`R1` antes que `R2` porque enseñar los fps del análisis puede volver innecesario
medio bloque: si con `C1` puesto captura y análisis coinciden, no hay nada que
optimizar y `R3`/`R4` se cierran con un «se midió y no hacía falta».

`I` al final a propósito. Enseñar el estado de la estación (`I1`) solo tiene
sentido cuando hay estado que enseñar, y con `C` y `R` dentro lo habrá.

## Referencias consultadas

- [OpenCV — `VideoCaptureProperties`](https://docs.opencv.org/4.x/d4/d15/group__videoio__flags__base.html)
  — confirma que `CAP_PROP_AUTO_EXPOSURE` y `CAP_PROP_EXPOSURE` no tienen escala
  definida por la API y dependen del backend, que es justo lo que se midió en
  `C2`: la propiedad se escribe, se acepta y luego se lee −1.
- [Microsoft — `IAMCameraControl` / `IAMVideoProcAmp` (DirectShow)](https://learn.microsoft.com/en-us/windows/win32/directshow/iamcameracontrol-interface)
  — la exposición de DirectShow va en **log2 de segundos**, lo que explica que
  −4 diera exactamente la mitad de fps que −6 en la tabla de arriba.
- [Cognex — *Lighting and lens selection* (guías de visión industrial)](https://www.cognex.com/what-is/machine-vision/components/lighting)
  — la práctica del sector es exposición corta con iluminación controlada, no
  exposición larga: sostiene `C3` y su contrapartida.
- Medido en esta máquina, no consultado: la tabla de fps del principio y el
  reparto de tiempos de `ARQUITECTURA.md` sección 2.
