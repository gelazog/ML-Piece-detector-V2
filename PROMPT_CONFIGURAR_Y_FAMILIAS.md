# PROMPT — Configurar, y la paleta de herramientas por familias

Documento de planificación para la siguiente ronda de **PC Inspector**.
Continúa [PROMPT_MAESTRO_PC_INSPECTOR.md](PROMPT_MAESTRO_PC_INSPECTOR.md) (las 6
fases base) y las rondas de pulido posteriores. Pensado para consumirse **dentro
de `/loop`, ítem por ítem**.

Sale de una petición con dos mitades:

1. Un apartado **«Configurar»** donde se pueda establecer y modificar la imagen:
   **enfoque**, **cámara**, **cuántas piezas van a aparecer**, y un **zoom
   automático** para que el programa trabaje menos y se centre directamente en
   la pieza, sea grande o pequeña.
2. Reorganizar las herramientas en **familias** y llenarlas: **Figuras
   básicas** (simetría, región, perímetro, área, lados…), **GD&T**
   (paralelismo, perpendicularidad, redondez, rectitud, posición verdadera…),
   **Medición en línea** (diámetro/radio, punto a punto, ángulos…) y
   **Máximos, mínimos y piezas torneadas** (engranajes, roscas…). Las catorce
   herramientas que ya existen se reparten entre esas familias.

---

## Principios (heredados, no negociables)

1. **No reestructurar la arquitectura por capas** (`core/ domain/ camera/
   vision/ ml/ database/ inspection_editor/ repositories/ engine/ ui/`). Las
   dependencias siempre bajan.
2. **Compilar y probar de verdad** antes de cerrar un ítem: `cmake --build
   --preset mingw-release` limpio bajo `-Werror`, `ctest --preset
   mingw-release` en verde, y humo de la app.
3. **Un test por lógica nueva no trivial**, con piezas sintéticas de geometría
   conocida cuando se mida algo.
4. **Commits atómicos por ítem**, mensaje en español, **sin firma**.
5. **Actualizar README, ARQUITECTURA y memoria** al cerrar cada ítem.
6. Al terminar el backlog entero, **borrar este archivo**.

Este backlog es largo (34 ítems) porque la petición lo es. Está partido en
familias que **se cierran de forma independiente**: se puede parar después de
cualquiera de ellas y lo entregado sigue siendo coherente. Lo único que no se
puede saltar es el bloque `R`, que es el andamio de todo lo demás.

---

## Punto de partida medido (no supuesto)

Antes de planificar se inventarió el código y se investigó cómo resuelven esto
los sistemas comerciales. Estos son los hechos que gobiernan el documento;
están comprobados, no estimados.

**Del código:**

- **Añadir un tipo de herramienta cuesta hoy ~22 ediciones en 10 archivos.**
  Tres no compilan si se olvidan (`typeOf`, `translateGeometry`, `toJson`,
  protegidas con `static_assert(alwaysFalse<T>)`) y **cuatro fallan en
  silencio**: los visitors de `canvas/canvas_geometry.cpp` (`referencePoints`,
  `handlePoints`, `setHandlePoint`, `distanceToGeometry`). Sin ellos la
  herramienta compila, se guarda y mide… y no se puede seleccionar ni editar
  con el ratón.
- **La fila «Dibujar» ya no cabe.** 15 `QToolButton` de icono + 5 botones de
  texto ≈ **1400-1450 px de ancho mínimo**, en una ventana que arranca a
  1100 px (`main_window.cpp:217`). El editor tiene el mismo problema en
  vertical: 15 filas ≈ 440 px de alto. **No hay ninguna agrupación.**
- **Los atajos se agotaron.** `toolKeys[]` (`main_window.cpp:1042`) está escrita
  a mano y cubre 10 de 14: Arco, Eje, Rosca y Engranaje **no tienen tecla**
  porque los dígitos 1-9 y 0 están usados.
- **No existe la idea de «cuántas piezas espero».** `analyzeFrame` se queda con
  el **contorno mayor** (`contour_analysis.cpp:16-31`) y `pipeline.cpp:31-41`
  **borra el resto de la máscara** a propósito. Las demás piezas no se cuentan,
  no avisan y no llegan al fixture.
- **No hay zoom ni ROI automático.** El zoom de `EditorCanvas` es solo de
  visualización. Pero **`PipelineConfig::roi` ya existe** y `analyzeFrame` ya
  recorta por él devolviendo coordenadas de la imagen completa: falta **quién
  calcule y mueva ese rectángulo**, no el mecanismo.
- **La nitidez ya se calcula.** `domain::QualityMetrics::sharpness` (varianza
  del Laplaciano) existe y se usa **solo** para validar capturas al registrar.
  El deslizador de enfoque se mueve hoy a ciegas.
- **La configuración está repartida en 7 diálogos y 4 menús.**
- **`min/maxAreaFraction` y `canonicalSize` no tienen UI ni clave** de
  `Settings`.
- **Fallo real:** `EditorWindow` llama `vision::analyzeFrame(image)` **sin
  configuración** en `editor_window.cpp:472`, `:713` y `:735` → el editor
  detecta con los valores por defecto, no con los del operador.
- **La «redondez» de hoy no es la de la norma.** `runCircle`
  (`tool_executor.cpp:382-395`) devuelve `max |rᵢ − R|` respecto al círculo de
  mínimos cuadrados. La redondez ISO es la **zona mínima**: el mínimo, sobre
  todos los centros posibles, de `Rmax − Rmin`. Son números distintos y el
  segundo es el que aparece en un plano.
- Esquema de BD en **v8**. Los tipos de herramienta se persisten **por nombre**,
  así que añadir tipos **no necesita migración**.

**De la industria** (Cognex In-Sight/VisionPro, Keyence CV-X, MVTec
MERLIC/HALCON, Hikrobot VisionMaster, Omron FH, Datalogic Impact):

- Los seis agrupan por **función de visión**, nunca por familia de pieza:
  *adquirir → calibrar → localizar/alinear → medir/inspeccionar → construir
  geometría derivada → evaluar*.
- **Ningún sistema de visión 2D tiene un grupo «GD&T»**: eso vive en el mundo
  CMM (PC-DMIS, CALYPSO). Es un diferenciador, pero implica que **no hay
  convención de nombres que copiar** y hay que ser especialmente cuidadoso.
- Cognex dedica una categoría entera, `Geometry Tools`, a **construcciones
  geométricas** (punto medio, bisectriz, intersección, círculo por N puntos):
  ~10 herramientas. Nosotros no tenemos ninguna.
- HALCON, el más potente del mercado, se conforma con **cuatro primitivas** en
  su módulo de metrología 2D: círculo, elipse, rectángulo y recta. No hace
  falta inventar cincuenta herramientas.

---

## Tres decisiones de diseño que gobiernan el documento

### 1. Primero el andamio, después las herramientas

Este backlog añade una veintena de herramientas. A 22 ediciones por herramienta,
con cuatro trampas silenciosas cada una, eso son ~450 ediciones y ~80
oportunidades de dejar una herramienta a medias sin que nada se queje.

Por eso los ítems `R` van antes que cualquier herramienta nueva, y su objetivo
no es cosmético: **convertir las cuatro trampas silenciosas en errores de
compilación** y agrupar la paleta para que quepa. Sin eso, cada herramienta
nueva es una tirada de dados.

### 2. GD&T sin datum no es GD&T, y el datum es una construcción

Paralelismo, perpendicularidad, angularidad, posición verdadera y perfil **no
son medidas absolutas**: son medidas **respecto a una referencia declarada**
(datum A, B…). Una herramienta que dijera "paralelismo = 0,08" sin decir
*paralelo a qué* estaría inventándose un número con nombre de norma, que es
peor que no darlo.

Y para declarar un datum hacen falta **construcciones geométricas**: la recta
media entre dos caras, la intersección de dos bordes, el centro de un círculo.
Es decir, el mecanismo que necesita el GD&T y la familia de construcciones que
tiene Cognex **son el mismo mecanismo**: *una herramienta puede producir un
elemento geométrico, y otra puede consumirlo por nombre*.

Eso se construye una vez, en `X0`, y de ahí cuelgan las dos familias.
Consecuencias que hay que asumir desde el principio:

- `ToolConfig` gana una **referencia a otro elemento por nombre**.
- `runTools` pasa a ejecutarse **en dos pasadas**: primero lo que puede ser
  referencia, después lo que la consume. Con detección de ciclos.
- Una herramienta cuya referencia no existe o falló **no mide**: lo dice y se
  queda en NG, en vez de caer a una referencia implícita.

### 3. Las familias que pidió el usuario, más la que hace falta

La petición nombra cuatro apartados. La industria agrupa por función y ninguno
de los seis productos consultados tiene un grupo por *familia de pieza* como
«torneadas». Aun así **se respetan los cuatro tal cual se pidieron**: son los
que el operador de esta app entiende, y para una app de piezas mecanizadas
tienen sentido.

Lo que sí se añade es **una quinta familia, Construcciones geométricas**, y no
por simetría con Cognex: **sin ella no se puede declarar un datum, y sin datum
no hay GD&T**. Es una dependencia, no un adorno.

