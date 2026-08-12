# Cómo funciona por dentro

Recorrido técnico del programa, pieza por pieza: qué hace cada módulo, **cómo
se llama la técnica que usa**, por qué se eligió y dónde están sus límites.
Para el flujo de uso, ver el [README](README.md).

Índice:

1. [Mapa del código](#1-mapa-del-código)
2. [Captura de cámara](#2-captura-de-cámara)
3. [Detección de la pieza](#3-detección-de-la-pieza-visión-clásica)
4. [Sistema de coordenadas de la pieza](#4-sistema-de-coordenadas-de-la-pieza-position-fixture)
5. [Herramientas de medición](#5-herramientas-de-medición)
6. [Tablero de referencia y modos de medición](#6-tablero-de-referencia-y-modos-de-medición)
7. [Escala real: de píxeles a milímetros](#7-escala-real-de-píxeles-a-milímetros)
8. [Apariencia: el modelo y la detección de anomalías](#8-apariencia-el-modelo-y-la-detección-de-anomalías)
9. [Veredicto](#9-veredicto)
10. [Persistencia](#10-persistencia)
11. [Empaquetado](#11-empaquetado)
12. [Glosario: cómo se llama cada técnica](#12-glosario-cómo-se-llama-cada-técnica)
13. [Cómo mejorarlo](#13-cómo-mejorarlo)

---

## 1. Mapa del código

Diez módulos en capas. **La dependencia siempre baja**, nunca sube ni se cruza:

| Módulo | Qué contiene | De qué depende |
|---|---|---|
| `core/` | `Result<T>`, log, contador de FPS, blindaje anti-caída | nada |
| `domain/` | Veredicto, calidad de captura, calibración, modos de medición | `core` (**sin Qt ni OpenCV**) |
| `camera/` | Enumeración nativa, hilo de captura, controles de la fuente | `core`, Qt, OpenCV |
| `vision/` | Segmentación, contorno, fixture, estabilizador, tablero, escala, ajustes geométricos y periodicidad | `core`, `domain`, OpenCV |
| `ml/` | Extractor de embeddings ONNX, referencia estadística | `core` |
| `database/` | SQLite envuelto en RAII, esquema y migraciones | `core` |
| `inspection_editor/` | Geometrías, ejecutor de herramientas, perfiles, medición automática, lienzo | `vision`, `core`, Qt |
| `repositories/` | Puente entre dominio y base de datos | `database`, `domain`, `ml`, `vision` |
| `engine/` | Orquestador: registro e inspección completa | todos los anteriores |
| `ui/` | Ventana principal y diálogos | todo |

Dos reglas que sostienen el diseño:

- **`domain/` no conoce Qt ni OpenCV.** Ahí vive lo que decide (veredictos,
  tolerancias, criterios de calidad), y por eso se puede probar sin imagen ni
  ventana.
- **Nada lanza excepciones a través de la frontera de un módulo.** Todo devuelve
  `core::Result<T>`, que obliga al llamador a mirar el error. Las excepciones de
  OpenCV se atrapan donde ocurren.

---

## 2. Captura de cámara

**Enumerar sin abrir.** La lista de cámaras se pide a la API nativa del sistema
—DirectShow COM en Windows, V4L2 en Linux— que devuelve el **nombre real** del
dispositivo sin abrirlo. Esto no es un detalle estético: abrir una cámara
virtual que no está lista (por ejemplo AndroidCam antes de conectar el móvil)
hacía que su driver dividiera por cero y **matara el proceso entero** con una
excepción estructurada del sistema operativo que ningún `try/catch` de C++
atrapa.

**Apertura blindada.** El momento de abrir va envuelto en `core::runProtected`,
que instala un *Vectored Exception Handler* y usa `setjmp/longjmp` (GCC/MinGW no
soporta `__try/__except`) para sobrevivir a un fallo del driver y convertirlo en
un simple "no se pudo abrir". Si aun así el proceso muere, un manejador de
último recurso escribe el código de excepción y **la última operación en curso**
en `pc_inspector_crash.log`, para que un cierre silencioso quede diagnosticado.

**Hilo propio.** La captura corre en su propio hilo y entrega copias `QImage` a
la interfaz por señales encoladas; el búfer se fija en 1 frame para preferir
perder frames viejos antes que acumular retardo.

**Controles de la fuente.** Brillo, contraste, ganancia, exposición y enfoque se
sondean **escribiendo de verdad** en cada propiedad al abrir la cámara y
midiendo su rango al empujarla a los extremos. Mirar solo `get()` no vale: se
comprobó con una cámara real que devuelve brillo 91 mientras rechaza cambiarlo.
Los cambios se **agrupan por propiedad** antes de aplicarse, porque cada
`set()` cuesta milisegundos en el hilo de captura y arrastrar un deslizador
llegaba a atascar el vídeo.

**Resolución.** OpenCV tampoco sabe enumerar las resoluciones admitidas, así que
se sondean igual: se pide cada candidata de una lista estándar y se lee **la que
la cámara acabó dando** (los backends ajustan a la más cercana sin avisar), sin
repetidos y restaurando la original al terminar. Medido con una webcam real:
**~15 s para diez candidatas**, con el vídeo detenido mientras dura, porque cada
cambio reinicia el flujo de captura. Por eso el sondeo **no es automático**: lo
lanza el operador con un botón y **el resultado se cachea por cámara** en
`Settings`, de modo que se paga una sola vez.

Cambiar de resolución tiene efectos que hay que atender o se convierten en
fallos silenciosos:

- La **calibración px→mm deja de valer** (está sellada con la resolución) — ya
  existía el aviso, y ahora se dispara también aquí.
- La **zona de detección** y el **cero fijado del tablero** están en píxeles de
  imagen: se **reescalan proporcionalmente** (`vision::rescaleRect` /
  `rescalePoint`) para que sigan señalando el mismo punto de la escena. Las
  herramientas no necesitan nada: viven en coordenadas de pieza.
- El reajuste se dispara al detectar que **el frame recibido cambió de tamaño**,
  no al pedir el cambio: la cámara puede dar una resolución distinta de la
  solicitada, y así también se cubre que la cambie ella sola.

### El perfil de medición de la cámara, y por qué se prueba antes de creérselo

Una webcam viene ajustada para que una cara se vea bien: automático todo, que la
imagen se adapte sola. Para medir, «que se adapte sola» es exactamente el
defecto — significa que **el borde de la pieza se mueve cuando cambia la luz de
la nave**. De ahí un perfil de arranque para cámaras que el operador no ha
configurado nunca (`measurementDefaults`).

Lo interesante no es el perfil: es cuántas veces lo desmintió la cámara real.

**Intento 1 — «congelar cada control donde ya está».** Suena inmejorable:
repetibilidad sin cambiarle la imagen a nadie. Medido: la captura pasó de
**29,7 a 8,0 fps**. Con el automático puesto, `get(CAP_PROP_EXPOSURE)` devuelve
el nominal —el más largo del rango— mientras el sensor usa exposiciones cortas
de verdad. **El valor reportado bajo automático miente**, igual que miente el
del propio interruptor, que devuelve −1 pase lo que pase.

**Intento 2 — «apagar el automático y elegir la exposición midiendo».** Los fps
no bajan poco a poco con la exposición: son planos —30,2 a 30,3— desde −11 hasta
−5 y se desploman solo en el extremo largo (−4 da 16,0 y −3 da 8,0), porque el
tiempo de integración pasa a ser mayor que el periodo del frame. La regla que
sale de esa forma es **la exposición más larga que todavía da la velocidad
máxima**: toda la luz que no cuesta fps. Medido: escribir *solo*
`auto_exposure = 0`, sin elegir valor, dejó la cámara en **8,0 fps** — al quitar
el automático se cae a su manual, que era el más largo. De ahí una regla que
vale para toda esta capa: **no se apaga un automático que no se pueda
sustituir**.

**Intento 3 — el que quedó.** Con la exposición elegida por medida, la cámara
daba 30,0 fps... y **el 21 % del contraste**. En automático la cámara gobierna
también la GANANCIA, y en esta máquina `gain` sale no ajustable: ese refuerzo se
pierde y no hay con qué reponerlo. O sea que el perfil compraba repetibilidad a
cambio de una imagen mucho peor, por un 0 % de velocidad.

Así que el perfil **se prueba y se juzga** (`judgeProfile`): se mide qué da la
cámara en automático sobre esa escena, se aplica, se vuelve a medir, y si no se
lo ha ganado se vuelve al automático **diciendo por qué**. Perder contraste está
bien si a cambio hay velocidad de verdad —los 3,8× del caso lento sí lo valen, y
esa imagen más oscura se arregla con una lámpara—; por un 3 % no. Una estación
que mide repetible pero no ve la pieza no mide nada.

El experimento entero cuesta **3,2 s** y solo corre en cámaras sin configurar.
Se repite en cada arranque a propósito: si alguien añade luz, la respuesta
cambia sola y el perfil pasa a aceptarse.

La moraleja, que es lo que hay que llevarse: **de una cámara no se cree lo que
dice, se mide lo que hace**. Las tres versiones tenían tests verdes; las tres
veces fue la cámara real la que dijo que no.

### El aviso de «calibrado + automático encendido»

La combinación que produce números **creíbles y falsos**, que es la peor clase
de error que este programa puede dar. La escala px→mm se fijó con una
magnificación concreta: si el autofoco reenfoca, la magnificación cambia y
**todas las cotas cambian a la vez y proporcionalmente**, sin que nada en
pantalla lo delate. La exposición automática es más sutil pero del mismo tipo —
mueve el umbral aparente del borde, así que la misma pieza sale más gorda o más
fina según la luz.

Tres decisiones:

- **Solo con las dos condiciones juntas.** Sin calibrar, el autofoco es una
  comodidad legítima: las medidas van en píxeles y nadie ha prometido
  milímetros. Avisar ahí sería ruido, y un aviso que salta siempre es uno que se
  aprende a ignorar — con lo que tampoco serviría donde importa. El barrido
  recorre los cuatro cuadrantes y exige silencio en tres.
- **Va donde se ve la escala**, en la misma etiqueta que dice «Escala: 0,0421
  mm/px», no en una pestaña que el operador no va a abrir. Reutiliza el sitio y
  el estilo del aviso de calibración obsoleta, que existía por la misma razón.
- **Nombra cuál está encendido**, porque el daño no es el mismo ni lo que hay
  que hacer tampoco.

El estado de los automáticos **se lleva en la ventana, no se le pregunta a la
cámara**: `get(CAP_PROP_AUTO_EXPOSURE)` devuelve −1 pase lo que pase, y sobre
esa mentira ya se perdieron dos diseños del perfil de medición. Se actualiza en
los cuatro sitios donde puede cambiar: el sondeo inicial, el perfil, el veredicto
del barrido y un cambio del operador.

### Asistente de enfoque

`vision::sharpnessOf(imagen, roi)` es la varianza del Laplaciano de una región.
La métrica ya existía en `computeQualityMetrics`, pero solo se usaba para
validar capturas al registrar, y el deslizador de enfoque se movía a ciegas.

Tres decisiones:

- **Se mide sobre la pieza, no sobre el frame.** Medido en el test: con un fondo
  ruidoso y la pieza desenfocada, el encuadre completo da **90 970** y la pieza
  **155**. Quien mirase el número del frame estaría enfocando el fondo.
- **La barra es relativa al máximo visto.** La varianza del Laplaciano no tiene
  tope: un valor absoluto no dice nada, y lo único accionable es "¿sube o baja?"
  — que es exactamente cómo se enfoca.
- **No se tocó `computeQualityMetrics`.** Su umbral de aceptación
  (`QualityCriteria::minSharpness = 40`) está ajustado contra el número del
  frame completo; moverle la medida debajo habría cambiado en silencio qué
  capturas se aceptan al registrar.

Un hallazgo del test que conviene no olvidar: la nitidez **no es monótona hasta
el final**. Medido sobre la misma imagen desenfocada progresivamente: 7444 →
1372 → 765 → 98 → 2,5 → **6,5**. Por debajo del 0,1 % del pico ya no queda
detalle que perder y lo que se mide es residuo numérico. Por eso la prueba exige
monotonía solo mientras la medida signifique algo, y por separado que el
desenfoque fuerte deje el valor cien veces por debajo. Afirmar monotonía en la
cola sería afirmar sobre ruido.

### Contar piezas

`analyzeFrames` devuelve **todas** las piezas de la imagen ordenadas de mayor a
menor; `analyzeFrame` sigue devolviendo la mayor y **no cambia de contrato**.

Van por separado, y no una encima de la otra, por coste: el camino de una sola
pieza es el que corre en cada frame del vídeo, y hacerle analizar también las
manchas que pasan el filtro de área sería pagar de más en el sitio más caliente.
Contar solo se activa cuando alguien va a mirar el número — la pieza declara que
espera más de una, o el panel Configurar está abierto — y entonces el análisis
se hace **una sola vez**: la mayor de `analyzeFrames` es exactamente la que
habría dado `analyzeFrame`.

Cada pieza se procesa **dentro de su propia envolvente**, no a tamaño de frame.
Sin eso, seis piezas costarían seis análisis completos y se perdería lo ganado
en C4b. Medido: seis piezas cuestan **1,62×** lo que cuesta una, no 6×.

El recuento es **una inspección por sí misma**: `domain::evaluatePieceCount`
compara lo esperado con lo encontrado y `combineVerdict` lo suma al veredicto,
con los dos números en el motivo ("se esperaban 6 piezas y se ven 5") — decir
solo "faltan piezas" obligaría a ir a contarlas a mano, que es justo el trabajo
que esto ahorra. Con `expected <= 0` no se juzga nada: la regla de siempre, un
aviso que salta siempre acaba ignorándose.

El número vive **en la pieza** (`Pieces.expected_pieces`, migración v9) y no en
los ajustes de la máquina, porque "seis tornillos en bandeja" es una propiedad
del trabajo. Al cambiar de pieza se recupera el suyo y no se arrastra el
anterior.

**Medir las N piezas sale casi gratis**, y esa es la recompensa de haber puesto
las herramientas en coordenadas de pieza desde la fase 2: medir la segunda barra
de la bandeja es `runTools` con **otro fixture**, sin tocar ni una herramienta.
Lo que hubo que decidir es todo lo demás:

- **La pieza 0 sigue siendo la de siempre.** Es `outcome.analysis`, con su rasgo
  distintivo y su giro ya aplicados; las demás salen de `analyzeFrames`, que ya
  se llamó para contar. Así, inspeccionar de una en una da exactamente el mismo
  resultado que antes, y no se analiza dos veces.
- **El veredicto es el de la peor pieza**, y el nombre de cada medida dice de
  cuál es ("ancho (pieza 3)"). Un NG que dijera solo "ancho fuera de tolerancia"
  obligaría a ir barra por barra, que es el trabajo que esto ahorra.
- **`ToolRunResult` lleva su `pieceIndex`.** Viaja en el propio resultado y no
  en una lista paralela, así la cadena entera —motor, historial, lienzo— sabe a
  quién pertenece cada medida sin llevar la cuenta aparte. El historial lo
  guarda en `ToolResults.piece_index` (migración v10); `0` es la pieza
  principal, así que lo ya guardado se lee igual.
- **En el vídeo se pintan las marcas de todas y los números de una.** Seis
  piezas por cinco herramientas son treinta etiquetas encima de la imagen: eso
  no se lee, se tapa la pieza.

Limitación anotada: el **rasgo distintivo** solo se aplica a la pieza principal.
Es un punto concreto de *una* pieza y no significa nada en las demás; con
`autoOrient` activo, las piezas 2..N usan su propio eje principal.

### La zona de trabajo automática

`vision/auto_roi.*` decide en qué rectángulo buscar la pieza en el próximo
frame. **No hizo falta mecanismo nuevo**: `PipelineConfig::roi` ya recortaba y
`analyzeFrame` ya devolvía las coordenadas en el marco completo; lo que faltaba
era quién calcula ese rectángulo y lo mueve con la pieza.

Lo primero que hubo que demostrar es que **recortar no cambia el resultado**: si
el fixture saliera distinto, todas las herramientas se desplazarían. El test
recorre una secuencia de doce frames con la pieza moviéndose y compara el
fixture del recorte con el del frame completo — coinciden dentro de **±0,5 px**,
y el recorte contiene a la pieza en todos ellos.

La ganancia, medida en el mismo proceso: sobre 1280×720 con una pieza de
180×140, el recorte ocupa el **7,9 %** del área y el análisis pasa de ~13 ms a
~2 ms, **6×**. El test exige solo 2× porque el margen sobra y afinar el umbral
lo convertiría en un generador de fallos intermitentes; y compara **relativo**,
nunca milisegundos absolutos, que dependen de la máquina.

Cuatro decisiones de diseño:

- **Ante la duda, la imagen entera.** El seguimiento se rinde si pierde la
  pieza varios frames, si la pieza toca el borde del recorte o si su área salta
  de golpe. Volver cuesta un frame; medir dentro de un rectángulo equivocado no
  se nota, y esa es exactamente la forma de fallar que hay que evitar.
- **El orden de las comprobaciones importa.** "Se está saliendo" se mira antes
  que "saltó el área", porque una pieza que se sale acaba recortada y al
  recortarse su área cae: salirse es la causa y el salto el síntoma. Al revés,
  el motivo que se le enseñaría al operador sería el equivocado.
- **Crecer es inmediato, encoger es lento.** El recorte se une siempre con el
  objetivo (así nunca corta a la pieza) y solo se encoge interpolando. Sin eso,
  el rectángulo late al ritmo del ruido de la segmentación.
- **No recorta cuando no hay nada que ganar**: ni piezas diminutas (el ahorro no
  compensa el riesgo) ni piezas que ocupan casi todo el frame.

La zona automática **no pisa la zona manual** del operador: `pipelineConfig_.roi`
sigue guardando la que él dibujó y el modo (`Off` / `Automatic` / `Fixed`)
decide cuál se usa. Y la zona activa se dibuja sobre el vídeo, porque un recorte
invisible convierte cualquier fallo en un misterio.

**Tres fallos que costó eso**, y que se corrigieron después de que el operador
reportara que «la zona de detección no funciona»:

- **Dibujar una zona no la usaba.** El modo lo decidía todo y arranca en «imagen
  entera», así que al arrastrar el recuadro este se guardaba, el botón pasaba a
  decir «Quitar zona», la barra de estado decía «zona activa»… y no se pintaba
  ni se usaba. Solo funcionaba si además se iba a *Configurar ▸ Rendimiento* a
  marcar el modo a mano. Meter el modo en medio desacopló el gesto de su efecto,
  y tres sitios de la interfaz afirmaban lo contrario de lo que hacía el
  programa. Ahora **dibujar una zona es usarla** y quitarla apaga el modo que la
  usaba; la regla vive en `vision::modeAfterFixedZoneChanged` —una función pura,
  probada— y no escrita a mano en la ventana.
- **La zona automática se movía sin verse.** Su rectángulo solo se repintaba al
  cambiar de modo, así que el operador veía uno quieto, o ninguno si no había
  tocado el modo desde que arrancó. Se repinta con cada frame analizado.
- **Con el contorno oculto se daba la pieza por perdida.** Con la pose congelada
  no se segmenta nada, así que no hay contorno; eso se le pasaba al seguimiento
  como «no hay pieza» y a los dos frames se rendía con un «se dejó de ver la
  pieza» que era falso — la pieza estaba ahí, lo apagado era el dibujo. Ahora
  `AnalysisOverlay::analysed` distingue «se buscó y no había» de «no se buscó»,
  y solo lo primero alimenta al seguimiento. Dar algo por perdido sin haberlo
  mirado es afirmar lo que no se ha medido.

### Dónde se va el tiempo de un análisis (y por qué no hay escala adaptativa)

Se planificó una «escala de trabajo adaptativa»: segmentar una copia reducida
de la imagen cuando la pieza es grande. Se implementó, se midió y **se retiró**,
porque la premisa era falsa. Conviene dejar escrito el reparto real del coste
para no volver a intentarlo.

Medido sobre 2560×1440 con una pieza que ocupa casi todo el frame:

| Etapa | Tiempo | % |
|---|---|---|
| `analyzeFrame` completo | 35,4 ms | — |
| `segmentPiece` (suavizado + Otsu + morfología) | 8,3 ms | 23 % |
| `findLargestContour` | 2,0 ms | 6 % |
| `computeFixture` (momentos sobre la máscara) | **14,1 ms** | **40 %** |
| `normalizePiece` (recorte canónico) | **12,1 ms** | **34 %** |

Segmentar a 1/4 baja esos 8,3 ms a 2,1 ms contando el remuestreo de ida y
vuelta: **1,10× medido** sobre el total. Un 10 % no justifica un modo nuevo en
la interfaz ni una forma más de equivocarse.

Lo que sí quedó demostrado, y sirve si algún día hace falta: reducir **no mueve
las medidas** si el contorno se recupera a resolución completa. Segmentando a
1/4, subiendo la máscara con interpolación bilineal y volviendo a umbralizar, el
fixture salía a **±0,000 px** del de referencia y las cotas idénticas. Lo que no
vale es escalar los puntos del contorno, que son enteros: eso los cuantizaría al
factor.

El coste real estaba en **los momentos sobre la máscara completa** y en **el
recorte canónico**. Los dos se arreglaron:

- **`computeFixture` recorría la máscara dos veces** (tres con `autoOrient`),
  porque el centroide, la anisotropía y el ángulo se pedían por separado y cada
  uno llamaba a `cv::moments`. Ahora los momentos se calculan **una vez** y se
  comparten, y **sobre la envolvente de la pieza** en vez de sobre la máscara
  entera. Es exactamente equivalente: los momentos centrales son invariantes a
  la traslación, así que el ángulo y la anisotropía no cambian, y al centroide
  solo hay que sumarle la esquina del recorte.
- **`normalizePiece` hacía dos `warpAffine` de imagen completa incluso sin
  giro**, que es el caso **por defecto** (`autoOrient` es false: la pieza se
  deja vertical). Una rotación identidad aplicada a 3,7 millones de píxeles, dos
  veces. Ahora, sin giro, se recorta la envolvente y ya está. El camino con giro
  sigue igual.

Ambas son optimizaciones de código que ya funcionaba, así que la prueba que vale
no es "da un número razonable" sino **"da exactamente lo de antes"**: los tests
llevan una implementación de referencia con el código original y comparan contra
ella. El recorte canónico sale con **0 píxeles distintos**.

Resultado sobre la misma escena:

| Etapa | Antes | Ahora |
|---|---|---|
| `analyzeFrame` completo | 35,4 ms | **23,1 ms** (1,53×) |
| `computeFixture` | 14,1 ms | 5,2 ms |
| `normalizePiece` | 12,1 ms | 5,7 ms |
| `segmentPiece` | 8,3 ms | 7,8 ms |

Con eso, **la segmentación pasa a ser el mayor coste** (34 % de los 23 ms). Si
alguna vez hiciera falta más, ahí sí volvería a tener sentido mirar la escala de
trabajo: sobre el total de hoy valdría ~1,33× en vez del 1,10× que se midió
antes. Sigue sin justificar un modo nuevo en la interfaz.

### Una sola regla para el tablero, y una trampa de la fila entera

El tablero de referencia estaba **duplicado**: claves globales `board_*` en
`Settings` y columnas en `Pieces`. Al seleccionar pieza ganaba la pieza, pero
tocarlo desde el menú *Ver* escribía el global. La regla, ahora escrita y
cumplida: **el ajuste global es solo la plantilla para piezas nuevas**. Con una
pieza seleccionada, todo cambio va a sus columnas y **no se toca el global** —
pisarlo haría que la siguiente pieza naciera con el tablero de la anterior.

Y una trampa que costó un fallo real: **`saveMeasurement` escribe la fila
entera**. `persistBoardConfig` construía un `PieceMeasurement` nuevo para
cambiar solo el tablero, así que ponía a su valor por defecto todo lo que esa
función no tocaba — y cambiar el origen del tablero **borraba en silencio las
piezas esperadas** de la pieza, añadidas en C5. Ahora se lee, se modifica y se
escribe, y hay un test que lo fija. La regla general para esa fila: **cargar
antes de guardar, siempre**.

### El editor mide con lo que el operador puso

Fallo real encontrado en el inventario: `EditorWindow` llamaba a
`vision::analyzeFrame(image)` **sin `PipelineConfig`** en sus tres puntos de
análisis. El editor detectaba con Otsu, polaridad automática y sin zona, diera
igual lo que el operador tuviera configurado — así que **lo que se dibujaba
encima y lo que luego se inspeccionaba podían no ser la misma pieza**.

La configuración se pasa ahora en el constructor. El test lo fija con una escena
donde el ajuste **decide**: fondo oscuro, una pieza grande gris medio y una
pequeña muy clara. Con Otsu gana la grande (420×320); con umbral manual 180 gana
la clara (238×198). Si el editor volviera a ignorar la configuración, el test lo
diría.

Se intentó primero con la **polaridad** sobre una escena de tres niveles y se
descartó: con un histograma trimodal, dónde cae Otsu no es predecible y el test
medía la suerte en vez de la configuración.

### El panel «Configurar»: un solo sitio

`ui/configure_dialog.*` es un `QTabWidget` que aloja las páginas de ajuste.
Nació de un problema medido: los ajustes vivían en **siete diálogos colgados de
cuatro menús**, y para cambiar el enfoque y el umbral había que saber que uno
estaba en *Cámara* y el otro en *Inspección*.

Tres decisiones lo definen:

- **No es modal.** Ajustar un umbral o un enfoque consiste en mover y mirar: con
  un diálogo modal encima del vídeo no se ve el efecto de lo que se toca. Por
  eso se abre sin bloquear y trae *Aplicar* además de *Aceptar*. Solo puede
  haber uno abierto; volver a pulsar lo trae al frente en vez de apilar paneles.
- **El panel no aplica nada; avisa.** Emite `applied()` y la ventana lee las
  páginas. La excepción es la de cámara, que **aplica al instante** porque un
  deslizador de enfoque con efecto diferido sería inservible. Esa asimetría es
  deliberada y está escrita en la propia página.
- **Dos pestañas abren un asistente en vez de fingir ser un formulario.** La
  escala se calibra haciendo clic en dos puntos de una foto y los atajos son una
  tabla que se edita: meterlos a la fuerza aquí los haría peores, no mejores.

Los diálogos que eran formularios puros (`DetectionDialog`, `PreferencesDialog`,
`CameraControlsDialog`) pasaron a ser páginas (`DetectionPage`,
`PreferencesPage`, `CameraImagePage`): mismo cuerpo, sin barra de botones ni
título de ventana. No se dejaron cáscaras `QDialog` porque `MainWindow` era el
único que las abría y habrían quedado como código muerto.

Al parar la cámara el panel **se cierra**: sus deslizadores de cámara no harían
nada y quedarse abierto sería mentir. La pestaña que estaba visible se guarda
(`config_last_tab`) porque quien pelea con la iluminación vuelve diez veces a la
misma.

---

## 3. Detección de la pieza (visión clásica)

Aquí **no hay IA**. Es una cadena de visión clásica, elegida porque es rápida,
predecible y no necesita entrenar nada:

1. **Suavizado** gaussiano opcional para quitar ruido de sensor.
2. **Umbralización de Otsu** (umbral automático global) o umbral manual fijo. La
   polaridad —pieza oscura sobre fondo claro o al revés— se decide sola o se
   fuerza.
3. **Morfología matemática** (apertura y cierre) para cerrar huecos y borrar
   motas.
4. **Extracción de contornos** por seguimiento de bordes (`findContours`), y se
   toma el contorno de mayor área dentro de unas fracciones mínima y máxima del
   encuadre: así una mota no pasa por pieza, ni una sombra que ocupa todo.
5. **Momentos de imagen** para el centroide y el eje principal, y `minAreaRect`
   para el rectángulo mínimo que la contiene.

**Zona de detección (ROI)**: todo lo anterior puede restringirse a un rectángulo;
los resultados se devuelven en coordenadas de la imagen completa.

**Perfiles de detección**: el juego de ajustes (umbral, polaridad, kernels) se
guarda con un nombre ("luz brillante", "contraluz") y se asigna **a cada pieza**,
porque la misma línea puede tener piezas que necesitan iluminaciones distintas.

Límite real, medido: con un **degradado de sombra** fuerte, el Otsu global falla
—el "contorno" se come medio fondo— y hay que pasar a umbral manual. Está fijado
en un test para que no se olvide.

---

## 4. Sistema de coordenadas de la pieza (Position Fixture)

El concepto central. Cada pieza detectada define su propio sistema de
coordenadas: **origen en su centro y eje X sobre su eje principal**. En la
industria esto se llama *reference frame*, *datum* o *fixturing* (es el
vocabulario de Cognex VisionPro y MVTec Halcon).

Las herramientas **se guardan en coordenadas de pieza**, no de imagen. Por eso,
si la pieza llega desplazada o girada, las herramientas la siguen sin tocar
nada. Está verificado por tests: la misma barra medida con la pieza a 0°, 20°,
45°, 70° y 120° da el mismo número.

**Estabilizador temporal.** El fixture medido en cada frame se compara con el
mostrado en el anterior:

- Movimiento por debajo de la banda muerta → **la vista se clava** (el ruido de
  medición no mueve nada).
- Movimiento moderado → media exponencial, para seguir sin vibrar.
- Movimiento grande → salto inmediato, para no arrastrarse por detrás.
- **Piezas casi redondas**: por debajo de un umbral de anisotropía el eje
  principal es puro ruido, así que **el ángulo se congela**.
- **Giros espurios de 180°**: el signo del momento de tercer orden es inestable
  en piezas simétricas. Si el ángulo salta media vuelta, se conserva el sentido
  anterior y se avisa al llamador para que rehaga el recorte normalizado.

**Rasgo distintivo.** Para piezas realmente simétricas, el operador marca un
punto visualmente único (un agujero, una marca). Ese rasgo es la verdad: fija la
orientación en cualquier rotación, incluida la de 180° que los momentos no
distinguen.

---

## 5. Herramientas de medición

**32 herramientas**, todas ancladas al fixture. El motor común
(`tool_executor.cpp`) recibe imagen + fixture + configuración y devuelve un
resultado con la medida, el veredicto y los puntos para dibujarla.

La tabla va **por familias**, en el mismo orden que `allToolCategories()`, para
que se lea igual aquí que en la paleta.

| Herramienta | Qué mide | Técnica |
|---|---|---|
| **— Figuras básicas —** | | |
| Borde liso | Desviación máxima de un borde que debería ser recto | Ajuste de recta + máxima distancia de los puntos detectados |
| Blob | Conteo de manchas en un rectángulo | Umbral por polaridad + contornos externos filtrados por área mínima |
| Blob poligonal | Igual, en una región de forma libre | Máscara poligonal + los mismos contornos externos |
| Región | Área, perímetro, solidez, circularidad, relación de aspecto o agujeros | Contorno mayor dentro del recuadro + momentos y casco convexo; el perímetro por cadena **Vossepoel–Smeulders**, no `arcLength` |
| Simetría | Grado de simetría de la silueta, de 0 a 1 | Busca el **mejor** eje de simetría y compara la silueta con su reflejo. No es la simetría de GD&T, que se retiró de la norma en 2018 |
| Lados | Cuenta los lados de un perfil poligonal y mide cada uno y sus ángulos | Simplificación del contorno con **epsilon en milésimas del perímetro** (no en px, para que el recuento no cambie al acercar la cámara). Si el recuento no aguanta al doblar y partir ese valor, la figura no es un polígono claro y lo dice |
| Rebabas y mellas | Defectos de borde, con su tamaño y dónde están | Barrido perpendicular al contorno + rachas contiguas fuera de banda (una rebaba más ancha que la ventana también cuenta) |
| **— Medición en línea —** | | |
| Caliper | Distancia entre dos bordes | Perfil de intensidad promediado en banda + gradiente + refinamiento subpíxel parabólico; empareja bordes de **polaridad opuesta** |
| Círculo | Diámetro y redondez | Rayos radiales buscando el borde + ajuste de círculo por mínimos cuadrados |
| Punto-Línea | Distancia perpendicular de un borde a una recta | Escaneo perpendicular + proyección sobre la recta |
| Regla | Distancia directa entre dos puntos | Geometría pura (no busca bordes) |
| Línea-Línea | Ángulo entre dos rectas | Producto escalar de direcciones |
| Ángulo | Ángulo interior de una esquina | Ángulo entre dos vectores desde el vértice |
| Arco | Radio de una esquina o un redondeo | Círculo por tres puntos para situar el sector + barrido radial acotado a él + ajuste Taubin robusto |
| Holgura | La separación **más corta** entre dos figuras, y dónde está | Las dos figuras mayores del recuadro + distancia mínima entre contornos. Si solo se ve una, puede que se estén tocando, y se dice: cuánto solapan no está en la silueta |
| **— Construcciones —** (no miden: fabrican referencias) | | |
| Punto construido | Una intersección, un punto medio o un centro | Geometría sobre elementos que dan otras herramientas (`DerivedElements`) |
| Recta construida | Una bisectriz, una perpendicular o una paralela | Igual; la dirección se canonicaliza para que no dependa de cómo se trazó |
| Eje medio | La línea que va por el centro de una pieza alargada, y su rectitud | Punto medio entre los dos bordes **reales** corte a corte, no la línea que trazó el operador; avisa de la desalineación entre tramos |
| **— GD&T —** (siempre contra un marco de referencia declarado) | | |
| Posición | Desviación de un rasgo respecto al cero del tablero (radial, X o Y) o **posición verdadera** contra un marco de referencia | Lectura en el sistema del tablero; con datums declarados, el diámetro de la zona `2·√(dx²+dy²)` en ese marco, que es como lo acota el plano |
| Rectitud | Rectitud por **zona mínima** | `minimumZoneBand`: casco convexo + calípers giratorios (la banda más estrecha, no la de área mínima) |
| Redondez | Redondez por **zona mínima** | `minimumZoneCircle`: Nelder-Mead sembrado con Taubin (MZC, no mínimos cuadrados) |
| Orientación | Paralelismo/perpendicularidad/angularidad contra un datum | Ancho de banda, que es una **distancia** y no un ángulo — como lo define la norma |
| Desviación de centros | Cuánto se desplazan dos centros entre sí | Distancia entre los dos centros ajustados. **No es concentricidad**, y por eso no se llama así |
| Patrón de agujeros | Si un patrón de agujeros está donde toca | Ajuste del patrón nominal + el peor agujero identificado **por su posición angular**, no por un índice |
| Perfil de línea | Desviación de un borde respecto a un perfil nominal | Perfil nominal capturado del contorno de la pieza patrón, en coordenadas de pieza; comparación punto a punto **sin ICP** (el fixture ya alinea) |
| **— Máx./mín. y torneadas —** | | |
| Eje / Diámetro | Diámetro, conicidad y rectitud de una pieza de torno | Perfil axial a los dos lados + ajuste robusto de recta a cada borde; el diámetro es la separación entre las dos rectas |
| Rosca | Paso, Ø exterior, Ø de fondo y ángulo de flanco (con el sesgo de hélice cuantificado) | Perfil axial a los dos lados + periodo por autocorrelación (el paso) + plegado síncrono por ese periodo y ajuste de recta a los flancos |
| Engranaje | Dientes, Ø de cabeza y raíz, módulo, Ø primitivo, excentricidad | Perfil radial + periodo circular (los dientes) + ajuste de círculo a las puntas (la excentricidad) |
| Máx./mín. | Anchura mínima y diámetro máximo, **en cualquier dirección** | Casco convexo + calípers giratorios; las dos se dan siempre con su dirección |
| Chaflán | Ángulo del bisel y sus **dos catetos** | `decomposeContour` + ajuste de tres rectas e intersección para construir la **esquina virtual**, que es de donde acota el plano |
| Radio de acuerdo | Radio del redondeo **y si empalma tangente** | Arco de `decomposeContour` + ángulo entre su tangente en cada extremo y la recta vecina |
| Ranura | Ancho, profundidad y Ø de fondo de una entalla | El mismo perfil axial que el Eje pero **sin ajustar recta**: la ranura es justo donde el borde se sale de esa recta. Mínimo local del perfil crudo + cruce con el nivel de media profundidad en cada flanco. El ancho sale de **contar cortes**, así que se rechaza si la ranura no abarca al menos tres |

### Por qué zona mínima y no mínimos cuadrados en GD&T

Rectitud y Redondez podrían salir de un ajuste por mínimos cuadrados, que es lo
que ya hace el Círculo, y sería más corto de escribir. No se hace, y la razón no
es estética: **la norma define otra cosa**. Una tolerancia de forma es el ancho
de la **zona más estrecha que contiene todos los puntos**, no la dispersión
alrededor del elemento mejor ajustado. Mínimos cuadrados minimiza la suma de
cuadrados; la zona mínima minimiza el máximo. Son problemas distintos y dan
números distintos, y el de mínimos cuadrados sale **siempre igual o mayor** —
o sea que rechazaría piezas buenas.

Con la misma lógica se descartó `cv::minAreaRect` para la rectitud: minimiza el
**área** del rectángulo, no su **ancho**. Sobre un triángulo el rectángulo de
área mínima sale un 41 % más ancho que la banda de ancho mínimo, y sobre un
equilátero su diagonal se pasa un 32 % del diámetro real. Es una función que
existe y que casi encaja, que es exactamente el tipo de atajo que hay que
escribir por qué no se tomó.

De ahí `minimumZoneBand` (casco convexo + calípers giratorios) y
`minimumZoneCircle` (Nelder-Mead sembrado con Taubin) en `vision/fitting.*`. El
invariante que los guarda es barato y fuerte: **la zona mínima nunca puede dar
más que su equivalente por mínimos cuadrados**, porque la banda alrededor de la
recta de mínimos cuadrados es una candidata más entre todas las orientaciones y
la zona mínima es el mínimo sobre todas. Si un día diera más, hay un error de
implementación, y el barrido lo dice.

El barrido comprueba además que **no coinciden**: sobre 200 nubes de puntos la
zona mínima gana estrictamente en las 200, y sobre 100 perfiles circulares la
MZC gana en los 100. Sin esa segunda mitad, un test que solo exigiera «≤»
pasaría igual de verde con las dos funciones devolviendo lo mismo, que es
exactamente el error que buscaría.

### Quién decide qué herramientas llevan referencia

`referenceOperandsOf(const ToolGeometry&)`, en el modelo, y solo ahí. Antes lo
decidía el panel del editor por su cuenta, y la pregunta que se hacía —«¿es una
construcción?»— dejaba fuera a **Posición, Orientación y Desviación de
centros**: medían contra una referencia que desde el editor no había forma de
asignarles, así que solo se podían configurar editando la plantilla a mano.

Lo encontró el barrido de coherencia del cierre, y la forma de encontrarlo es
la parte reutilizable: recorrer **todas** las herramientas, ejecutarlas sin
referencias y exigir que **toda la que se queje de que le falta un datum lo
declare**. Dos sitios que tienen que estar de acuerdo —el ejecutor que se niega
a medir y el panel que ofrece los desplegables— no se mantienen sincronizados
por buena voluntad: o comparten la fuente o divergen.

`OperandKind::PointOrLine` existe por el datum secundario de Posición, que
admite las dos cosas: una recta corta a la primaria y un punto se proyecta
sobre ella, y las dos maneras fijan el origen. Etiquetarlo «recta» habría sido
una mentira pequeña en el sitio donde el operador mira para decidir.

**Las de forma no llevan datum, y eso también hay que decirlo.** Rectitud,
Redondez, Patrón de agujeros y Perfil de línea no toman referencia, y sus
descripciones lo dicen en voz alta desde este repaso. Quien viene de un plano
asocia GD&T con declarar datums: sin la frase, se queda buscando un desplegable
que no existe y lee el hueco como un fallo del programa.

### Referencias entre herramientas

`tools/derived_element.h` y las dos pasadas de `runTools` son el mecanismo del
que cuelgan las construcciones geométricas y todo el GD&T. Existe porque
paralelismo, perpendicularidad, angularidad y posición verdadera **no son
medidas absolutas**: son medidas respecto a una referencia declarada.

- Una herramienta puede producir, además de su medida, un **elemento derivado**
  (punto, recta o círculo) en **coordenadas de pieza**, para que la referencia
  siga a la pieza igual que quien la usa. Las que ya existían lo llenan casi
  gratis: la Regla ofrece su recta, el Círculo su circunferencia **ajustada**
  (no la trazada: el centro que vale para un datum es el que sale del borde
  real) y Posición su punto.
- `ToolConfig::reference` es el **nombre** de otra herramienta, no su id: el
  operador referencia lo que ve escrito en la lista, y una plantilla exportada
  e importada en otra pieza cambia de ids pero conserva los nombres. Se
  persiste dentro de `paramsJson` — esa columna existía sin usarse y es el sitio
  previsto para parámetros por herramienta, así que **no hizo falta migrar**.
- `runTools` ejecuta en **orden de dependencia** pero devuelve los resultados
  **en el orden de la lista del operador**. Si se reordenaran, la tabla de
  resultados bailaría cada vez que alguien añade una referencia.
- **Una referencia que no está no se sustituye por nada.** Si no existe, está
  desactivada o falló al medir, la herramienta **no mide** y lo dice. Nunca cae
  a una referencia implícita: un GD&T medido contra otro datum del que cree el
  operador es exactamente el fallo que este programa existe para evitar, porque
  el número sale creíble y es falso.
- Dos herramientas que se referencian en círculo **fallan diciéndolo** en vez
  de colgar el análisis.

**Ninguna herramienta muda.** Las cinco cadenas de `if constexpr` sobre la
variante terminan ahora en `static_assert(alwaysFalse<T>)`, con un mensaje que
dice qué se rompería: sin `referencePoints` el marco de selección no encuadra;
sin `handlePoints` no se puede editar; sin `setHandlePoint` las manijas se
dibujan pero no se mueven; sin `distanceToGeometry` no se puede seleccionar; sin
`paintTool` la herramienta es invisible.

**El inventario contaba de más y se comprobó.** Se añadió un tipo sonda a la
variante y se miró qué dejaba de compilar. Resultado medido: `referencePoints`,
`handlePoints` y `setHandlePoint` **ya** daban error (su `else` final usaba
miembros que el tipo nuevo no tiene). Las trampas silenciosas eran **dos**, no
cuatro: `distanceToGeometry`, cuya cadena no tenía `else` y dejaba la distancia
en 1e9, y `paintTool`, que tampoco lo tenía.

Y el experimento destapó un fallo ya entregado: **Eje, Rosca y Engranaje no
tenían rama en `paintTool`** desde T2–T4. Se veían solo cuando ya habían medido
—`paintResults` sí dibuja sus puntos— y antes de eso eran invisibles en el
lienzo. Nada lo dijo en su momento. Ya tienen su dibujo: el eje con su banda de
búsqueda a rayas, y el engranaje con los dos aros entre los que busca los
dientes.

Además del `static_assert`, que solo garantiza que la rama **exista**, un
barrido renderiza cada herramienta sobre un lienzo vacío y exige que **pinte
algo**. El compilador no puede comprobar eso.

**La paleta** (`canvas/tool_palette.*`) construye los botones desde
`toolsInCategory()` y la comparten las dos superficies, así que el orden, los
iconos y las descripciones son los mismos en las dos. Tiene dos formas porque
los dos sitios tienen huecos distintos: **compacta** (un botón por familia con
menú desplegable) para la barra de la vista en vivo, donde falta ancho, y
**acordeón** para la columna del editor, donde falta alto.

La medida que lo justifica: la fila plana pedía **~1400 px** de ancho mínimo en
una ventana que arranca a 1100. La paleta compacta pide **312 px**. Hay un test
que lo fija y otro que exige que **toda** herramienta siga siendo alcanzable a
clics — agrupar no puede esconder nada.

Los **atajos** pasan a ser *familia + dígito* (`Ctrl+1..5` elige familia, `1..9`
la herramienta dentro) y se generan de las propias familias. La tabla escrita a
mano que había se quedó corta: con catorce herramientas y diez dígitos, Arco,
Eje, Rosca y Engranaje **no tenían tecla**.

**Familias de herramientas** (`ToolCategory`). Cinco: *Figuras básicas*,
*Medición en línea*, *Construcciones*, *GD&T* y *Máximos, mínimos y torneadas*.
Son un **dato**, no el orden en que se pintan los botones, y viven junto a
`allToolTypes()` por la misma razón: con la agrupación escrita en cada
superficie de interfaz, la fila de la vista en vivo y la columna del editor
acabarían agrupando distinto.

Aquí había un recuento herramienta por herramienta —«(3), (7), (2), (1),
(3)»— que quedó obsoleto en cuanto entraron las rondas siguientes y llegó a
estar a la mitad del número real. Se ha quitado a propósito: **el recuento vive
en `toolsInCategory()`**, y un número escrito a mano en un documento no tiene
quién lo desmienta. Lo que sí se escribe aquí es la regla, porque la regla no
caduca.

Un barrido exige que las familias sean una **partición**: cada herramienta en
una y solo una, y las cinco juntas reconstruyen exactamente `allToolTypes()`.
Una herramienta que faltara quedaría escondida en la paleta; una repetida
aparecería dos veces.

*Construcciones* nació vacía **a propósito**, con un test que exigía que lo
estuviera para que el hueco no se olvidara. `X1` la llenó, ese test saltó —que es
para lo que estaba— y la regla es ahora la contraria: **ninguna familia puede
estar vacía**, porque un cajón que se abre para nada es peor que no tenerlo.

### Construcciones geométricas (`X1`)

**Punto construido** y **Recta construida**. No miden: calculan un elemento a
partir de los que ofrecen otras herramientas, para que exista un **datum** que
declarar. Sin eso, paralelismo, perpendicularidad, angularidad y posición
verdadera no se pueden dar: no son medidas absolutas, son medidas *respecto a
algo*, y una herramienta que dijera "paralelismo = 0,08" sin decir *paralelo a
qué* estaría inventándose un número con nombre de norma.

Ocho construcciones, todas trigonometría sobre primitivas que ya se ajustan
—cero algoritmo nuevo—: punto medio, intersección de dos rectas, proyección de
un punto sobre una recta y centro de un círculo; recta por dos puntos,
bisectriz, paralela y perpendicular por un punto.

Decisiones que se tomaron ahí:

- **«Bisectriz» y «recta media» son UNA construcción, no dos.** Cuando las dos
  rectas se cortan, la bisectriz pasa por el corte; cuando son paralelas, pasa
  por el punto medio entre ellas — que es exactamente la recta media. No es un
  caso especial esquivado: es el mismo resultado por continuidad.
- **Las direcciones se llevan a una forma canónica antes de bisecar.** Un vector
  de dirección tiene sentido y depende de hacia dónde arrastró el operador; la
  recta que representa, no. Sin canonizar, la bisectriz de dos rectas
  **perpendiculares** salía a 45° o a 135° según el sentido del trazo. Las dos
  son igual de válidas —con 90° entre las rectas no hay ángulo agudo que
  partir— pero que cambie sola no lo es, porque el datum giraría 90° sin que
  nadie tocara nada. Lo destapó un test que afirmaba invariancia y falló.
- **Un círculo vale donde se pide un punto**: aporta su centro, que es el datum
  natural de un agujero. Exigir un "punto" literal sería una limitación
  inventada.
- **Un resultado informativo** (`ToolRunResult::informative`). Una construcción
  que sale bien **no es un OK**: no ha juzgado nada, así que la tabla escribe
  «—» y no un verde que no significaría nada. Que **falle** sí es NG, porque
  deja sin referencia a todo lo que la usaba.
- **Nada de NaN.** Rectas paralelas que no se cortan y dos puntos coincidentes
  que no definen recta fallan **con motivo escrito**. Un NaN es un número con
  toda la pinta de ser una medida.
- **El ancla no entra en ningún cálculo.** Es solo dónde se escribe el resultado
  y por dónde se agarra la herramienta con el ratón: el elemento construido lo
  dictan las referencias y puede caer fuera de la imagen.

`ToolConfig` gana **`reference2`**, que viaja junto a `reference` dentro de
`paramsJson` — sin migración de esquema, porque esa columna existía sin usarse.
Los params escritos por `X0` (con `ref` y sin `ref2`) se siguen leyendo: una
plantilla guardada antes no puede perder su datum al abrirse.

`runTools` ordena por dependencia con **las dos** referencias y detecta ciclos
que ahora pueden ser largos (A→B→C→A); como no hay un culpable único, el motivo
nombra **a quién espera cada una**.

**Eje medio de la silueta** (`X2`). La tercera de la familia, y la única que
**mira la imagen**: los flancos hay que encontrarlos. Reutiliza entera la
exploración del Eje torneado —dos perfiles axiales, uno por lado— y lo que
cambia es qué se hace con ellos: en vez de sumar los dos offsets para dar el
diámetro, se toma el **punto medio** de cada pareja y se les ajusta una recta
robusta.

Eso es lo que hace que **dé igual cómo de descentrado vaya el trazo**: el punto
medio entre los bordes reales no depende de por dónde pase la línea que dibujó
el operador. El test lo fija trazando el mismo eje en tres alturas distintas
(centrado, +25 px y −22 px) y exigiendo el mismo resultado dentro de **±0,3 px**.

Da dos números: la **rectitud** (desviación máxima respecto a la recta ajustada
— la banda mínima que contiene los puntos, que es como se define, y no la
desviación típica: una curvatura en un extremo tiene que salir, no diluirse) y
la **desalineación entre la primera mitad y la segunda**, que es lo que delata
dos tramos de distinto diámetro que no son coaxiales. Esa segunda solo se da si
cada mitad tiene al menos cuatro puntos; con tres, el «ángulo» sería ruido con
unidades.

Solo cuentan las estaciones donde se vieron **los dos** flancos. Con uno solo se
podría suponer el centro por simetría, y eso sería inventárselo justo en la
herramienta que existe para encontrarlo: si no llegan a cinco, no mide y dice
cuántas vio y cuál es el alcance actual.

**Flechas de dependencia en el lienzo.** Cada referencia declarada se dibuja
como una flecha punteada de quien **aporta** el dato a quien lo **consume**, por
debajo de las herramientas y en trazo fino: son estructura, no medida. Sin
ellas, en pantalla se ve una recta construida y las dos de las que sale sin nada
que las relacione, y borrar la equivocada rompe la medida sin aviso. Una
referencia rota **no dibuja flecha**: no hay a dónde llevarla, y una flecha
hacia la nada haría creer que el datum existe.

**Una sola lista de herramientas** (`allToolTypes()`). El repaso de coherencia
encontró que las cuatro herramientas de pieza torneada estaban en el editor de
plantilla y **no** en la fila "Dibujar" de la vista en vivo: existían, se
guardaban, se ejecutaban… y no había forma de dibujarlas sobre el vídeo. El
motivo era prosaico —la fila se construía con una lista escrita a mano que nadie
actualizó—, y la cura es que exista **una** lista: la usan el parseo por nombre,
las dos filas de botones y los propios barridos de coherencia. Lo mismo con
`toolTypeLabel`, que estaba duplicado en las dos ventanas.

Diez pruebas recorren esa lista y exigen a **toda** herramienta lo mismo:
manijas que responden al arrastre y no derivan al re-agarrarlas, puntos de
referencia dentro de su propia huella, distancia de clic que crece al alejarse,
geometría que sigue a la pieza al girar, traslación que la mueve entera, ida y
vuelta por el JSON de la plantilla, nombre corto y nombre interno únicos,
descripción que empieza nombrando la herramienta y explica cómo trazarla,
tolerancias sugeridas que aceptan la pieza con la que se midieron, e icono
propio y no en blanco. Los barridos son `switch` exhaustivos sin `default`: con
`-Werror`, una herramienta decimoquinta **no compila** hasta pasar por todos.

**Ajustes geométricos** (`vision/fitting.*`). El Círculo —y las herramientas de
pieza torneada que vienen detrás— acaban preguntando lo mismo: qué
circunferencia explica esta nube de puntos de borde. Dos decisiones ahí:

- **Taubin, no Kasa.** Kasa minimiza el residuo algebraico sin normalizar, lo
  que pesa de más los puntos lejanos al centro. Sobre una circunferencia
  completa da igual, pero sobre un **arco parcial** el radio sale corto, y
  cuanto más corto es el arco, peor. Medido en el test con un radio de 200 px:
  a 30° de arco Kasa se desvía **10,3 px (5 %)** y Taubin **0,92 px (0,46 %)**;
  a 90°, 0,23 frente a 0,12; con medio círculo o más los dos aciertan. Cuesta
  lo mismo, así que no hay razón para el sesgo.
- **Reponderación robusta.** El borde de una pieza real trae puntos que no son
  del círculo: una rebaba, un reflejo, un rayo que enganchó el borde
  equivocado. Se reponderan con la biponderada de Tukey midiendo la dispersión
  con la MAD, que no se deja arrastrar por los propios atípicos. El resultado
  dice **cuántos puntos acabaron contando**: un borde que descarta la mitad
  puede seguir dando un diámetro perfecto y estar midiendo media pieza, así que
  el Círculo lo escribe en el detalle en vez de callárselo.

Para las **rectas** hay dos decisiones equivalentes: se ajusta por **mínimos
cuadrados totales** (distancia perpendicular, recta descrita por punto y
dirección) en vez del clásico `y = mx + b`, que no puede representar una recta
vertical y se degrada mucho antes de llegar a ella; y la dirección se devuelve
en **forma canónica** para que el mismo conjunto de puntos recorrido al revés no
dé un ángulo girado 180°. Cuando la nube es redonda sí sale una dirección, pero
es ruido: en vez de esconder un umbral dentro del ajuste se devuelve la
**anisotropía** —la misma medida y la misma fórmula que `Fixture::anisotropy`,
0 = redondo, 1 = línea— y decide quien pregunta.

**Recorrer un borde** (`inspection_editor/execution/profiles.*`). Dos formas, y
las dos devuelven una señal **muestreada uniformemente**, que es lo esencial: la
rosca y el engranaje son señales periódicas —el perfil se repite cada paso, o
cada diente— y el paso y el número de dientes salen de su periodo, que un
muestreo irregular falsearía. Por eso una muestra sin borde se devuelve marcada
`found=false` en lugar de omitirla: quitarla desplazaría todas las demás.

- **Perfil radial** `r(θ)`: desde un centro, dónde está el borde en cada
  ángulo. Base del engranaje (una repetición por diente) y del arco.
- **Perfil axial**: a lo largo de un eje, a qué distancia perpendicular está el
  borde de cada lado. Base del eje torneado (el diámetro es la suma de los dos
  lados) y de la rosca (el rizado del perfil es el paso).

Su exactitud está medida contra un **semiplano**, cuyo borde cae en una
coordenada exacta sin rasterizado de curvas de por medio: **sesgo +0,000 px**, y
una barra de 80 px se lee 39,50 + 40,50 = 80,00. Conviene saber que
`cv::circle` con suavizado pinta el disco **0,6 px más grande** que el radio
pedido —desfase constante, comprobado de R=30 a R=150—, así que las pruebas de
exactitud usan bordes duros; el suavizado se queda donde sí aporta, que es
reducir la dispersión entre rayos a menos de la mitad (0,24 frente a 0,64 px).
Traducido: un borde suave da mejor **redondez** y peor **diámetro absoluto**.

**Periodo de una señal repetitiva** (`vision/periodicity.*`). La rosca y el
engranaje son el mismo problema visto de dos maneras, y el paso y el número de
dientes salen del mismo cálculo. Tres decisiones:

- **Autocorrelación, no contar picos.** Contar picos parece más simple hasta que
  la pieza tiene un diente mellado: aparece o desaparece un pico y el recuento
  se descuadra entero. La autocorrelación mira la señal completa — medido con un
  diente arrasado, el periodo pasa de 25,00 a 24,98 y lo que baja es la
  confianza (1,000 → 0,952), que es exactamente la señal que el operador
  necesita.
- **Modo circular frente a lineal.** El perfil radial de un engranaje recorre
  una vuelta y **cierra sobre sí mismo**, así que la correlación da la vuelta y
  usa todas las muestras en todos los desfases; el de una rosca a lo largo del
  eje no cierra. En el caso lineal se le quita además la **tendencia recta**,
  que es como se separa la conicidad de la pieza del rizado que interesa.
- **Corrección del error de octava.** La autocorrelación pica también en los
  múltiplos del periodo, y con un periodo fraccionario el múltiplo puede ganar:
  con periodo real 17,4 el máximo global cae en el desfase 35, porque 2,0115
  periodos alinean mejor que 0,977. Devolver eso sería el doble del paso, o la
  mitad de los dientes — un error grande y silencioso. Se parte del máximo
  global y se comprueban sus submúltiplos **buscando en una ventana**, no en el
  valor redondeado, porque el pico real cae al lado. Verificado con 17, 23, 31 y
  47 dientes (primos, que no dividen bien el muestreo): todos exactos.

**Tolerancias.** Cada herramienta tiene banda mínima y máxima; al crearla se
sugieren automáticamente a partir de lo que mide en la pieza buena (±10 % para
distancias, conteo exacto para blobs, ±2° para ángulos). La tolerancia decide el
OK/NG pero **nunca cambia la medida** — hay un test que lo fija.

**Ayudas de dibujo**: imán al borde detectado mientras trazas, manijas por
extremo para afinar sin redibujar, duplicar y copiar/pegar, y deshacer/rehacer
real.

Límite real, documentado tras encontrarlo en un test: el **Borde liso** solo ve
lo que cae dentro de su ventana de escaneo. Una muesca más profunda que esa
ventana pasa desapercibida y la desviación vuelve a salir baja; hay que subir el
parámetro si se esperan defectos grandes.

**Por qué el Arco existe si ya hay Círculo.** El Círculo pide un centro y un
contorno cerrado; una esquina redondeada no tiene ninguno de los dos. El Arco se
define por **tres puntos sobre el propio arco** —como se comprueba un radio con
una plantilla— y esos tres puntos solo *sitúan* el sector: el radio se mide
después sobre el borde real, barriéndolo y ajustando. Si se devolviera la
circunferencia de los tres puntos se estaría midiendo el pulso de quien dibuja,
no la pieza; hay un test que marca los puntos a ojo, desviados unos píxeles, y
exige la misma medida.

**El límite que tiene, medido.** Sobre un arco corto el radio y el centro son
casi indistinguibles, así que un error **sistemático** del borde —no ruido
aleatorio, que se promedia— se amplifica en el radio. Comprobado sobre el mismo
disco de radio 40 dibujado con borde duro: la circunferencia entera se mide
39,99 y un cuadrante del mismo disco, 38,69. No es un defecto del ajuste (F1
mide que Taubin apenas sesga a 90° con ruido aleatorio): es que la
discretización del borde introduce un error correlacionado que un tramo corto no
puede promediar. Con borde suavizado —lo que se parece a una pieza real
retroiluminada— la diferencia baja a unas décimas. De ahí dos consecuencias: las
pruebas del Arco usan borde suavizado, y **la herramienta avisa cuando el tramo
marcado baja de 30°** en vez de dar el número a secas.

**Por qué el Eje no es un preset del Calíper.** Un calíper mide en **un punto**,
y en un punto un cilindro y un cono son idénticos. El Eje explora **a lo largo**:
un perfil axial por cada lado, una recta robusta ajustada a cada borde, y de ahí
salen a la vez el diámetro, la conicidad y la rectitud — los tres números con
los que se acepta una pieza al salir del torno. Verificado con un tronco de cono
dibujado con 40 px de diferencia entre extremos: sobre el tramo explorado la
conicidad esperada es 32,0 y se mide **31,9**.

Un detalle que evita un error de uso frecuente: el diámetro es la **separación
entre las dos rectas ajustadas**, no la distancia a la línea que dibujó el
operador. Por eso descentrar el eje 25 px no cambia la medida (91,44 en los dos
casos, comprobado), que es justo lo que no se consigue encadenando calíperes.
Cuando la banda de búsqueda no llega al borde —pieza gruesa o eje descentrado—,
el aviso dice **el alcance actual y qué hacer**, en vez de un "bordes
insuficientes" que no orienta.

**Cómo se mide una rosca.** El perfil de un tornillo a lo largo de su eje se
repite una vez por vuelta: es una señal periódica, y de su periodo sale el
**paso** (F4). Los diámetros **exterior** y **de fondo** son las crestas y los
valles — media del decil superior e inferior, no el máximo suelto, para que una
rebaba no defina el diámetro — sumando los dos lados, igual que el Eje, para no
depender de que el eje esté centrado.

El **ángulo de flanco** costó dos intentos y merece contarse. El primero fue
estadística de pendientes (mediana de las más inclinadas): estaba mal, porque la
proporción de muestras que caen en el flanco depende de cuánto llano tenga la
cresta, así que el resultado cambiaba con el **paso** en vez de con el ángulo —
daba 42°, 57° y 118° para la misma rosca de 60° con tres pasos distintos. Lo
correcto es **plegar la señal por su periodo** (promediado síncrono, lo que hace
un perfilómetro): al superponer decenas de vueltas alineadas por fase, el ruido
de borde baja y queda la forma del filete; sobre ella se ajusta una recta a la
parte central de cada flanco, del 20 % al 80 % de la altura, para no contaminarla
con el redondeo de la punta ni el del fondo.

Un detalle que costó un tercer intento: el número de casillas del plegado lo
manda el **periodo**, no un valor fijo. Con un periodo de 46 muestras solo
existen ~46 fases distintas, así que pedir 72 casillas dejaba huecos vacíos por
construcción. Y el perfil se toma con **grosor de promediado 1**, no el 3
habitual: ese promediado va perpendicular al escaneo, o sea **a lo largo del
eje**, justo sobre el filete que se quiere medir.

**Hasta dónde llega, medido.** Con roscas sintéticas de ángulo conocido: con
**50 px de altura de filete el ángulo sale a ±1°** (60,01 y 54,98 medidos), con
25 px a ±2°, y con 12 px deja de distinguir 60° de 55° — da ~55 para cualquiera.
El paso y los diámetros aguantan mucho mejor, así que la herramienta **avisa solo
del ángulo** en vez de rechazar la medida entera. Y **sin calibración px→mm se
niega a proponer designación**: un paso en píxeles no identifica ningún tornillo.

**Cómo se mide un engranaje.** El perfil radial desde el centro se repite una
vez por diente, así que **el número de dientes sale del mismo cálculo que el paso
de una rosca**, en modo circular porque una vuelta cierra. Verificado con ruedas
sintéticas de 12, 17, 24, 31 y 48 dientes —incluidos primos, que no dividen bien
el muestreo— todas exactas.

Sobre las puntas detectadas se ajusta un **círculo** (F1): da un centro mejor que
el marcado a ojo y su dispersión **es** la excentricidad, que es lo que se busca
al medir una rueda desgastada. El **módulo** sale de Da = m·(z+2) y sólo con
calibración: es milímetros por diente, y sin escala real no existe.

Dos salvaguardas que se ganaron probando:

- **Doble recuento.** Además de la periodicidad se cuentan los dientes picando el
  contorno, y si los dos números no coinciden se dice. Con un diente rebajado al
  60 %, la periodicidad sigue dando 24 y el conteo por picos da 23: el resultado
  correcto es 24, y el aviso es justo lo que el operador necesita ver.
- **Comprobación cruzada del módulo** por altura de diente (2,25·m en la norma).
  La primera versión dividía la diferencia de **diámetros** por 2,25, pero esa
  diferencia es el **doble** de la altura del diente: el módulo cruzado salía
  exactamente al doble y el aviso de discrepancia saltaba en ruedas
  perfectamente normalizadas. Un aviso que salta siempre es un aviso que el
  operador aprende a ignorar, así que ahora hay un test que exige que **no**
  salte con una rueda normalizada.

### De una silueta a rasgos medibles

`vision/geometry_features.*` convierte "una lista de puntos" en "cuatro lados y
cuatro esquinas redondeadas de radio 38", que es lo que hace falta antes de poder
proponer medidas solo. El camino hasta que funcionó tiene cuatro decisiones que
salieron de medir, no de suponer:

- **Barrido voraz, no detección de esquinas.** En la unión tangente de una recta
  con un redondeo **no hay esquina**: la dirección no cambia, solo la curvatura.
  Un detector de esquinas se salta justo las transiciones de una pieza
  mecanizada. El barrido —extender un tramo mientras una primitiva lo explique y
  cortar donde se rompe— encuentra por igual la esquina viva y la tangente.
  También se probó partición recursiva por el punto de peor ajuste y se
  descartó: más difícil de razonar y dejaba tramos de 541 px con residuo 11 sin
  partir.
- **Suavizado antes de ajustar.** Sin él, un lado *perfectamente recto* daba
  residuo 1,2–1,4 px por el dentado de la rasterización — el suelo de ruido
  quedaba al nivel de la tolerancia, y con eso no se distingue un rasgo limpio de
  una mezcla. Con una media móvil corta baja a 0,1–0,3 y la tolerancia puede
  apretarse a 0,8.
- **Ajuste de fronteras.** El barrido corta *después* de la transición real
  (sigue creciendo hasta pasarse), así que cada tramo se lleva un trozo del
  siguiente y en un redondeo eso aplana el ajuste: los radios salían 45–57 para
  un radio real de 40. Moviendo cada frontera al punto que minimiza la suma de
  los residuos vecinos, pasan a **38,0–38,6** y los residuos caen un orden de
  magnitud.
- **La costura, fundida solo si de verdad es un rasgo.** El contorno cierra pero
  el barrido es lineal, así que al dar la vuelta deja un muñón: un disco salía
  como "un arco de 701 px y otro de 49". Se funden el primer y el último tramo
  **ajustando su unión y aceptando solo si sigue siendo una primitiva**. Se probó
  antes con heurísticas por clase ("dos rectas cuyos extremos se tocan") y era un
  error de bulto: en un contorno cerrado los extremos *siempre* se tocan en la
  costura, así que fundía dos lados perpendiculares de un rectángulo.

Verificado contra siluetas de geometría conocida: rectángulo → 4 rectas; pieza
en L → 6 rectas; disco → **un** arco (R = 119,5 para 120); rectángulo con
esquinas de radio 40 → **4 rectas y 4 arcos** de R ≈ 38,3.

Los **agujeros** salen aparte, de la jerarquía de `findContours`: un contorno con
padre es un hueco interno, y cada uno es candidato a un Círculo.

### Medición automática: propuestas, no números

`inspection_editor/auto_measure.*` mira la pieza y propone qué medir. La
decisión que gobierna la función entera: **genera propuestas de herramientas, no
una lista de números**. Unos números sueltos serían un callejón sin salida —sin
tolerancia, sin veredicto, sin guardarse en la plantilla, sin seguir a la pieza
con el fixture, sin aparecer en el histórico—, y todo eso ya existe y funciona
para las herramientas. El operador pasa de *dibujar veinte herramientas* a
*revisar veinte propuestas*.

Qué propone, a partir de la descomposición del contorno y de los agujeros:
**largo y ancho** (rectángulo mínimo), un **Círculo** por agujero, un **Arco**
por redondeo, un **Calíper** por cada par de caras paralelas enfrentadas y un
**Ángulo** por esquina viva.

Tres reglas que hacen que la lista sea revisable:

- **Cada propuesta se ejecuta antes de proponerse.** Si la herramienta no
  consigue medir sobre esta pieza, se descarta en vez de ofrecérsela al
  operador. Lo que llega a la lista ya funciona, y su tolerancia sugerida sale
  de una medida real y no de una estimación geométrica.
- **Sin duplicados.** En un rectángulo salían "Ancho total = 279" y "Espesor 1 =
  280": la misma cota dos veces con dos nombres. Una propuesta de longitud se
  descarta si otra ya aceptada mide lo mismo en el mismo sitio.
- **Con el porqué y con tope.** Cada una lleva una frase explicando por qué se
  propone; sin eso, doce propuestas no se revisan, se aceptan todas o se
  descartan todas. Y se cortan en doce, ordenadas por tamaño del rasgo:
  cincuenta son tan inútiles como ninguna.

**El botón abre una revisión, no inserta a lo loco** (decisión confirmada con el
usuario). Insertar directamente es más rápido de programar y peor de usar: deja
al operador borrando lo que no pidió. El diálogo muestra por fila **qué mide,
cuánto da sobre esta pieza, su tolerancia sugerida y por qué se propone** —
quitar cualquiera de esos cuatro datos convierte la revisión en "aceptar todo".
Vienen todas marcadas, porque lo normal es querer casi todas y así revisar es
desmarcar; el botón dice **cuántas va a insertar**; y todas entran en **un solo
paso deshacible**, que quitar siete herramientas con siete Ctrl+Z sería peor que
haberlas dibujado a mano.

Verificado sobre piezas de medidas conocidas: agujeros de Ø70 y Ø100 se proponen
como Ø70,1 y Ø100,0; las cuatro esquinas de radio 45 salen como cuatro Arcos; un
pinchazo de 6 px no genera propuesta.

### Ver y exportar el contorno

La otra mitad de "medir la pieza y los contornos". `vision::describeContour(mask)`
devuelve **un solo informe** —contorno exterior, agujeros, descomposición,
perímetro, área y envolvente— y no cuatro funciones sueltas, porque las partes
tienen que ser coherentes entre sí: el área descuenta **estos** agujeros, que son
los hijos jerárquicos de **este** contorno. Calculadas por separado, una pieza
pequeña con su propio agujero al lado de la principal le restaría área a la
grande; hay un test que lo comprueba.

La superposición (`EditorCanvas::paintContourReport`) pinta cuatro cosas y cada
una tiene su motivo:

- El **contorno crudo en blanco tenue, por debajo**. Es la referencia contra la
  que se lee todo lo demás: sin él, un arco mal ajustado se ve como un arco
  perfecto y nadie nota que no sigue al borde.
- Los **tramos rectos en azul y los arcos en naranja**, dibujados **a partir de
  la primitiva ajustada**, no de los puntos del contorno — que es justo lo que
  hace visible dónde se despega.
- Un **punto blanco en cada corte** entre tramos, para que la descomposición se
  vea aunque dos tramos vecinos sean del mismo tipo.
- El **radio junto a cada arco**, que es el dato que se viene a buscar en un
  redondeo. Solo en los tramos largos: etiquetar los cortos tapa la pieza de
  números.

Va **por debajo de las herramientas** y no se puede seleccionar ni arrastrar: es
una capa de consulta y no debe competir con lo que sí se mide. Y se invalida al
recapturar desde la cámara, porque describe la foto anterior.

El **resumen** (perímetro, área, agujeros, envolvente, cuántos tramos) lo genera
el propio lienzo, en la unidad activa, y lo reutiliza la ventana para el panel de
estado: un solo sitio donde se decide el formato, o los dos acabarían diciendo
cosas distintas. El área se convierte con el **cuadrado** de la escala; hacerlo
linealmente daría un número plausible y cuatro veces mayor, y hay un test que
falla si alguien lo cambia.

#### El perímetro no se mide con `cv::arcLength`

Preparando `F1` se midió el sesgo del perímetro y salió algo peor de lo esperado.
`arcLength` mide el polígono que pasa por los **centros de los píxeles** del
borde, y eso lo hace **depender de cómo esté girada la pieza**. El mismo cuadrado
de 200 px, barrido por 0°, 15°, 30°, 45° y 60°:

| ángulo | `arcLength` | `digitalPerimeter` |
| --- | --- | --- |
| 0° | +0,00 % | −2,05 % |
| 15° | +7,06 % | +0,75 % |
| 30° | +7,71 % | −0,22 % |
| 45° | −0,30 % | −0,92 % |
| 60° | +7,71 % | −0,22 % |
| **dispersión** | **8,01 puntos** | **2,79 puntos** |

Ese **+7,7 %** es el problema, no el sesgo del círculo del que avisaba el plan:
la misma pieza leída un 7,7 % más larga solo por estar posada de otra manera. Una
medida de inspección no puede depender de eso.

Se usa **Vossepoel–Smeulders** (`vision::digitalPerimeter`), que pesa por
separado pasos rectos, pasos diagonales y esquinas: error por debajo del 2,3 % en
todas las figuras probadas y **2,9 veces menos dispersión** por orientación. Se
descartó la corrección de Kulpa (×0,948) porque arregla el círculo y estropea el
cuadrado alineado en un −5,2 %.

El precio, dicho claro: en un borde recto **alineado con los ejes** `arcLength`
era exacto y el estimador se queda un 2 % corto. Por eso el test de la placa
cuadrada tuvo que pasar de 1,5 % a 3 % — y esa placa es justo el caso donde el
método viejo era perfecto. Se acepta porque una pieza real no llega siempre
alineada, y un error acotado en todas las orientaciones vale más que uno perfecto
en una sola.

Nadie tenía tolerancias puestas sobre este número: solo se muestra en el panel de
contorno y se exporta al CSV. Si algún día alimenta un veredicto, el cambio de
estimador sí sería un cambio de criterio.

La **exportación a CSV** (`vision::contourToCsv`) existe para llevarse la pieza a
un CAD. Dos decisiones que parecen menores y no lo son:

- La **unidad va en el nombre de la columna** (`x_mm` / `x_px`), no en una línea
  de comentario: los importadores de CAD y las hojas de cálculo tragan cabeceras
  y no comentarios, y un archivo de coordenadas sin unidad no sirve para nada.
- El formato se escribe con el **locale clásico a la fuerza**. En un Windows en
  español el separador decimal es la coma, y un CSV con `12,50` en un archivo
  separado por comas no lo abre nadie. El test vuelve a leer el archivo partiendo
  por comas —igual que hará el CAD— y falla si aparece un campo de más.

Verificado con números: el CSV de una placa de 340×340 px releído encierra
114 921 px² (339², el contorno de una máscara pasa por el centro de los píxeles
del borde) y con calibración de 0,25 mm/px da 7 182 mm² frente a los 7 225
teóricos.

### Avisos de condiciones: el fallo que no se ve en el número

Estas herramientas dan resultados **creíbles** con datos malos, que es la peor
forma de fallar: un diámetro con la cámara inclinada sale corto y nada en el
número lo delata. Por eso, además de negarse cuando no puede medir, cada
herramienta de esta familia dice **en qué condiciones midió**:

| Aviso | Cuándo | Por qué importa |
|---|---|---|
| Cámara inclinada | `MarkerScale::quality` < 0,75 | Un círculo se ve como elipse y los diámetros salen cortos |
| Poco contraste | Gradiente medio del borde < 25 | El "borde" detectado no es el de la pieza |
| Arco corto | Tramo < 30° | Radio y centro son casi indistinguibles |
| Alcance corto | Un lado del eje sin bordes | La banda no llega a la pieza |
| Filete pequeño | Altura < 20 px | El ángulo de flanco deja de resolverse |
| Recuento discrepante | Periodicidad ≠ conteo de picos | Un diente de más o de menos cambia la rueda |
| Sin calibración | Se pide módulo o designación | En píxeles esos números no existen |

La regla que gobierna todo esto: **un aviso que salta siempre es un aviso que el
operador aprende a ignorar**, y entonces no avisa de nada. Se ha pagado dos veces
por no respetarla — el módulo cruzado del engranaje con el divisor a la mitad, y
la calidad de escala, que vale 0 cuando **no hay marcador** y habría hecho saltar
"cámara inclinada" en cada medición sin ArUco. Por eso hay un valor explícito de
**"no se sabe"** que no dispara nada, y por eso la mitad de las pruebas de avisos
comprueban que **NO** salten cuando no toca.

### El trazado: por qué las tolerancias van en píxeles de pantalla

La aritmética del lienzo vive en `canvas/canvas_geometry.*`, **fuera del
widget** y sin Qt, para poder probarla sin abrir una ventana: `ViewTransform`
(ajuste, zoom, límite del desplazamiento y las dos conversiones
pantalla↔imagen), las manijas de cada tipo de herramienta y la distancia de un
clic a una geometría.

La regla que gobierna todo lo que el operador *toca* es esta: **la zona de clic
se mide en píxeles de PANTALLA y se traduce a píxeles de imagen dividiendo por
la escala de la vista** (`pickTolerance`). Suena obvio y no lo era: las
tolerancias estaban fijadas en píxeles de imagen mientras las manijas se
dibujan siempre del mismo tamaño en pantalla, así que solo coincidían al 100 %
de zoom. Medido en el propio widget con una imagen de 1920×1080 en una ventana
de 900×640, al zoom máximo la escala es **9,375 px de pantalla por px de
imagen**, de modo que:

- una manija de 7 px dibujados se agarraba desde **84 px de distancia** — un
  clic en un sitio visiblemente vacío deformaba la herramienta;
- el punto de cierre del blob poligonal atrapaba desde ~112 px, así que poner
  un vértice cerca del inicio cerraba el polígono sin querer;
- un trazo intencionado de 30 px no llegaba al mínimo de 8 px de imagen (75 de
  pantalla) y la herramienta **no se creaba, sin aviso**.

Ese último caso destapó que ahí había **dos reglas confundidas en un solo
número**, y ahora están separadas porque responden a preguntas distintas:

| Mínimo | En qué se mide | Qué decide | Si no se cumple |
|---|---|---|---|
| 8 px de **pantalla** | movimiento de la mano | ¿fue un clic o un arrastre? | se ignora, sin ruido |
| 8 px de **imagen** | tamaño de la herramienta | ¿hay muestras para medir? | se avisa en la barra de estado |

Una herramienta de 3 px de imagen no tiene perfil que promediar ni rayos que
lanzar: no se puede crear. Pero el gesto fue deliberado, así que la señal
`traceRejected` lo dice con el número concreto en vez de tragárselo.

Con una imagen grande en una ventana pequeña pasaba lo contrario: la manija que
se ve no se podía agarrar. Los cuatro umbrales (selección, manija, cierre del
polígono y mínimo de arrastre) están ahora en píxeles de pantalla, y hay
pruebas que fijan que la zona de agarre sea la misma con cualquier zoom.

El **imán al borde** sigue la misma lógica, pero solo hacia abajo: su alcance se
acota por lo que se ve **sin superar nunca los 14 px de imagen originales** y con
un suelo de 6, para que la ventana de escaneo siga dando de sí. Al zoom de
ajuste no cambia nada; al máximo deja de arrastrar el extremo hasta un borde que
está a 112 px de pantalla de donde se soltó. Comprobado sobre el widget con un
borde sintético: soltando en x=991 con el ajuste, aterriza en 999,5 (subpíxel);
soltando en x=988 al máximo, se queda en 988.

**Las etiquetas de medida** también se colocan con geometría probable
(`placeLabel`): se busca hueco alejándose del ancla, primero hacia abajo y luego
hacia arriba, y **siempre dentro del área visible**. Antes solo se empujaba
hacia abajo sin mirar el borde, así que una medida anclada en la parte baja del
lienzo se empujaba fuera de la vista y el operador **no veía ninguna lectura** —
peor que un solape, porque no hay nada que sugiera que falta algo.

El alcance de la búsqueda está acotado a propósito (12 saltos por lado): una
etiqueta que se va al otro extremo deja de pertenecer visualmente a su
herramienta. Con una plantilla realista (doce medidas casi en el mismo punto) no
hay ni un solape; con cuarenta amontonadas, la banda alcanzable da para ~24 y
las demás se solapan a sabiendas, pero **ninguna desaparece**. Se probó añadir un
barrido fino para aprovechar huecos entre rejillas desalineadas y no mejoró el
reparto: el límite es el alcance, no el tamaño del salto, así que se quitó.

Dos contratos más que se cerraron aquí:

- **`setHandlePoint` con un índice fuera de rango no toca nada.** Antes caía en
  la rama por defecto de cada tipo y movía la última manija (cambiaba el radio,
  redimensionaba el blob).
- **Un arrastre de manija pertenece a la herramienta en la que empezó.** Si la
  selección cambia desde fuera a media faena —la lista del panel, un deshacer—
  el arrastre se cancela; era la vía real por la que llegaba un índice de manija
  a una geometría de otro tipo.

---

## 6. Tablero de referencia y modos de medición

El **tablero** es un sistema de coordenadas visible con el **cero declarado**,
al estilo del *datum* de la metrología: sirve para medir *posición* (cuánto se
desvía y cuánto gira la pieza) y no solo distancias sueltas. Convenios: +X a la
derecha, **+Y hacia arriba** (se invierte la Y de la imagen), ángulo en
(−180, 180] antihorario positivo.

El cero puede ser:

- **Centro del contorno** (recomendado): el centro geométrico de la pieza, el
  que se ve centrado.
- **Centro de masa**: el centroide. En piezas asimétricas cae visiblemente
  desplazado — en una pieza en L, 20 px por eje. Fue justo el origen de un fallo
  reportado: "no centra bien".
- **Centro de la imagen**: fijo en pantalla, para vigilar el centrado en un
  soporte.
- **Punto fijado a mano**, más un **ajuste fino** en X/Y aplicable a cualquiera
  de los anteriores.

**Dos modos de medición por pieza**:

- **Posición real (personalizada)**: lo de siempre; cada herramienta se juzga
  con sus tolerancias.
- **Especial (tablero centrado)**: además, la pieza se juzga por **dónde está**:
  desviación máxima del centro y giro máximo, que entran en el veredicto. La
  regla de giro **se salta sola** cuando la pieza es casi simétrica y su eje no
  es fiable, en vez de dar NG falsos.

La **regla graduada** (opcional) dibuja escalas en los bordes con números en la
unidad activa, una barra de escala y la marca del cursor.

---

## 7. Escala real: de píxeles a milímetros

Tres caminos, de menos a más robusto:

1. **Objeto de referencia**: mides algo de tamaño conocido y escribes sus
   milímetros. Simple y exacto en el plano de trabajo.
2. **Distancia de cámara + campo de visión** (modelo pinhole): estima mm/px a
   partir de la altura de la cámara.
3. **Marcador ArUco** (diccionario 4×4) de lado conocido: se detecta en cada
   frame y se calcula la **homografía del plano** imagen→mm. La escala se
   reajusta sola al acercar o alejar la cámara, y **corrige la perspectiva**:
   las herramientas de longitud convierten sus puntos por la homografía en vez
   de multiplicar por una constante.

La calibración se **sella con la cámara y la resolución** con las que se hizo: si
cambian, la app avisa de que la escala dejó de ser válida en vez de mostrar
milímetros silenciosamente equivocados.

El marcador ArUco reporta además un **indicador de calidad** (0–1) basado en la
uniformidad de sus lados y diagonales: mide cuán perpendicular está la cámara al
plano. Con la cámara muy inclinada, una escala única deja de ser fiable lejos
del marcador y el indicador lo dice.

**Límite fundamental**: con una sola cámara 2D no se recupera la profundidad
punto a punto. Todo lo anterior vale para objetos **en el plano de trabajo**;
medir a distinta altura requeriría cámara de profundidad o estéreo.

---

## 8. Apariencia: el modelo y la detección de anomalías

Esta es la parte que la gente llama "la IA", así que conviene ser preciso con
los nombres.

### El modelo

- **EfficientNet-Lite4**, del zoo oficial de ONNX (~49 MB), ejecutado con **ONNX
  Runtime** en C++. El prompt original pedía Lite0, pero no hay un ONNX fiable
  publicado de Lite0; Lite4 es la misma familia y el mismo preprocesado.
- La herramienta `prepare_model` (se compila con el proyecto) **le recorta el
  clasificador**: en vez del softmax de 1000 clases, la salida pasa a ser el
  vector de características del *global average pooling* — un **embedding de
  1280 dimensiones**.
- Entrada 224×224, normalización `(x − 127) / 128`, disposición NHWC o NCHW
  autodetectada, salida **L2-normalizada** para que la similitud coseno sea
  comparable.
- Lo que entra al modelo no es el frame: es el **recorte canónico** de la pieza
  (256×256, sin fondo, orientado por el fixture), así que la comparación no
  depende de dónde ni cómo esté colocada.

### Qué NO se hace

- **No se entrena ni se reentrena nada.** El modelo va congelado. El nombre
  correcto es *transfer learning sin ajuste fino*, o "extractor de
  características congelado".
- No hay detección supervisada por *deep learning* (nada de YOLO ni segmentación
  semántica), ni *template matching* por correlación, ni OCR.

### Qué se hace

- **Registrar una pieza** es lo que en visión industrial se llama *teach-in* o
  *golden sample*: se le enseña al sistema cómo es la pieza buena. Se capturan
  ~30 muestras válidas y se acumula su estadística.
- La comparación es **detección de anomalías de una sola clase** (*one-class* /
  *unsupervised anomaly detection*, también *novelty detection*): solo se ven
  piezas buenas, nunca hace falta etiquetar defectos.
- El criterio: **similitud coseno** del embedding actual contra la media de la
  referencia, con umbral en `media − max(k·σ, 0.02)`. El mismo espíritu que el
  control estadístico de procesos. `k` es configurable (3 por defecto): más bajo
  = más estricto.
- Media y desviación se actualizan con el **algoritmo en línea de Welford**, en
  O(dimensiones) por muestra y sin guardar los embeddings anteriores. Eso es lo
  que el programa llama **aprendizaje incremental**: cada pieza buena confirmada
  produce una **versión nueva** de la referencia, y las anteriores nunca se
  borran.

### Con qué se parece esto en el mundo

La familia de métodos del *benchmark* MVTec AD —**PaDiM, PatchCore, SPADE,
FastFlow**— y los enfoques con autoencoders o GAN (*GANomaly*) comparten la idea
de comparar características de una red preentrenada contra las de piezas buenas.
Este programa es una versión deliberadamente ligera de esa idea: **un solo vector
por pieza** en lugar de un banco de parches, que es lo que permite que corra en
una PC modesta y sin GPU.

Sin el modelo, la aplicación **degrada a inspección solo con herramientas**: se
puede registrar y medir igual, avisando de que no habrá comparación de
apariencia.

---

## 9. Veredicto

`domain::combineVerdict` es lógica pura y combina tres cosas:

1. **Apariencia** (si hay modelo y referencia): anómala o no.
2. **Herramientas**: cuántas quedaron fuera de tolerancia.
3. **Posición** (solo en modo Especial): centrado y giro dentro de sus
   tolerancias.

OK exige las tres. El resumen explica **por qué** sale NG ("anomalía de
apariencia; 2 herramienta(s) fuera de tolerancia; pieza descentrada"), y cada
inspección guarda historial, miniatura y estadísticas del día.

---

## 10. Persistencia

SQLite en un solo archivo junto al ejecutable, con envoltura RAII propia
(`Db`/`Statement`), todo por `Result<T>`, claves foráneas activas, modo WAL y
`busy_timeout`. **Diez tablas** y migraciones versionadas por
`PRAGMA user_version`: el esquema va por la **v8** y cada salto está en su
propia migración, de modo que una base de datos vieja se actualiza sola al
abrirla.

Qué se guarda por pieza: referencias de embeddings **versionadas**, miniatura,
rasgo distintivo, ajuste de orientación, modo de medición y su tablero,
tolerancias de posición, perfil de detección y sus plantillas de herramientas
(una pieza puede tener varias, por ejemplo una por cara).

Aparte: ajustes de la aplicación (calibración, atajos, preferencias, controles
de cámara) e historial de inspecciones con estadísticas por día.

Todo lo que no está ligado a una pieza se puede **exportar e importar en JSON**
para clonar la puesta a punto a otra PC de la línea. Las plantillas de
herramientas se exportan aparte, para copiarlas entre piezas.

---

## 11. Empaquetado

`.\run.ps1 -Package` genera un `.zip` que corre en una PC **sin MSYS2**: el
ejecutable, los plugins de Qt vía `windeployqt6`, el modelo, y las DLL restantes
resueltas **recorriendo los imports del binario con `objdump`** contra
`ucrt64\bin`.

Se usa `objdump` y no `ldd` a propósito: `ldd` resuelve según el PATH del shell
que lo ejecuta y llegaba a devolver DLL de otro runtime. Los imports, en cambio,
son propiedad del binario. Verificado arrancando el paquete con un PATH limpio.

---

## 12. Glosario: cómo se llama cada técnica

Para poder explicar el sistema con las palabras correctas, y no atribuirle
métodos que no usa:

| En el programa | Nombre técnico |
|---|---|
| Detección de la pieza | Umbralización de Otsu + morfología matemática + extracción de contornos |
| Centro y eje de la pieza | Momentos de imagen (centroide y eje principal) |
| Anclaje de herramientas | *Position fixture* / *reference frame* / *datum* / *fixturing* |
| Caliper | Detección de bordes por perfil de intensidad con interpolación subpíxel |
| Rectángulo mínimo | *Rotating calipers* (`minAreaRect`) |
| Escala en mm | Marcadores fiduciales (ArUco) + homografía de plano |
| "Entrenar" una pieza | *Teach-in* con *golden sample* (no hay entrenamiento de red) |
| Extracción de rasgos | *Transfer learning* sin ajuste fino / extractor congelado |
| Comparar apariencia | Detección de anomalías de una sola clase (*one-class*, *novelty detection*) |
| Umbral de anomalía | Similitud coseno con umbral tipo control estadístico (media − k·σ) |
| Aprendizaje incremental | Algoritmo en línea de Welford (estadística, no reentrenamiento) |

---

## 13. Cómo mejorarlo

Ordenado por relación entre lo que aporta y lo que cuesta. Ninguna de estas
cosas está hecha; son propuestas con su contrapartida dicha en voz alta.

### Mejoras de precisión

1. **Iluminación antes que software.** El salto de calidad más grande y más
   barato no está en el código: es un aro de luz difusa y un fondo mate de color
   contrastado. La mitad de los problemas de segmentación desaparecen.
2. **Segmentación adaptativa** (umbral local en vez de Otsu global) para
   sobrevivir a degradados de sombra sin tener que cambiar de perfil a mano.
   Coste: más lento y con más parámetros que ajustar.
3. **Corrección de distorsión de lente** con un patrón de tablero de ajedrez
   (`calibrateCamera`). Hoy no se corrige, y en lentes angulares las medidas
   cerca del borde se estiran. Es la mejora de exactitud más seria que queda.
4. **Subpíxel en el contorno**, no solo en las herramientas de borde: hoy el
   contorno es de resolución entera.

### Mejoras de inspección

5. **Banco de parches en vez de un vector** (estilo PatchCore/PaDiM): permitiría
   **localizar** el defecto en la pieza, no solo decir que algo cambió. Coste:
   mucha más memoria y CPU por pieza; habría que medir si sigue siendo viable
   sin GPU.
6. **Múltiples referencias por pieza** (variantes admisibles: dos acabados, dos
   proveedores) en vez de una sola media.
7. **Mapa de calor de diferencia** contra el recorte de referencia: es barato
   (resta y suavizado) y ayuda muchísimo a que el operador entienda un NG.
8. **Curva ROC con piezas malas reales**: hoy el umbral es estadístico. Con un
   puñado de piezas defectuosas etiquetadas se podría elegir el umbral que
   maximiza el acierto en lugar de asumir 3σ.

### Mejoras de operación

9. **Disparo por sensor** (fotocélula o señal externa) en vez de inspección
   continua por temporizador, para líneas con cadencia fija.
10. **Salida a PLC / señal digital** del veredicto: sin eso, la app informa pero
    no actúa sobre la línea.
11. **Exportar el historial a CSV/Excel** y un informe por turno.
12. **Multi-cámara** (varias vistas de la misma pieza) con veredicto combinado.

### Mejoras técnicas

13. **Firmar el ejecutable y hacer un instalador** en vez del `.zip` portable.
14. **Integración continua** que compile y pase los tests en cada commit; hoy
    eso se hace a mano en la máquina de desarrollo.
15. **Traducciones**: la interfaz está en español y los textos van con `tr()`,
    así que falta solo generar los `.ts`/`.qm`.
16. **Pruebas con cámara real automatizadas**: hoy toda la batería usa imágenes
    sintéticas y el hardware se prueba a mano.

### Lo que NO conviene hacer

- **Entrenar una red propia** con las piezas de la línea: exige cientos de
  ejemplos etiquetados, incluidos defectos que casi nunca se tienen, y ata el
  sistema a un modelo que hay que mantener. El enfoque de una sola clase existe
  precisamente para evitarlo.
- **Medir profundidad con esta cámara.** No es cuestión de software: una sola
  vista 2D no la contiene. Si hace falta, es cambio de hardware (estéreo o
  cámara de profundidad).
