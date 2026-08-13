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

### - [x] C1 — Un perfil de arranque para medir, no para videollamada

Una webcam viene ajustada para que una cara se vea bien: automático todo, que la
imagen se adapte sola. Para medir, «que se adapte sola» es exactamente el
defecto — significa que **el borde de la pieza se mueve cuando cambia la luz de
la nave**.

*(Cerrado, y la cámara real desmintió el diseño **dos veces** antes de dejarlo
en pie. Lo escribo entero porque el camino vale más que el destino:*

*1) **«Congelar cada control donde ya está.»** Suena inmejorable: repetibilidad
sin cambiarle la imagen a nadie. Medido: **29,7 -> 8,0 fps**. Con el automático
puesto, `get(CAP_PROP_EXPOSURE)` devuelve el nominal —el más largo del rango—
mientras el sensor usa exposiciones cortas de verdad. El valor reportado bajo
automático miente, igual que el del propio interruptor, que devuelve −1 pase lo
que pase.*

*2) **«Apagar el automático y elegir la exposición midiendo.»** Escribir solo
`auto_exposure = 0`, sin elegir valor, dejó la cámara en **8,0 fps**: al quitarle
el automático se cae a su manual, que era el más largo. De ahí una regla que
vale para toda la capa — **no se apaga un automático que no se pueda
sustituir**.*

*3) **La que quedó.** La exposición se elige por medida: la más larga que
todavía da la velocidad máxima, porque los fps son planos (30,2-30,3 de −11 a
−5) y se caen por un acantilado (−4 da 16,0, −3 da 8,0), no poco a poco. Y
encima el perfil **se juzga a sí mismo**: en esta máquina daba 30,0 fps y el
**21 % del contraste**, porque en automático la cámara gobierna también la
GANANCIA y aquí `gain` no es ajustable. Así que se descarta solo, vuelve a
automático y dice que con más luz sí compensaría.*

*Las tres versiones tenían tests verdes. Las tres veces lo dijo la cámara.)*

Lo que quedó implementado:

| Pieza | Qué hace |
| --- | --- |
| `measurementDefaults(probed, saved)` | Apaga los automáticos **que se pueden sustituir**, y solo los que el operador no haya tocado. Hoy eso es el autofoco. |
| `exposureCandidates(min, max)` | Las exposiciones que vale la pena probar dentro del rango medido, de la más larga a la más corta. |
| `chooseExposure(sweep)` | La más larga que aún da la velocidad máxima, con un 5 % de margen para el ruido de medida. |
| `judgeProfile(antes, después)` | Si el cambio se lo ha ganado. Si no, se deshace **diciendo por qué**. |

El experimento entero cuesta **3,2 s** y solo corre en cámaras sin configurar.
Se repite en cada arranque a propósito: si alguien enciende una lámpara, la
respuesta cambia sola.

### - [x] C2 — Apagar el automático de verdad, y no fiarse de que lo diga

*(Absorbido por C1, y no por comodidad: el diseño lo forzó. La regla que este
ítem pedía —«no se comprueba leyendo la propiedad, se comprueba midiendo el
efecto»— es exactamente lo que hace `judgeProfile`, y sin ella C1 no podía
cerrarse, porque las dos primeras versiones fallaron justo por creerse lo que
la cámara decía. Un ítem que resulta ser inseparable de otro se cierra con el
otro; dejarlo abierto para poder tacharlo dos veces sería contabilidad.)*

### - [x] C3 — Exposición corta necesita luz, y el programa lo tiene que decir

*(También absorbido, y por la misma razón. La contrapartida que este ítem
anticipaba resultó ser **más grave de lo previsto**: no es que una exposición
corta oscurezca la imagen, es que apagar el automático pierde además la
ganancia automática, y si `gain` no es ajustable —el caso de esta máquina— no
hay con qué reponerla. Por eso el aviso no podía ser un mensaje al final: tenía
que ser un veredicto capaz de DESHACER el perfil. Lo que este ítem pedía decir
—«no hay luz suficiente, sube la iluminación»— es el texto que emite
`profileRejected`.)*

### - [x] C4 — «Restaurar los valores de medición» y el aviso que falta

*(Las dos partes hechas. El botón obligó a añadir `SettingsRepository::remove`:
no valía poner los ajustes a cero, porque el perfil se salta a propósito toda
propiedad que el operador haya tocado y «cero» sigue siendo haberla tocado —
olvidar y poner a cero son estados distintos y hacía falta el primero.
El aviso salió más barato de lo previsto porque el sitio ya existía: la
etiqueta de la escala ya avisaba de «calibración obsoleta» por la misma razón,
así que reutiliza sitio y estilo. Lo que sí costó es que el estado de los
automáticos hay que LLEVARLO en la ventana —cuatro sitios lo cambian: sondeo,
perfil, veredicto del barrido y el operador—, porque preguntárselo a la cámara
no sirve: `get(CAP_PROP_AUTO_EXPOSURE)` devuelve −1 pase lo que pase.)*

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