De las otras tres categorías que la industria tiene y esta paleta no: dos ya
existen en la app fuera de la paleta (**Calibración**, en el diálogo de escala,
y **Alineación**, que es el Position Fixture y se aplica solo), y la tercera
(**Presencia y conteo**) ya está cubierta por Blob y Blob poligonal dentro de
Figuras básicas.

---

## Lo que hay que decir en voz alta

Sigue valiendo lo de la ronda anterior (silueta 2D: contraluz, cámara de
frente, calibración para los mm, vista correcta por herramienta), y se añade lo
propio de esta:

- **Un zoom de trabajo que se equivoque es peor que no tenerlo.** Si el ROI
  automático pierde la pieza y nadie se entera, la app medirá con confianza
  dentro de un rectángulo donde ya no hay nada. Todo `C3` tiene que **volver a
  la imagen completa en cuanto duda**, y decirlo.
- **Reducir la imagen para segmentar no puede cambiar la medida.** Las
  herramientas miden siempre a resolución completa; lo que se reduce es solo la
  pasada que *localiza* la pieza. Si `C4` no demuestra que la medida no cambia,
  **no se entrega**.
- **Contar piezas y medir piezas son dos cosas distintas.** Detectar que hay 5
  en vez de 6 es útil y barato. Medir las 6 es útil y caro. `C5` y `C6`, y el
  segundo puede quedarse fuera.
- **El GD&T de esta app es indicativo mientras la óptica no sea telecéntrica.**
  Con un objetivo normal, la silueta de una pieza con espesor es una proyección
  en perspectiva: se mide el contorno de la penumbra, no la arista, y el error
  **depende de la distancia**, así que no se puede calibrar fuera. Las
  herramientas GD&T deben decirlo en su descripción. No es excusa para medir
  mal: es la diferencia entre un número y un número con su incertidumbre.
- **Hay tolerancias que una silueta NO contiene, y no se van a ofrecer.** Esta
  lista va en la UI, no solo en el README, porque decir "no puedo medir esto y
  este es el motivo" da más confianza que un número inventado:
  | No se ofrece | Por qué |
  |---|---|
  | **Planitud** | Es una propiedad de una superficie; una silueta solo da la arista. Lo medible es la **rectitud de la arista**, que se llama distinto. |
  | **Cilindricidad** | Pide la superficie completa; una silueta da **dos generatrices**. El sustituto honesto ya existe: rectitud de cada flanco + conicidad + rango de diámetro (herramienta Eje). |
  | **Perfil de superficie** | 3D por definición. Solo se ofrece **perfil de línea**. |
  | **Concentricidad y simetría normativas** | **Retiradas de ASME Y14.5-2018** por ser inverificables de forma repetible. Se sustituyen por posición y runout. Lo que sí se da es "**desviación de centros**", con ese nombre. |
  | **Runout total** | Necesita rotación indexada. El circular sería abordable con un eje giratorio; no con una foto. |
- **El ángulo de flanco de la Rosca tiene un sesgo conocido.** Por el ángulo de
  hélice, el flanco cercano y el lejano se proyectan con inclinaciones
  distintas. Los comparadores ópticos lo resuelven **inclinando el eje óptico**
  el ángulo de hélice; sin esa corrección hay sesgo sistemático. Se documenta
  (`M5`), no se disimula.

---

## Backlog

Formato: casilla, ID, tarea, por qué, cómo, archivos, cómo se verifica.

### C — Configurar

- [x] **C1 — El panel «Configurar»: un solo sitio.** *(Dos desvíos del plan, los
  dos por coherencia: (a) **no se dejaron cáscaras `QDialog`** de los diálogos
  convertidos, porque `MainWindow` era el único que los abría y habrían quedado
  como código muerto; (b) **Piezas y Rendimiento no se crean aquí** sino en `C3`
  y `C5`, cuando tengan contenido — una pestaña vacía contradice la propia
  verificación de este ítem. Y **Escala y Atajos son asistentes, no
  formularios**: su pestaña explica y abre el de siempre.)* Hoy los ajustes viven en
  siete diálogos colgados de cuatro menús, y para cambiar el enfoque y el
  umbral hay que saber que uno está en *Cámara* y el otro en *Inspección*.
  Nuevo `ui/configure_dialog.{h,cpp}`: un `QTabWidget` con las páginas **Cámara
  e imagen**, **Detección**, **Piezas**, **Rendimiento**, **Escala**,
  **Preferencias** y **Atajos**, con una única entrada de menú y un botón.
  El movimiento es **mecánico y sin reescribir**: el cuerpo de cada diálogo
  existente se extrae a un `QWidget` (`XxxPage`) y el diálogo de siempre pasa a
  ser una cáscara que aloja esa misma página — así nada de lo que hoy los abre
  se rompe y **hay una sola implementación de cada página**. *Piezas* y
  *Rendimiento* nacen vacías y las llenan `C3`-`C6`.
  Se recuerda la última pestaña abierta (`config_last_tab`).
  Verificación: prueba de gesto en `pci_gui_tests` que abre el diálogo, recorre
  **todas** las pestañas exigiendo que ninguna esté vacía, y comprueba que un
  valor cambiado y aceptado llega a `Settings`.
  Skills: `qt-ui-design`, `qt-cpp-review`.

- [x] **C2 — Asistente de enfoque.** *(La monotonía solo se cumple mientras la
  medida significa algo: por debajo del 0,1 % del pico repunta, y es residuo
  numérico, no un defecto. `computeQualityMetrics` se dejó intacta para no
  mover en silencio el umbral de aceptación del registro.)* El deslizador de enfoque se mueve hoy a
  ciegas. La métrica **ya existe** (`QualityMetrics::sharpness`), solo que se
  usa únicamente al validar capturas de registro.
  Enseñarla en vivo junto al control: barra de nitidez, **marca del máximo
  alcanzado** y botón de reinicio del pico — eso es lo que convierte "mover un
  deslizador" en "buscar el máximo".
  **Detalle que no es obvio:** la nitidez se mide **sobre el recorte de la
  pieza**, no sobre el frame entero. Con fondo texturizado o con la regla
  graduada encima, el Laplaciano del fondo domina y el número deja de hablar de
  la pieza. Sin pieza, se mide en el centro y se dice que es del encuadre.
  Verificación: sobre la misma imagen desenfocada progresivamente con
  `GaussianBlur`, la nitidez **decrece de forma monótona**; y con fondo
  texturizado alrededor de una pieza nítida, la medida en el ROI de la pieza es
  claramente mayor que la del frame entero — ese test es el que justifica el
  ROI.

- [x] **C3 — Zoom de trabajo automático (el ROI que sigue a la pieza).**
  *(Medido: 6× más rápido con la pieza al 7,9 % del área, y el fixture coincide
  con el del frame completo dentro de ±0,5 px. La pestaña «Rendimiento» nace
  aquí, como se anotó en `C1`.)* Es el
  "zoom automático para que el programa trabaje menos". Hoy cada frame se
  segmenta entero aunque la pieza ocupe un 6 %.
  **No hace falta mecanismo nuevo**: `PipelineConfig::roi` ya recorta y
  `analyzeFrame` ya devuelve coordenadas del marco completo. Falta quién
  calcula el rectángulo: nuevo `vision/auto_roi.{h,cpp}` (lógica pura, sin Qt):
  - De la envolvente de la pieza anterior, **expandida** un margen proporcional
    a su tamaño (para que la pieza pueda moverse entre frames) y **suavizada**
    para que no baile.
  - **Se rinde y vuelve al frame completo** si la pieza no aparece N frames
    seguidos, si toca el borde del ROI, o si su área cambia de golpe más de un
    X % (alguien cambió la pieza). Volver es barato; medir dentro de un
    rectángulo equivocado, no.
  - Conmutador en *Rendimiento* con tres estados: **apagado / automático / zona
    fija** (la ROI manual de hoy), y la zona activa **dibujada en el vídeo** —
    el operador tiene que ver el recorte o no sabrá por dónde empezar cuando
    algo falle.
  Verificación: (a) sobre una secuencia sintética de una pieza que se desplaza,
  el ROI la contiene siempre y el fixture **coincide** con el del frame
  completo dentro de ±0,5 px; (b) la pieza que desaparece provoca la vuelta al
  frame completo en ≤ N frames; (c) **rendimiento**: el mismo frame con y sin
  ROI, en el mismo proceso, exigiendo **≥ 2×** cuando la pieza ocupa menos de un
  tercio del ancho. Relativo y no en milisegundos absolutos: lo segundo depende
  de la máquina y convertiría el test en un generador de fallos intermitentes.

- [~] **C4 — Escala de trabajo adaptativa. NO SE ENTREGA: la premisa era
  falsa.** Se implementó y se midió. La precisión salió perfecta (fixture a
  ±0,000 px y cotas idénticas a 1/4, recuperando el contorno a resolución
  completa), pero la ganancia es **1,10×**, porque `segmentPiece` es solo el
  **23 %** del coste de un análisis. El reparto medido sobre 2560×1440:
  `computeFixture` **40 %**, `normalizePiece` **34 %**, segmentación 23 %,
  contorno 6 %. Un 10 % no paga un modo nuevo en la interfaz. Sustituido por
  `C4b`; el reparto queda documentado en ARQUITECTURA para no reintentarlo.

- [x] **C4b — Acelerar lo que de verdad cuesta: los momentos y el recorte.**
  *(Medido: análisis completo de 35,4 a 23,1 ms (1,53×); `computeFixture` 14,1
  → 5,2 ms y `normalizePiece` 12,1 → 5,7 ms. Exacto: el recorte canónico sale
  con 0 píxeles distintos frente a la implementación de referencia. Ahora el
  mayor coste es la segmentación, con el 34 %.)*
  Sale de medir `C4`. Dos objetivos, cada uno con el mismo criterio de entrega
  que tenía `C4` —**si mueve las medidas, no se entrega**—:
  - **`computeFixture` (40 %)** calcula los momentos sobre una máscara de 3,7
    millones de píxeles. Los mismos momentos salen del **polígono del contorno**
    (`cv::moments` sobre los puntos) en tiempo proporcional al número de puntos,
    que son unos miles. Ojo: los momentos de polígono y los de máscara no son
    idénticos en el borde (medio píxel), así que hay que medir la diferencia
    antes de cambiar nada, no suponerla.
  - **`normalizePiece` (34 %)** deforma la imagen entera para sacar un recorte
    de 256×256. Recortar primero por la envolvente de la pieza y deformar solo
    eso tiene que dar el mismo resultado por mucho menos.
  Verificación: sobre las mismas piezas sintéticas, el fixture y **todas** las
  cotas de una plantilla completa coinciden con las de hoy dentro de 0,05 px, y
  el recorte canónico es idéntico píxel a píxel. Más la ganancia relativa,
  medida como en `C3`. La
  otra mitad de "que trabaje menos". Con el ROI ya ajustado, si la pieza es
  **grande** el recorte sigue siendo enorme y el contorno de una pieza de
  1500 px se localiza igual de bien a media escala. Si es **pequeña**, no se
  reduce nada: ahí cada píxel cuenta.
  Regla: se elige el factor (1, ½, ¼) para que el **ancho de trabajo caiga en
  una banda objetivo** (~400-800 px); se segmenta reducido y el contorno y el
  fixture se devuelven **escalados de vuelta**.
  **La línea que no se cruza:** las herramientas miden **siempre** a resolución
  completa. El fixture sale de un contorno reducido y es algo menos fino, pero
  solo *coloca* las herramientas: cada una vuelve a buscar su borde con
  precisión subpíxel donde de verdad está.
  **Criterio de entrega:** si el test no demuestra que las medidas no se mueven,
  el ítem **no se entrega** y queda solo `C3`.
  Verificación: misma plantilla sobre la misma pieza sintética con factor 1 y
  con factor ½, exigiendo que cada medida coincida dentro de **0,3 px**; más la
  ganancia relativa como en `C3`.

- [x] **C5 — Cuántas piezas espero (y avisar cuando no cuadra).** *(Medido:
  seis piezas cuestan 1,62× lo que cuesta una, porque cada una se procesa
  dentro de su propia envolvente. Migración v8→v9 verificada sobre la BD real.
  El recuento solo se calcula cuando alguien va a mirarlo.)* Hoy la app
  asume una pieza —la mayor— y **borra el resto en silencio**: con una bandeja
  de seis tornillos, cinco tornillos y una bandeja casi vacía dan el mismo
  resultado.
  - `vision/pipeline.*`: nuevo `analyzeFrames()` que devuelve **todas** las
    piezas que pasan el filtro de área, ordenadas por tamaño; `analyzeFrame`
    pasa a ser "la primera de esas" y **no cambia de contrato**.
  - `PipelineConfig::expectedPieces` (1 por defecto) y, en la página *Piezas*,
    el número esperado más un "usar lo que veo ahora" que lo rellena.
  - **Es una inspección por sí sola**: si se encuentran más o menos de las
    esperadas, veredicto **NG** con el motivo ("esperaba 6, veo 5"), sin
    necesidad de ninguna herramienta dibujada.
  - Se guarda **por pieza**, no global: seis tornillos en bandeja es una
    propiedad del trabajo. `kMigrationV9` → `Pieces.expected_pieces INTEGER
    DEFAULT 1`.
  - En el vídeo, cada pieza detectada se numera y la que se mide va resaltada.
  Verificación: imagen sintética con 1, 3 y 6 piezas de distinto tamaño — se
  encuentran todas y en orden de área; una mota por debajo de `minAreaFraction`
  no cuenta; el veredicto por recuento salta cuando falta una y **no** salta
  cuando están todas; migración v8→v9 probada sobre una BD con piezas.

- [x] **C6 — Medir las N piezas con la misma plantilla.** *(Salió más barato de
  lo previsto: las herramientas ya viven en coordenadas de pieza, así que medir
  la siguiente es cambiar de fixture. Migración v10 para el índice de pieza en
  el historial, verificada sobre la BD real. Limitación anotada: el rasgo
  distintivo solo se aplica a la pieza principal.)* *(El más caro de `C`;
  puede quedarse fuera sin dejar `C5` cojo.)* Hoy
  `InspectionEngine::Outcome` es de **una** pieza.
  A favor: como las herramientas viven en **coordenadas de pieza**, medir la
  segunda es literalmente `runTools(imagen, fixtureDeLaSegunda, …)`. No hay que
  tocar ni una herramienta.
  En contra: hay que decidir qué es el veredicto de una bandeja (**la peor de
  las piezas**), qué guarda el historial (una fila por pieza, con su índice) y
  cómo se enseña sin llenar la pantalla (una fila por pieza en el panel, y en
  el vídeo solo las cotas de la pieza seleccionada — catorce etiquetas por seis
  piezas es ruido, no información).
  Verificación: bandeja sintética de 3 piezas iguales salvo una fuera de
  tolerancia → 3 juegos de resultados, veredicto NG, historial con las tres.

- [x] **C7 — El editor de plantilla usa los ajustes del operador.** *(El test
  se hizo con el umbral manual y no con la polaridad: sobre un histograma
  trimodal, dónde cae Otsu no es predecible y el test medía la suerte.)* Fallo real:
  `editor_window.cpp:472`, `:713` y `:735` llaman `analyzeFrame(image)` **sin
  `PipelineConfig`**, así que el editor detecta con Otsu, polaridad automática
  y sin zona, dé igual lo que el operador tenga puesto. Lo que se dibuja y lo
  que luego se inspecciona pueden no ser la misma pieza.
  Pasar la configuración en el constructor y usarla en las tres llamadas, **con
  test**, porque es justo el tipo de fallo que vuelve.
  Verificación: con una imagen que solo se segmenta bien con polaridad
  invertida, el editor encuentra la pieza al recibir esa configuración y no la
  encuentra con la de por defecto.

- [x] **C8 — Los ajustes que existen y no se pueden tocar, y el tablero
  duplicado.** *(Encontró un fallo real introducido en C5: `saveMeasurement`
  escribe la fila entera, así que cambiar el tablero borraba en silencio las
  piezas esperadas. Regla escrita: cargar antes de guardar.)* Dos deudas de coherencia que se cierran juntas:
  - `minAreaFraction` (0,005), `maxAreaFraction` (0,9) y `canonicalSize` (256)
    deciden qué es una pieza y **no tienen UI ni clave**. Con piezas pequeñas,
    el 0,5 % del área es la frontera entre "no hay pieza" y "hay pieza", y hoy
    no se puede mover sin recompilar. Van a *Detección*, con su valor por
    defecto marcado y un "restaurar".
  - El tablero está **duplicado**: claves globales `board_*` en `Settings` y
    columnas en `Pieces`. Al seleccionar pieza gana el de la pieza, pero
    tocarlo desde el menú *Ver* escribe el global. Decidir **una** regla,
    escribirla en ARQUITECTURA y hacer que el código la cumpla: el global es
    solo la **plantilla para piezas nuevas**; con una pieza seleccionada, todo
    cambio va a la pieza.
  Verificación: una fracción de área subida a mano rechaza la pieza pequeña que
  antes aceptaba; y cambiar el origen del tablero con una pieza seleccionada y
  volver a seleccionarla devuelve lo que se puso, no el global.

### R — Registro y paleta (andamio: antes de cualquier herramienta nueva)