### - [x] R1 — Enseñar los fps que importan, que no son los que se enseñan

*(La contabilidad se sacó a `FrameAccounting` en vez de dejarla suelta en la
ventana, y ahí apareció lo que había que probar: la invariante de que **cada
frame o se mide o se descarta**. Simulado con reloj inyectado —cámara a 30 fps,
análisis de 125 ms— da 7 medidos y 22 descartados, que es exactamente el
escenario que el ítem existía para hacer visible.
Dos umbrales que no son cosméticos: el descarte se enseña a partir de 2/s
porque dos contadores por ventana deslizante nunca cuadran al frame aunque el
análisis vaya sobrado, y congelar el contorno NO cuenta como descartar —sería
llamar avería a lo que el operador acaba de pedir.
Escribí una aserción que era una tautología (`X == X`) intentando expresar la
invariante; pasaba en verde sin probar nada. Sustituida por la suma de verdad.)*

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

### - [x] R2 — Cronometrar las etapas dentro de la app, no en un banco aparte

*(El «apagado no cuesta nada» sale de un `StageTimings*` opcional: con puntero
nulo no hay ni una llamada al reloj. Lo que costó pensar fue qué exigirle al
desglose para que no sea un adorno, y quedaron tres pruebas: que cronometrar no
cambie el resultado —recorte canónico comparado píxel a píxel—, que las etapas
sumen el total, y que el hueco sin atribuir se ENSEÑE en vez de esconderse,
porque si crece el reparto está señalando el sitio equivocado.
Medido en la suite sobre 800×600: total 3,53 ms = 1,60 segmentar + 0,44
contorno + 0,53 fixture + 0,96 normalizar, sin atribuir 0,00. `runTools` se
cronometra aparte porque corre fuera de `analyzeFrame` y en una plantilla real
puede ser el mayor coste — que es justo lo que R3 va a medir.)*

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

### - [x] R3 — `runTools` con muchas herramientas: el coste que nadie ha medido

*(Medido, y resultó ser el primer puesto: con veinte herramientas del tamaño de
las de verdad, `runTools` cuesta **27,8 ms**, el 83 % de un frame a 30 fps y
más que la segmentación, el fixture y el recorte juntos. Sumado al análisis no
cabe en el frame, así que el análisis descarta — que es justo lo que R1 acaba
de hacer visible.
Crece **lineal**, o sea que no hay trabajo repetido entre herramientas que
quitar: cada una escanea su región y eso es lo que cuesta. El test vigila la
forma, no el número.
Y el aviso de método: mi primer banco uso las geometrías de prueba que ya
existían y dio 0,5 ms con veinte, que habría cerrado el ítem con un
«despreciable». Eran de juguete —un Eje de 50 px con 12 cortes cuando el real
cruza 400 con 64— y el coste está dominado por los cortes. Medir el tamaño
equivocado no da un número impreciso: da la conclusión contraria. Ese banco se
borró en vez de dejarlo dando una cifra que engaña, y de paso era un generador
de fallos intermitentes: media décimas de milisegundo y solo fallaba con la
máquina ocupada.)*

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

### - [x] R4 — La escala de trabajo se descarta; el tiempo estaba en las herramientas

*(El ítem se escribió para poder caerse y se cayó, pero con la medida delante en
vez de por corazonada: la escala de trabajo acelera la **segmentación**, que con
herramientas dibujadas son ~1,6 ms de 31 — el 1,33× prometido se quedaba en
menos de un 2 % del frame.*

*Lo que sí se hizo es donde estaba el 83 %: **repartir las herramientas entre
hilos**. Y lo interesante es que no hubo que rediseñar nada para que fuera
seguro — `runTools` ya avanzaba por ONDAS, y una herramienta solo entra en una
onda cuando sus referencias se intentaron en ondas anteriores. El orden de
dependencia que ya existía por corrección resultó ser también el permiso para
repartir. Lo único que cambió es dónde se escribe: cada una en su hueco, y el
mapa de referencias después, en serie.*

*Medido sobre la misma pieza quieta, con 8 núcleos: 20 herramientas pasan de
**33,8 ms a 9,1** (3,72×), o sea de no caber en un frame a ocupar un cuarto.
Con 10, de 14,4 a 5,0.*

*Las pruebas, en este orden: que da **exactamente** las mismas cifras —incluido
el detalle, donde van los avisos—, que las referencias siguen resolviéndose en
veinte pasadas seguidas —una carrera de datos no falla a la primera—, y solo
después el cronómetro.)*

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