- [x] **R1 — Familias como dato, no como orden de los botones.** *(Reparto
  final 3 / 7 / 0 / 1 / 3 = 14, con un barrido que exige que las familias sean
  una partición exacta de `allToolTypes()`.)* Nuevo
  `enum class ToolCategory { BasicShape, InLine, Construction, Gdt,
  TurnedAndExtremes }` junto a `ToolType`, con `categoryOf(ToolType)`,
  `categoryLabel`, `categoryDescription` y `toolsInCategory(category)`.
  Va en `tools/tool_geometry.*`, al lado de `allToolTypes()`, por la misma
  razón por la que esa lista existe: **una sola fuente de verdad**, o las dos
  superficies de UI acabarán agrupando distinto.
  Reparto de las catorce actuales:
  | Familia | Herramientas de hoy |
  |---|---|
  | **Figuras básicas** | Blob, Blob poligonal, Borde liso |
  | **Medición en línea** | Caliper, Círculo, Regla, Punto-Línea, Ángulo, Línea-Línea, Arco |
  | **Construcciones geométricas** | *(ninguna todavía)* |
  | **GD&T** | Posición |
  | **Máximos, mínimos y torneadas** | Eje/Diámetro, Rosca, Engranaje |
  Verificación: barrido sobre `allToolTypes()` exigiendo que **toda**
  herramienta tenga categoría, y que `toolsInCategory` de las cinco familias
  reconstruya exactamente `allToolTypes()` sin repetidos ni huecos. (Una
  familia vacía se permite mientras esté declarada: Construcciones se llena en
  `X`.)

- [x] **R2 — Paleta agrupada, en las dos superficies.** *(Medido: la paleta
  compacta pide 312 px frente a los ~1400 de la fila plana. Los atajos pasan a
  familia + dígito y se generan de las familias, con lo que Arco, Eje, Rosca y
  Engranaje tienen tecla por primera vez.)* La fila plana pide
  ~1400 px en una ventana de 1100 y el editor gasta 440 px de alto; con veinte
  herramientas más, las dos revientan.
  Nuevo widget **compartido** `inspection_editor/canvas/tool_palette.{h,cpp}`
  (vive en `pci_editor`, del que ya depende `pci_ui`, así que la dependencia
  baja), construido desde `toolsInCategory()`.
  - **Vista en vivo**: un botón por familia con menú desplegable, o una barra
    de familias compacta. Objetivo concreto y comprobable: **la fila cabe en
    1100 px**.
  - **Editor**: `QToolBox` (acordeón) con una sección por familia, recordando
    la última abierta.
  - **Atajos**: se acabó el dígito por herramienta. `Ctrl+1..5` elige familia y
    `1..9` la herramienta dentro; la tabla escrita a mano `toolKeys[]`
    desaparece y se genera de `toolsInCategory()`. Arregla de paso que Arco,
    Eje, Rosca y Engranaje **no tengan tecla hoy**.
  Verificación: prueba de gesto que construye la paleta y comprueba que
  **toda** herramienta de `allToolTypes()` es alcanzable con clics (ninguna
  escondida), que el ancho pedido baja del umbral, y que familia + dígito
  activa la herramienta correcta.
  Skills: `qt-ui-design`.

- [x] **R3 — Que olvidarse deje de compilar.** *(Se comprobó con un tipo sonda
  en vez de dar por buena la cuenta del inventario: las trampas silenciosas
  eran **dos** —`distanceToGeometry` y `paintTool`—, no cuatro. Y el
  experimento destapó un fallo ya entregado: Eje, Rosca y Engranaje no tenían
  rama de dibujo desde T2–T4.)* Los cuatro visitors de
  `canvas/canvas_geometry.cpp` y el `paintTool` de `canvas/editor_canvas.cpp`
  recorren la variante con cadenas de `if constexpr` **sin cierre**: una
  herramienta nueva que no se añada ahí compila y se queda muda.
  Cerrar las cinco cadenas con `static_assert(alwaysFalse<T>)`, igual que ya
  están `typeOf`, `translateGeometry` y `toJson`. Y añadir un barrido que
  ejecute `runTool` con cada tipo sobre una escena sintética exigiendo que
  **ninguno** devuelva "tipo no soportado".
  Con esto, de las ~22 ediciones por herramienta nueva quedan **cero
  silenciosas**: o compila y pasa los barridos, o la herramienta está completa.
  Verificación: el barrido, más la lista de puntos de edición documentada en
  ARQUITECTURA con qué mecanismo protege cada uno.

### X — Construcciones geométricas (habilitan el GD&T)

- [x] **X0 — Elementos derivados y referencias entre herramientas.** *(La
  referencia se guarda en `paramsJson`, que existía sin usarse: no hizo falta
  migrar el esquema. Falta el desplegable de referencia en el panel y la flecha
  de dependencia en el lienzo, que van con `X1`, cuando haya herramientas que
  de verdad la usen.)* El
  mecanismo del que cuelgan `X` y `G` enteras (ver decisión de diseño 2).
  - `ToolConfig` gana `reference` (nombre de otra herramienta) y, donde haga
    falta, `reference2` para un marco de dos datums.
  - Una herramienta puede devolver, además de su medida, un **elemento
    geométrico** (`DerivedElement`: punto, recta o círculo, en coordenadas de
    pieza). Las que ya existen lo llenan casi gratis: el Círculo ya conoce su
    centro y radio, el Borde liso su recta ajustada, la Regla sus dos puntos.
  - `runTools` pasa a **dos pasadas** con **detección de ciclos**: si A
    referencia a B y B a A, las dos fallan con ese motivo en vez de colgarse.
  - Referencia ausente, con nombre cambiado o fallida ⇒ **la herramienta no
    mide** y lo dice. Nunca cae a una referencia implícita: un GD&T medido
    contra otro datum del que cree el operador es exactamente el fallo que este
    programa existe para evitar.
  - En la UI, el panel de la herramienta gana un desplegable "Referencia" con
    las herramientas compatibles ya dibujadas; y el lienzo **dibuja la flecha**
    de quién depende de quién al seleccionarla.
  Verificación: cadena A→B→C que mide bien; ciclo detectado; referencia
  inexistente ⇒ NG con motivo; renombrar la herramienta referenciada actualiza
  a los que la usan (o los deja fallando con un mensaje claro — decidirlo y
  probarlo, no dejarlo al azar); ida y vuelta de las referencias por la
  plantilla JSON.

- [x] **X1 — Punto y recta construidos.** *(Ocho construcciones, no nueve:
  «bisectriz» y «recta media» resultaron ser la MISMA —cuando las rectas se
  cortan la bisectriz pasa por el corte, cuando son paralelas pasa por el punto
  medio entre ellas, que es la recta media—, así que son una sola y no un caso
  especial esquivado. Un test que exigía que la bisectriz no dependiera del
  sentido del trazo **falló y destapó algo real**: con dos rectas
  perpendiculares salía 45° o 135° según cómo se hubieran arrastrado. Las dos
  son válidas —a 90° no hay ángulo agudo que partir— pero elegir sola no lo es,
  así que las direcciones se llevan a forma canónica antes de bisecar. También
  entra aquí el desplegable de referencia que `X0` dejó pendiente, ahora por
  duplicado porque `ToolConfig` gana `reference2`; la flecha de dependencia en
  el lienzo sigue sin hacerse y va con `X2`. Y `ToolRunResult::informative`: una
  construcción que sale bien escribe «—» y no un OK verde, porque no ha juzgado
  nada.)* La categoría `Geometry Tools` de
  Cognex en dos herramientas, que es lo que hace falta para tener datums:
  - **Punto construido**: punto medio de dos referencias, intersección de dos
    rectas, proyección de un punto sobre una recta, o centro de un círculo.
  - **Recta construida**: por dos puntos, bisectriz de dos rectas, paralela o
    perpendicular a una recta por un punto, o recta media entre dos rectas.
  No miden nada por sí solas (su "medida" es informativa: coordenadas o
  ángulo); existen para ser referenciadas. Es trigonometría sobre primitivas
  que ya se ajustan: **cero algoritmo nuevo**.
  Verificación: cada construcción contra su valor analítico sobre entradas
  conocidas; casos degenerados (rectas paralelas que no se cortan, dos puntos
  coincidentes) fallan con motivo y **no** devuelven NaN.

- [x] **X2 — Eje medio de la silueta.** *(Salió tal cual estaba planeado, que
  con lo medido hasta ahora es la excepción: reutiliza entero el perfil axial
  del Eje torneado y `fitLineRobust`, y los cinco tests pasaron a la primera. La
  verificación del trazo descentrado se hizo con tres alturas (centrado, +25 px
  y −22 px) en vez de una, y las tres caen dentro de ±0,3 px. Se añadió una
  regla que el plan no pedía: si en un corte solo se ve UN flanco, ese corte no
  cuenta —suponer el centro por simetría sería inventárselo justo en la
  herramienta que existe para encontrarlo—, y con menos de cinco cortes buenos
  no mide y dice cuántos vio. Entra aquí también la **flecha de dependencia en
  el lienzo** que arrastraban `X0` y `X1`; una referencia rota no dibuja
  flecha, porque una flecha hacia la nada haría creer que el datum existe.)* La
  línea media entre los dos flancos de
  una pieza alargada. Es el datum natural de una pieza torneada y el sustituto
  honesto de la simetría retirada de la norma.
  Algoritmo: emparejar puntos opuestos por la normal (reutiliza el perfil axial
  que ya genera el Eje), calcular los puntos medios y ajustarles una recta
  robusta (`fitLineRobust`, ya existe). De ahí salen la **rectitud del eje** y
  la **coaxialidad de tramos de distinto diámetro**.
  Verificación: sobre un eje sintético recto, el eje medio coincide con el
  dibujado dentro de ±0,3 px aunque el trazo del operador vaya descentrado;
  sobre uno con dos tramos desalineados, la desalineación medida es la
  dibujada.

### F — Figuras básicas

*(Ya dentro: Blob, Blob poligonal, Borde liso.)*

- [x] **F1 — Región: área, perímetro y compañía.** *(La trampa de la
  circularidad se resolvió por una vía que el plan no contemplaba y que resultó
  mejor que las dos que proponía. Medí las dos: suavizar el contorno sigue
  dependiendo del tamaño (0,98 a r=15 frente a 0,9998 a r=240) y deforma las
  esquinas de verdad; normalizar la escala es imposible porque el sesgo depende
  de la ORIENTACIÓN de cada tramo —un cuadrado alineado ya sale exacto y un
  círculo se va un 5 %—. Al medirlo apareció algo peor de lo avisado: el mismo
  cuadrado leído un 7,7 % más largo solo por estar girado 30°. Se arregló en la
  raíz cambiando el estimador de perímetro (`vision::digitalPerimeter`,
  Vossepoel–Smeulders, commit aparte), y con él la circularidad de un círculo
  digital sale **0,993** y la de un cuadrado **0,819**: la escala ya significa
  lo que dice y no hace falta ninguna corrección cosmética. Se añadió una
  sobrecarga de `suggestTolerances` que mira la geometría, porque una banda de
  ±10 % vale para un área y no para una circularidad (0..1) ni para un recuento
  de agujeros (exacto). Y una mota de ruido no cuenta como agujero.)* Una sola herramienta con un
  **selector de medida** —igual que Posición tiene su selector de eje— sobre la
  pieza entera o sobre una región dibujada: **área**, **perímetro**,
  **solidez** (área / área del casco convexo), **circularidad de forma**
  (4πA/P²), **relación de aspecto** (del rectángulo mínimo) y **número de
  agujeros**. Una herramienta y no seis, porque cada instancia lleva su propia
  tolerancia y así el operador pone solo las que le importan.
  Todo tiene función directa en OpenCV (`contourArea`, `arcLength`,
  `convexHull`, `minAreaRect`, jerarquía de `findContours`).
  **Dos trampas medidas que hay que documentar en el código:** el perímetro de
  una máscara **sale alto** por la escalera de píxeles (ya se vio en la ronda
  anterior: el contorno pasa por el centro de los píxeles del borde), y por eso
  **4πA/P² da ~0,90 sobre un círculo digital perfecto, no 1,0**. O se suaviza
  el contorno antes de medir, o se normaliza la escala de circularidad — pero
  no se puede entregar un "1,0 = círculo perfecto" que nunca se alcanza.
  Verificación: figuras sintéticas de área y perímetro conocidos; la
  circularidad de un círculo dibujado tiene que dar el valor que el test
  declare como referencia (y ese valor, con su explicación, en el comentario).

- [x] **F2 — Simetría.** *(Se barre el ángulo ENTERO en vez de sembrar con el
  eje principal de inercia como decía el plan. El eje de simetría de una figura
  simétrica sí es un eje principal, así que sembrar sería correcto… salvo
  cuando la nube es casi redonda, que es cuando ese eje es ruido — y es justo
  la figura en la que uno querría fiarse. Barrer entero quita el caso especial;
  costaba 26 ms, y haciendo el barrido grueso sobre una copia reducida a 160 px
  baja a 14 sin mover el resultado (el afinado sigue a resolución completa,
  porque perder píxeles del borde INFLA la simetría: los detalles que la rompen
  son los primeros en desaparecer). Aun así es la herramienta más cara y eso
  está dicho en el README. Medido: rectángulo 1,00 en los dos ejes, L 0,69,
  círculo 1,00, y el recorte de esquina baja 1,000 → 0,956 → 0,858 → 0,809. El
  eje encontrado se ofrece como referencia. Dos fallos propios cazados por los
  barridos de coherencia: el `case` nuevo se coló entre `MedianAxis` y
  `EdgeFlaw` y le recortaba a 1 la rectitud del eje; y el barrido de tolerancias
  probaba la simetría con valores de 137, que una fracción no puede dar — se
  arregló declarando `measuresFraction` en el modelo, no relajando el test.)* Lo que pidió el usuario, y honesto **como descriptor
  de forma**, no como tolerancia GD&T (la de la norma está retirada).
  Devuelve el **ángulo del mejor eje de simetría** y un **grado de simetría
  0..1**. Algoritmo: semilla con el eje principal de inercia
  (`0,5·atan2(2μ₁₁, μ₂₀−μ₀₂)`, directo de `cv::moments`), luego barrido fino
  del ángulo reflejando el contorno y midiendo el solape (IoU) o la distancia
  de Chamfer contra el original.
  Sirve para lo que de verdad se usa: detectar una pieza montada del revés o
  con un rasgo asimétrico que no debería estar.
  Verificación: un rectángulo da 1,0 en dos ejes ortogonales; una pieza en L da
  claramente menos; recortarle una esquina a una pieza simétrica **baja** el
  grado de forma monótona con el tamaño del recorte.

- [x] **F3 — Lados y polígono.** *(Lo que costó pensar no fue contar lados sino
  decidir CUÁNDO ese número significa algo. El plan pedía que "un círculo no se
  dé por polígono" y la solución salió gratis, sin ningún umbral de curvatura
  inventado: sobre un polígono de verdad el recuento aguanta al cambiar la
  tolerancia, y sobre una curva cada tolerancia da otro número. Se aproxima con
  epsilon, con la mitad y con el doble, y solo se publica el recuento si los
  tres coinciden; si no, se dice cuántos salen con cada uno. Un círculo da
  8/12/8 y queda rechazado. Medido: hexágono de radio 140 → 6 lados, lado
  139,8–140,0 px, ángulos 119,90–120,05°; octógono con ángulos de 135,00°
  clavados; y el mismo hexágono a radios 50, 100 y 180 da 6 lados siempre, que
  es el test que justifica el epsilon relativo.)* Número de lados, longitud de cada uno y
  ángulos interiores, con `approxPolyDP`. El caso de uso obvio es el hexágono
  de una tuerca o un perfil poligonal.
  **El parámetro que lo decide todo es `epsilon`**, y por eso se expresa como
  **fracción del perímetro** y no en píxeles: en píxeles, la misma pieza a otra
  distancia da otro número de lados.
  Verificación: polígonos sintéticos de 3, 4, 6 y 8 lados dan su número exacto
  en un rango amplio de epsilon; un círculo **no** se da por polígono; el
  número de lados de un hexágono no cambia al escalar la imagen — ese test es
  el que justifica que epsilon sea relativo.

- [x] **F4 — Rebaba y mella: contar eventos, no dar un número.** *(Dos cosas
  que no estaban en el plan y salieron al hacerlo. La primera: la recta base se
  ajusta de forma ROBUSTA y no por mínimos cuadrados, porque una rebaba grande
  arrastra el ajuste clásico y reparte su altura entre ella y el resto del
  borde — el defecto sale más pequeño de lo que es y el borde sano parece
  torcido. La segunda la destapó un test que falló: una rebaba de 20 px con una
  ventana de escaneo de 30 se sale de la ventana, esos escaneos no encuentran
  borde, se saltaban, y la herramienta respondía «sin defectos». Un OK rotundo
  sobre el tramo donde estaba el defecto más gordo, que es el peor error que
  podía cometer. Ahora se cuentan los escaneos SEGUIDOS sin borde —uno o dos son
  ruido, tres ya son un tramo— y se avisa de que hay que subir el largo de
  escaneo. Medido: alturas de 3, 5, 6, 8, 9, 12 y 20 px se leen exactas, y el
  signo distingue rebaba de mella mirando de qué lado está el material en la
  imagen, no suponiendo hacia dónde se trazó la línea.)* Distinta del
  Borde liso, que devuelve *una* desviación máxima. Aquí se detectan, cuentan y
  miden **los defectos por separado**: residuo respecto al elemento ajustado
  (recta, círculo o el contorno esperado) → umbral → agrupación por
  conectividad → **altura y extensión de cada evento**, y su signo (rebaba
  hacia fuera / mella hacia dentro).
  Un borde con una mella de 0,5 mm y otro con veinte de 0,1 mm dan hoy la misma
  lectura y no son la misma pieza.
  Verificación: contorno sintético con 1, 3 y 0 defectos de tamaño conocido —
  se cuentan exactos, la altura de cada uno coincide con la dibujada, y el
  signo distingue rebaba de mella.

### L — Medición en línea

*(Ya dentro: Caliper, Círculo, Regla, Punto-Línea, Ángulo, Línea-Línea, Arco —
diámetro, radio, punto a punto y ángulos ya estaban cubiertos.)*

- [x] **L1 — Distancia mínima entre dos elementos.** *(Dos de las tres
  verificaciones salen exactas: dos círculos separados 10, 25 y 60 px se miden
  en 10,0, 25,0 y 60,0. La tercera —"solapados dan negativo (interferencia)"—
  **no se puede entregar, y no por falta de ganas**: dos contornos externos de
  una misma binarización jamás se solapan, porque en cuanto dos piezas se tocan
  la silueta las une y llega UNA figura. Escribí la rama de interferencia, vi
  que era inalcanzable y la quité en vez de dejarla de adorno; en su lugar, el
  caso de una sola figura dice "puede que se estén TOCANDO", que es justo el
  dato que el operador necesita. Cuánto se meten dos piezas la una en la otra no
  es una medida que contenga una imagen de siluetas. Se dibuja UN recuadro y no
  dos: el gesto de "mide la holgura de aquí" es uno solo. Y hay un test que
  compara con lo que daría un calíper desviado del punto más estrecho —25 px
  frente a 52— porque si fueran parecidos la herramienta no aportaría nada.)* La holgura: la separación
  **más corta** entre dos bordes, dos contornos o un contorno y una recta, que
  no es la que da un calíper (el calíper mide donde el operador cruzó, no donde
  la pieza está más apretada).
  Algoritmo: `cv::pointPolygonTest` con `measureDist=true` de un elemento
  contra el otro, o distancia punto-segmento por parejas con poda por
  envolvente. Devuelve la distancia y **dónde** ocurre, dibujada en el lienzo —
  sin el punto de contacto, un mínimo no se puede verificar a ojo.
  Verificación: dos círculos sintéticos separados una distancia conocida; dos
  contornos que se tocan dan 0; solapados dan negativo (interferencia) y lo
  dicen.

### G — GD&T

*(Ya dentro: Posición.)* Ninguna empieza antes de `X0`. Todas llevan en su
descripción la nota de la óptica, y la tabla de "lo que no se puede medir"
tiene que estar accesible desde la familia.

- [x] **G1 — Rectitud por zona mínima.** *(El ítem pedía "hacer visible la
  diferencia con números" y ahí están: sobre el mismo borde, 10,33 px de
  rectitud de la norma frente a 5,76 px del Borde liso. La comparación honesta
  no es esa, y conviene decirlo: el Borde liso da MEDIA banda (desviación
  máxima respecto a la recta media), así que el número sube al cambiar de
  herramienta sin que la pieza empeore. Eso está avisado en la descripción de la
  propia herramienta, porque es el malentendido que va a tener el operador.
  La trampa de `minAreaRect` no se dejó solo escrita: se buscó a propósito, con
  un barrido de polígonos al azar, una figura donde los dos criterios discrepen
  —en la mayoría coinciden y un test con una cualquiera no demostraría nada—, y
  el triángulo que salió da 97,9 de anchura mínima frente a 138,0 de lado corto
  de `minAreaRect`: un 41 % de rectitud inflada si se usara. La banda mínima
  vive en `vision::minimumZoneBand` y la usará también `G3`.
  Un test falló por un epsilon absoluto de 1e-6 donde `minAreaRect` trabaja en
  float y la función en double; se pasó a tolerancia relativa, que es lo que
  correspondía — el test estaba comprobando algo cierto.)* El valor de la norma es la **anchura
  de la banda más estrecha de dos rectas paralelas que contiene todos los
  puntos** — no la desviación respecto a la recta de mínimos cuadrados, que es
  lo que da hoy el Borde liso y siempre sale menor.
  Algoritmo: `cv::convexHull` + **rotating calipers**, barriendo las
  orientaciones apoyadas en cada arista del casco; el mínimo se alcanza siempre
  en una de ellas.
  **Trampa que hay que evitar y dejar escrita:** `cv::minAreaRect` minimiza el
  **área**, no la anchura. No sirve directamente.
  **Y el límite:** la rectitud de un *eje* solo se puede dar en el plano de la
  imagen; la componente perpendicular a la cámara es invisible. Se declara como
  "rectitud del eje proyectado", no a secas.
  Verificación: banda sintética de anchura conocida — el valor coincide;
  comparación explícita contra el número LSQ del Borde liso sobre los mismos
  puntos, con los dos impresos en el test (**este ítem tiene que hacer visible
  la diferencia con números**, no solo pasar).

- [x] **G2 — Redondez por zona mínima (MZC).** *(Nelder-Mead sobre el centro,
  sembrado con Taubin, en `vision::minimumZoneCircle`. Verificado con una elipse
  de 100×94, cuya redondez es exactamente 6: sale **6,000**. Sobre perfiles
  SIMÉTRICOS —tres lóbulos iguales— el centro de mínimos cuadrados ya es el
  óptimo y los dos números coinciden, lo que puede hacer pensar que la
  minimización no hace nada; por eso hay un test con una protuberancia
  asimétrica donde sí se separan: 6,355 frente a 7,175 (un 12,9 % mejor) con el
  centro corrido 1,48 px. Se corrigió además algo que ya estaba mal en la app:
  el Círculo llamaba «redondez» a `max|r−R|`, que es media banda y otro número
  — ahora dice «desv. radial máx.» y para la cota del plano está esta
  herramienta (10,1 frente a 5,1 sobre el mismo disco). Y es más exigente que
  el Círculo con el borde: exige el 80 % de los rayos en vez del 60 %, porque
  un diámetro se saca de medio contorno pero la redondez es la FORMA, y con un
  trozo sin ver el círculo interior se apoya donde le da la gana y el número
  sale bonito.)* La redondez ISO es el mínimo,
  sobre todos los centros posibles, de `Rmax − Rmin`: dos círculos concéntricos
  de separación radial mínima que contienen el perfil. Lo que la app da hoy
  (`tool_executor.cpp:382`) es `max |rᵢ − R_LSQ|`, otro número.
  Algoritmo: semilla con el ajuste Taubin que ya existe, y minimización de
  `max‖p−c‖ − min‖p−c‖` sobre el centro con Nelder-Mead. **Se reportan los
  dos**: MZC porque es el del plano, y LSC porque es el que dan la mayoría de
  las máquinas de medir y el operador va a comparar.
  **Límite que hay que decir en la propia herramienta:** solo es honesta si el
  elemento circular se ve **de frente**. La silueta de un cilindro visto de
  perfil no es un círculo: son dos tangentes, y ahí no hay redondez que medir.
  Verificación: círculo sintético con una deformación conocida — el MZC
  coincide con lo dibujado y **es ≥** que la desviación LSC; los dos números en
  el test.

- [x] **G3 — Orientación respecto a un datum: paralelismo, perpendicularidad y
  angularidad.** *(Una sola herramienta con campo de ángulo, y el datum viene por
  la referencia de `X0` — que es exactamente para lo que se construyó. El test
  que pedía el plan («comprobar que NO se está devolviendo el ángulo») está, y
  el que de verdad lo demuestra es otro: un borde ondulado que va a 0,41° del
  datum —paralelo de media— y necesita 12 px de banda. Si la herramienta
  devolviera el ángulo, ese borde pasaría. Se añadió una relación que ayuda a
  entender el bloque: la orientación nunca es menor que la rectitud del mismo
  borde (medido, 11,00 frente a 8,35), porque la rectitud elige la orientación
  de su banda y aquí la impone el datum. Dos correcciones propias: el mensaje de
  «falta la referencia» se reescribe en el idioma de la herramienta —quien pone
  un paralelismo busca la palabra DATUM—, y el test del nominal a 90° estaba
  girando datum y elemento a la vez, que es invariancia al giro y no lo que
  pedía el plan; ahora el datum se queda horizontal, el elemento va vertical y
  el nominal a 90.)* Son **la misma medida** con distinto ángulo nominal (0°, 90° o
  el que se ponga), así que son **una sola herramienta** con un campo de
  ángulo, no tres.
  **El error que hay que no cometer:** paralelismo **no es el ángulo entre dos
  rectas**. Es una **distancia**: la anchura de la banda, paralela al datum,
  que contiene los puntos del elemento tolerado. Ajustar el datum con mínimos
  cuadrados totales (`fitLineTotal`, ya existe), proyectar los puntos sobre la
  normal del datum y tomar `max(d) − min(d)`.
  Verificación: dos bordes sintéticos paralelos con una divergencia conocida —
  el valor es la anchura de banda esperada; el mismo par a 90° con el nominal
  puesto a 90° da lo mismo; y un test que compruebe explícitamente que **no** se
  está devolviendo el ángulo.

- [x] **G4 — Posición verdadera con marco de referencia.** *(Se amplió la
  Posición, no se duplicó, y hay un test que lo fija: sin datums el detalle
  sigue siendo el de siempre (dx/dy/r/ángulo) y no aparece ningún diámetro de
  zona. Con datums, Ø = 2·√(dx²+dy²) medido en el marco, y girando la pieza
  entera por 0°, 17°, 45°, 90° y 143° sale Ø10,000 en los cinco — que es la
  comprobación que pedía el plan y la que demuestra que el marco es un marco.
  El datum secundario admite recta (origen = intersección) o punto (origen =
  ese punto proyectado sobre la primaria), porque un agujero como secundario es
  lo normal en una brida. Los marcos a medias no miden: sin secundario no hay
  origen, y dos datums paralelos no se cortan.
  De paso hubo que subir los helpers geométricos de `X1` —`operand`,
  `intersectLines`, `projectOnLine`— a la zona de helpers generales del archivo:
  ya los usan las construcciones, `G3` y `G4`, y la Posición se ejecuta antes en
  el archivo. Subirlos era lo correcto; ya no pertenecen a una herramienta sino
  al mecanismo de referencias.)* La herramienta
  Posición de hoy mide contra el cero del tablero. La de la norma mide contra
  un **marco de referencia (DRF)** construido con datums, y se expresa como
  **diámetro de zona**: `P = 2·√(dx² + dy²)`.
  Se amplía la Posición existente (no se duplica): gana la referencia de `X0`
  para el datum primario y el secundario, el punto teórico nominal, y la
  salida en diámetro. Sin referencias, se comporta como hoy.
  **Honesta en 2D si y solo si los datums del marco son resolubles en el plano
  de la imagen**; si el datum es una cara perpendicular a la cámara, no se
  puede, y hay que decirlo en vez de medir otra cosa.
  Verificación: agujero sintético desplazado un vector conocido respecto a un
  marco de dos bordes — el diámetro de posición es `2·√(dx²+dy²)`; girar la
  pieza entera **no** cambia el valor (todo se mide en el marco).

- [x] **G5 — Desviación de centros (lo que NO es concentricidad).** *(No mira la
  imagen: los dos centros los aportan otras herramientas por referencia, así que
  admite círculos y también puntos construidos —el punto medio de dos agujeros
  contra un tercero sale gratis—. El test sobre el TEXTO que pedía el plan está,
  y comprueba tres cosas: que la descripción dice NO ES CONCENTRICIDAD, que
  manda a Posición verdadera para la cota formal y que explica el porqué (2018);
  además, ni la etiqueta ni el resultado usan esa palabra.
  Mi primer fixture de test estaba mal y lo dice el código: dos discos de radio
  40 con los centros a 10 px se funden en UNA mancha, y los dos ajustes
  encontraban el mismo borde (0,2 px en vez de 10). El caso real de esta
  pregunta es un casquillo —agujero descentrado respecto al exterior—, y ahí
  sale dx=6,00, dy=8,00 → 10,0 exacto.)* La
  concentricidad y la simetría normativas están **retiradas de ASME Y14.5-2018**
  por inverificables de forma repetible. Pero la pregunta del operador
  —"¿están estos dos círculos centrados uno con otro?"— es legítima.
  Se responde con lo que sí se puede medir: la **distancia entre los centros**
  de dos elementos circulares vistos de frente, con ese nombre exacto. Cognex
  hace justo esto y define su herramienta con ese cuidado; se copia la
  honestidad de la definición, no el nombre del símbolo.
  En la descripción, la frase que evita el malentendido: *"esto no es
  concentricidad ISO/ASME; para la cota formal usa Posición verdadera"*.
  Verificación: dos círculos con un descentrado conocido; la herramienta lo
  devuelve y su descripción contiene la advertencia (test sobre el texto, como
  el de la ronda anterior).

- [x] **G6 — Patrón de agujeros (bolt circle).** *(Cero algoritmo nuevo, como
  decía el plan: agujeros de la jerarquía de contornos, centro de cada uno con
  el ajuste de círculo que ya estaba, y primitivo ajustando otro círculo a esos
  centros. Medido sobre una brida de 6 agujeros en un primitivo de Ø280: sale
  279,7 y paso 60,00°. Con un agujero movido 9 px, la desviación salta de 0,3 a
  13,8 px de diámetro de zona y el resto sigue en su sitio.
  Dos decisiones que no estaban en el plan. La fase del reparto se ajusta con la
  MEDIA CIRCULAR de los restos —con la aritmética, un resto de 359° y otro de 1°
  darían 180°—, y sin ajustarla una brida perfecta pero girada saldría entera
  fuera de tolerancia; hay un test que la gira 0, 7, 23 y 41° y no cambia nada.
  Y el peor agujero se identifica por su ÁNGULO y no por un índice: el orden es
  el del barrido angular y en la pieza no hay ningún número escrito, así que
  «el nº 5» no le sirve de nada a quien está mirando la brida. Lo vi porque el
  test desplazaba el índice 2 y la herramienta decía «nº 5» — las dos cosas
  correctas y la respuesta inútil.)* Ø del círculo primitivo, paso
  angular y **posición verdadera de cada agujero** respecto al marco. Es la
  cota de una brida y se compone de piezas que ya existen: los agujeros salen
  de la jerarquía de `findContours` (ya está en `findHoles`), el centro de cada
  uno por ajuste de círculo, el primitivo por ajuste de círculo a los centros y
  la posición por `G4`.
  Verificación: brida sintética de 6 agujeros en un círculo conocido — Ø
  primitivo y paso correctos; con un agujero desplazado, **solo ese** sale
  fuera de tolerancia.

- [x] **G7 — Perfil de línea contra un nominal DXF.** *(Se entrega la mitad que
  el propio plan preveía como alternativa: el nominal es el CONTORNO DE LA PIEZA
  BUENA, capturado al crear la herramienta y guardado dentro de la plantilla.
  Sin parser de DXF, que es lo que hacía caro el ítem.
  Y una cosa que el plan pedía y **resultó no hacer falta: el ICP**. Los dos
  contornos están en coordenadas de PIEZA y el Position Fixture ya los alineó;
  meter un ajuste encima sería alinear dos veces y, peor, dejaría que el ajuste
  se comiera una desviación real girando el nominal para que encajara. Hay un
  test con el fixture girado 30° que lo comprueba: perfil limpio.
  Medido: bulto de 8 px → zona 16,6 y «sobra 8,3»; pieza 6 px más pequeña →
  «falta 6,8, sobra 0,0». El signo distingue material de más de material de
  menos, que son dos averías distintas.
  Dos fallos propios cazados por los barridos de coherencia. Le dejé el nominal
  sin manijas —razonando que arrastrar sus puntos sería inventárselo— y el
  barrido recordó que sin manijas la herramienta no se puede ajustar; ahora
  tiene DOS agarres del objeto rígido, que además le dan al lienzo una medida de
  su tamaño. Y al ponerlas, trasladaba siempre por el centroide, así que
  re-agarrar la segunda y soltarla en su sitio desplazaba la pieza 60 px: el
  barrido exige que re-agarrar no mueva nada, y tenía razón.)* *(El más caro del
  backlog; va el último y puede quedarse fuera.)* Es la tolerancia GD&T **más
  honesta que existe para una silueta**, porque está definida sobre un elemento
  lineal y no sobre una superficie.
  Cargar la polilínea del DXF, alinear con ICP rígido (o anclar al marco si hay
  datums), y por cada punto del contorno la distancia con signo a la polilínea
  (`pointPolygonTest` o KD-tree contra los segmentos). Valor = `2·max|d|` para
  zona bilateral simétrica.
  **Se reportan desviaciones 2D en el plano**, no normales a superficie — es lo
  mismo que advierte PC-DMIS para sus features de visión, y va escrito en el
  informe.
  Si el coste del parser DXF se dispara, se entrega la mitad útil: perfil
  contra **el contorno de la pieza de referencia registrada**, que ya está en
  la base de datos y no necesita parser ninguno.
  Verificación: silueta sintética contra su propio nominal desplazado y
  deformado en una cantidad conocida.

### M — Máximos, mínimos y piezas torneadas

*(Ya dentro: Eje/Diámetro, Rosca, Engranaje.)*

- [x] **M1 — Anchura mínima y máxima de la silueta.** *(La anchura mínima
  reutiliza `minimumZoneBand` de `G1`; el diámetro máximo es nuevo
  (`vision::maximumSpan`) y se resuelve recorriendo los pares del casco convexo,
  que con unas decenas de vértices es trivial y evita la maquinaria de antípodas
  para ganar un tiempo que aquí no se nota. Rectángulo de 200×80 girado 30°:
  anchura 81,5 y diámetro 216,5 (teóricos 80 y 215,4), y girándolo por 0, 17, 30
  y 65° la anchura no se mueve — que es la razón de ser de la herramienta.
  El test de que `minAreaRect` no vale para el DIÁMETRO costó un intento: sobre
  el triángulo alargado que ya servía para la anchura, la diagonal se quedaba a
  0,4 px del diámetro real y no demostraba nada. Hace falta una figura COMPACTA,
  y en un equilátero de lado 150 la diagonal marca 198, un 32 % de más.)* "Lo de max y mini": la
  medida más grande y la más pequeña de la pieza **en cualquier dirección**, no
  en la que el operador acertó a trazar. Es la cota de "¿pasa por la ranura?".
  Algoritmo: `cv::convexHull` + rotating calipers → anchura mínima y su
  dirección, y diámetro máximo (el par de puntos más separados) y la suya.
  **Misma trampa que en `G1`:** `minAreaRect` minimiza área, no anchura, y su
  lado corto **no** es la anchura mínima.
  Verificación: rectángulo girado 30° — la anchura mínima es su lado corto y la
  máxima su diagonal, con las direcciones correctas; y un test que enseñe que
  el lado corto de `minAreaRect` **difiere** en una figura donde difiere (por
  eso hay implementación propia).

- [x] **M2 — Chaflán.** *(«Reutiliza íntegramente Línea-Línea», decía el plan, y
  el álgebra sí; lo que costó fue todo lo demás, y salió de tests que fallaban.
  Tres cosas.
  Primera: el recuadro RECORTABA la pieza, así que los cortes del propio
  recuadro se convertían en tramos rectos y el chaflán se medía contra un borde
  que no existe. Ahora el recuadro SELECCIONA qué tramos se miran y no corta
  nada.
  Segunda: «cara A» y «cara B» eran las que `findContours` recorriera primero,
  detalle interno que el operador no puede predecir — con un chaflán asimétrico
  los catetos salían intercambiados. Se ordenan por tamaño, y cada uno va con el
  ángulo del bisel respecto a SU cara, porque un chaflán no tiene un ángulo sino
  uno con cada cara y el plano acota desde una de las dos.
  Tercera: `decomposeContour` clasificaba como ARCO un bisel recto de 68 px —en
  esa longitud una circunferencia grande lo explica un pelo mejor por el dentado
  de la rasterización— y la herramienta decía no ver tres rectas. Se le sube el
  listón del arco a 45° de barrido con las opciones que ya existían: un acuerdo
  de verdad barre mucho más, así que eso no confunde un chaflán con un redondeo,
  es lo que los separa.
  Medido: 44,93°, 30,34° y 59,68° sobre chaflanes dibujados de 45, 30 y 60; y un
  asimétrico de 80×30 da 77,5 y 29,2 con 20,58°.)* Ángulo del chaflán y sus **dos catetos**: después del
  diámetro, la cota más pedida en pieza mecanizada.
  Algoritmo: segmentar el contorno por curvatura (ya existe
  `decomposeContour`), ajustar recta robusta al tramo del chaflán y a las dos
  caras adyacentes, e intersecar. Reutiliza íntegramente Línea-Línea.
  Verificación: chaflanes sintéticos de 30°, 45° y 60° con catetos conocidos.

- [x] **M3 — Radio de acuerdo (fillet) con comprobación de tangencia.** *(«Casi
  gratis con lo que hay» y esta vez sí lo fue: `decomposeContour` da el arco y
  su radio, y la tangente en un extremo es perpendicular al radio ahí. Radios
  medidos 29,6 / 50,5 / 79,0 sobre dibujados de 30 / 50 / 80.
  Mi primera versión del test solo probaba el caso tangente —el parámetro de
  inclinación estaba mal escrito y no hacía nada—, o sea que verificaba la
  mitad que no aporta. Con el escalón bien hecho, el resultado es el que
  justifica la herramienta: con 0°, 12° y 22° de escalón el RADIO da 50,5 /
  50,4 / 50,4, indistinguibles, y la TANGENCIA da 4,4° / 10,7° / 20,6°.
  El suelo de ruido queda dicho: sobre un acuerdo perfectamente tangente el
  dentado de los píxeles deja 3-4° de desviación aparente, así que por debajo de
  eso la herramienta no puede afirmar nada.)* El
  radio del arco de transición **y si de verdad es tangente** a los tramos
  rectos vecinos: un acuerdo que no empalma tangente es un defecto de
  mecanizado que el radio solo no delata.
  Casi gratis con lo que hay: `decomposeContour` ya separa rectas de arcos y
  `fitCircleTaubin` ya da el radio; falta el ángulo entre la tangente del arco
  en cada extremo y la recta vecina.
  Verificación: acuerdo sintético tangente (desviación ~0) frente a uno con un
  escalón deliberado (desviación = la dibujada).

- [x] **M4 — Ranura o garganta en eje.** *(Reutiliza el perfil axial del Eje
  pero con el borde CRUDO, no ajustado: la ranura es justo donde el borde se
  sale de la recta que el Eje ajusta, así que ajustar ahí borraría la medida.
  El aviso que pedía el plan resultó tener DOS regímenes, no uno, y el segundo
  lo encontró un test que falló bien: con paso 13 px sobre una ranura de 12 px
  no cae dentro NINGÚN corte y el perfil sale plano, así que la herramienta
  decía «no se ve ninguna ranura» — verdad literal y mensaje que engaña, porque
  desde el perfil eso es indistinguible de un eje liso y el operador daría la
  pieza por buena. Ahora ese aviso lleva siempre cuál es la ranura más fina que
  ese muestreo podría ver. El régimen que sí estaba previsto —uno o dos cortes
  dentro— se rechaza con las cifras y el qué hacer.
  Exactitud comprobada en serio, y de paso corrigió mi propia hipótesis: el
  error NO es un sesgo fijo en píxeles sino que sigue al paso y cambia de signo
  con él (+1,50 px con paso 2,52; −0,97 px con paso 1,00), o sea cuantización
  del muestreo, que es lo anunciado. El test lo afirma por su FORMA —|error| <
  paso, y error relativo que se diluye al crecer la ranura— en vez de con una
  cota inventada, que es lo que descarta una escala mal puesta.
  Un detalle de método: la fixture dibujaba 39 px de hueco cuando le pedías 40,
  porque `fillPoly` pinta la columna del borde. Comparar contra el nominal
  habría cargado ese píxel en la cuenta de la herramienta, así que el test mide
  el hueco realmente dibujado y compara contra eso.)* Ancho, profundidad y **diámetro de
  fondo** de una entalla en una pieza torneada — la cota de un anillo de
  retención.
  Reutiliza el perfil radio-contra-posición-axial que el Eje ya genera:
  detectar el mínimo local y poner calípers locales sobre los dos flancos.
  Verificación: eje sintético con una ranura de dimensiones conocidas; una
  ranura más estrecha que el muestreo axial **se declara no medible** en vez de
  devolver un número redondeado al paso.

- [ ] **M5 — El sesgo del ángulo de flanco de la Rosca, dicho en voz alta.**
  No es una herramienta nueva: es cerrar un cabo suelto de la ronda anterior.
  Por el ángulo de hélice, el flanco cercano y el lejano de una rosca se
  proyectan con inclinaciones distintas y el filete sale engrosado; los
  comparadores ópticos lo corrigen **inclinando el eje óptico** el ángulo de
  hélice. Sin esa corrección hay **sesgo sistemático**, y hoy la herramienta no
  lo menciona.
  Añadir el aviso al detalle de la Rosca cuando se pida el ángulo de flanco,
  estimando el ángulo de hélice a partir del paso y el diámetro ya medidos
  (`atan(paso / (π·Ø))`) para poder decir **cuánto** sesgo cabe esperar en vez
  de un "puede haber error" genérico.
  Verificación: el aviso aparece con una hélice apreciable y **no** aparece con
  una rosca fina donde el sesgo es despreciable — la regla de siempre: un aviso
  que salta siempre es un aviso que se aprende a ignorar.

### D — Cierre

- [ ] **D1 — Documentación.** README (el apartado Configurar, las cinco
  familias con su tabla de herramientas, y la tabla de "lo que una silueta no
  puede medir" bien visible) y ARQUITECTURA (sección nueva: elementos derivados
  y referencias entre herramientas; por qué zona mínima y no mínimos cuadrados
  en GD&T; por qué el ROI automático se rinde). Tabla de herramientas ampliada
  a las que hayan entrado.

- [ ] **D2 — Repaso de coherencia.** Ampliar los barridos `ToolCoherence` que
  ya existen para cubrir lo nuevo: toda herramienta tiene familia; toda
  herramienta que acepte referencia la declara y falla con motivo si no está;
  toda herramienta GD&T dice en su descripción qué necesita y qué no puede; y
  las de zona mínima nunca devuelven menos que su equivalente por mínimos
  cuadrados (invariante matemático que además caza errores de implementación).

- [ ] **D3 — Borrar este archivo** cuando todo lo anterior esté marcado.

---

## Orden recomendado

```
C1 → C2 → C3 → C4 → C5 → (C6) → C7 → C8      Configurar, de arriba abajo
R1 → R2 → R3                                  el andamio: obligatorio
X0 → X1 → X2                                  construcciones y referencias
F1 → F2 → F3 → F4                             figuras básicas
L1                                            medición en línea
G1 → G2 → G3 → G4 → G5 → G6 → (G7)            GD&T
M1 → M2 → M3 → M4 → M5                        máximos, mínimos y torneadas
D1 → D2 → D3                                  cierre
```

`C` va primero porque es lo que se pidió primero, es independiente de todo lo
demás y entrega valor visible pronto (enfoque, cámara, zoom de trabajo,
recuento de piezas).

`R` va inmediatamente después y **es obligatorio**: la paleta ya no cabe hoy, y
las cuatro trampas silenciosas convertirían las veinte herramientas siguientes
en una lotería.

`X` va antes que `G` porque el GD&T sin datum no es GD&T, y el datum es una
construcción.

`F`, `L`, `G` y `M` son independientes entre sí una vez cerrado `X`: se pueden
reordenar según lo que haga falta antes, o parar después de cualquiera.

Los dos ítems entre paréntesis (`C6`, `G7`) son los caros. Cada uno tiene
escrito en su ficha qué se entrega si se decide dejarlo fuera.
