# Lo que hay que mejorar

Lista viva de pendientes. **Cada punto dice si está verificado contra el código
o si solo está informado**, porque no es lo mismo y confundirlo cuesta tiempo.

Cómo se usa: se coge **una** tarea, se hace entera, se marca aquí y se commitea.
Las reglas de trabajo están en [CONTEXTO.md](CONTEXTO.md).

Leyenda: `[ ]` pendiente · `[x]` hecho · `[~]` a medias · `[!]` se intentó y no salió

---

## A. Metrología: lo que separa una demo de un medio de control

Esto salió de investigar qué exige la práctica seria (ISO 14253-1, JCGM 106,
ISO 10360-7, MSA/AIAG). **Los cuatro puntos están verificados contra el código:
se buscó y no hay nada.** Es lo de más valor de toda la lista, y también lo más
grande.

- [ ] **A1 · Incertidumbre expandida *U* por cota.** Verificado: `grep -i
  incertidumbre|uncertainty` sobre `src/` no devuelve **nada**. Sin *U*, un
  OK/NG no es una declaración de conformidad: es una opinión. Empezar por lo
  barato — propagar la resolución (px→mm), la calidad de escala que ya se mide
  (`fmt.scaleQuality`) y la dispersión del ajuste de cada herramienta.
- [ ] **A2 · Banda de guarda y tercer estado «indeterminado».** Verificado:
  cero apariciones. Hoy una cota que cae justo en el límite se declara OK o NG
  con la misma seguridad que una que cae en el centro. ISO 14253-1 dice que en
  la banda *U* alrededor del límite no se puede declarar ninguna de las dos.
  Depende de A1.
- [ ] **A3 · Trazabilidad de la calibración.** Verificado: **no existe una tabla
  de calibración**; el factor px→mm vive suelto en `Settings` (ver
  `src/database/schema.cpp`), sin fecha, sin qué patrón se usó, sin caducidad, y
  **ningún resultado de inspección la referencia**. Consecuencia práctica: el día
  que se descubra que la calibración estaba mal, no hay forma de saber qué
  veredictos hay que revisar. Es el punto que más barato sale y más protege.
- [ ] **A4 · Estudio R&R / Tipo 1 (Cg, Cgk).** Verificado: cero. Ningún cliente
  de automoción homologa un medio de control sin %GRR. Y el Tipo 1 es barato de
  implementar: una pieza patrón, 50 repeticiones, y la aplicación ya sabe medir
  sola.
- [~] **A5 · Rechazar la imagen, no solo avisar.** A medias, y el hueco es más
  estrecho de lo que parecía. **Ya existe**: `vision/quality_metrics.h` calcula
  nitidez por varianza del laplaciano con umbral de aceptación, y
  `appendConditionWarnings` avisa de cámara inclinada y de borde con poco
  contraste. Lo que falta es **bloquear la medida** cuando la escena no cumple,
  en vez de publicar el número con una advertencia al final del texto — que es
  exactamente el fallo que ya se corrigió en la herramienta de Rosca.

## B. Tamaño y estructura del código

- [ ] **B1 · `src/ui/main_window.cpp` tiene 7 713 líneas.** Partirlo **dentro de
  su capa** (eso no reorganiza la arquitectura). Candidatos naturales: la
  construcción de menús y atajos, la tira de capturas, el ciclo de vídeo, los
  manejadores de inspección.
- [ ] **B2 · `inspection_editor/execution/tool_executor.cpp` tiene 4 700
  líneas** con las 32 herramientas en un solo `switch`. Un fichero por familia
  —longitudes, formas, GD&T, torneadas, construcciones— con el despacho
  quedándose donde está.
- [ ] **B3 · `inspection_editor/canvas/editor_canvas.cpp`, 3 282 líneas.**
- [~] **B4 · Auditoría de código muerto y duplicado.** Hecha una pasada, a mano,
  después de que el agente se quedara sin cupo.

  **Once cosas muertas, verificadas una a una** (el barrido automático da muchos
  falsos positivos: `paintEvent` y `sizeHint` los llama Qt, las señales se
  conectan por nombre):

  | qué | dónde |
  |---|---|
  | `buildMeasurementsTab()` | **declarada y nunca escrita** — si alguien la llamara, no enlazaría |
  | 4 getters de `editor_canvas.h` | `focusedPiece`, `boardVisible`, `contourReport`, `rulerVisible`: nadie los llama |
  | `managePiecesButton_` | declarado y **nunca asignado** |
  | 4 punteros a `QAction` de menú | se guardan al crear la entrada y nadie los vuelve a leer |
  | `pendingFrame_` | guarda una copia del frame que ya se pasa aparte |

  **Y un hallazgo mejor que el código muerto: `toGray` estaba escrita CUATRO
  veces en `vision/`, y tres contestaban distinto.** La de
  `orientation_anchor.cpp` convertía si había tres canales y **devolvía la
  imagen tal cual en cualquier otro caso** — un BGRA salía sin convertir y el
  resto del código lo trataba como gris. Eso no falla: da números. Ahora es una
  sola en `vision/gray.h`, con sus tres decisiones escritas y su prueba.

  Falta el resto: cabeceras incluidas y no usadas, y constantes mágicas
  repetidas. El barrido de funciones con el mismo nombre en varios ficheros dejó
  además estos candidatos sin mirar: `round0` (camera_controls y shape_class),
  `toHex` (dos ficheros de cámara), `readProperty`, `describe` y `errorOf`.

  **`canonicalDirection` ya está mirada y NO hay que unificarla.** Son dos
  funciones **distintas con el mismo nombre**, y esa es justamente la trampa: la
  de `auto_measure.cpp` normaliza la dirección de un tramo de contorno y la
  canonicaliza para que **`x > 0`**; la de `tool_executor.cpp` recibe una
  dirección ya normalizada y la canonicaliza para que **`y > 0`**. Ejes
  distintos. Las dos son correctas donde viven, y quien las junte creyéndolas la
  misma rompe una de las dos en silencio. Lo que convendría es que se llamaran
  distinto.
- [ ] **B5 · Auditoría de la suite de pruebas.** Igual: se quedó a mitad. 42 272
  líneas de prueba para 48 673 de código. Buscar tests redundantes, tests sin
  aserto útil, y módulos sin cobertura.

## C. Interfaz

Todo esto salió de una auditoría anterior y está verificado.

- [ ] **C1 · Ningún menú enseña su atajo.** 0 de 58 entradas. El operador no
  puede descubrir un atajo sin abrir la ayuda.

  **Ojo, el arreglo NO es poner `setShortcut` en las entradas de menú.**
  Investigado: los 21 atajos son `QAction` **invisibles** que `addShortcut()`
  cuelga de la ventana (`main_window.cpp:2805`), y las entradas de menú son
  `QAction` **distintas**. Poner la misma tecla en las dos da dos acciones con la
  misma secuencia en la misma ventana, que es `ambiguousActivate` — Qt no
  dispara ninguna de forma fiable. Este proyecto ya se comió ese fallo con
  `Ctrl+1` y `Ctrl+2`.

  El arreglo bueno es que **haya una sola acción**: meter en el menú la que ya
  tiene el atajo, en vez de crear una gemela. Eso obliga a construir los atajos
  antes que los menús, o a casarlos después, y `shortcuts_` tendría que apuntar a
  la acción superviviente para que la guía de atajos siga pudiendo editarla.
- [ ] **C2 · Ningún botón tiene acelerador `Alt+letra`.** 0 de unos 40.
- [~] **C3 · Diálogos donde Enter dispara el botón equivocado.** Hecho el
  peligroso, y **medido en vez de razonado**: una prueba pregunta a los botones
  quién se lleva el Enter, y en la guía de atajos la respuesta era «Restaurar por
  defecto» — que **borra toda la tabla de teclas que el operador acaba de
  editar**. Pulsar Enter creyendo que guardas y perder el trabajo, sin que haya
  ningún fallo de programación: basta con teclear.

  Nadie lo escribió a propósito. En un `QDialog` sin botón por defecto declarado,
  Qt coge el primero que se construyó, así que lo decidía el orden de tres líneas
  de C++ que nadie vuelve a mirar.

  `tests/test_default_buttons.cpp` no arregla un diálogo: arregla la **clase** de
  fallo, y también exige que ningún botón destructivo conserve `autoDefault` —
  porque con él basta tabular hasta el botón para que el Enter siguiente
  destruya.

  **Calibración, hecho también**, y tenía los dos problemas de golpe: el Enter
  se lo llevaba «Calcular escala con la distancia» —por ser el primero que se
  construye— en vez de «Aplicar calibración», y «Quitar calibración» conservaba
  `autoDefault`. Ese sí destruye: deja **todas las cotas de la pieza en
  píxeles**.

  Al mutar el arreglo para comprobar que la prueba lo caza salió un matiz:
  quitar el `setDefault(true)` **no** cambió el botón por defecto, porque el
  `setAutoDefault(false)` del primero ya lo desplazaba. Lo cazó la segunda
  comprobación. Las dos hacen falta y no son redundantes: una fija quién se lleva
  el Enter, la otra impide llegar al botón destructivo tabulando.

  Faltan los otros nueve diálogos. Construirlos en una prueba cuesta distinto en
  cada uno, así que van de uno en uno.
- [x] **C4 · `detection_page.cpp` apilaba las filas sin agrupar.** Hecho, y para
  cuando llegué eran **diecinueve**, porque el aviso de mesa de color le añadió
  tres. Ahora son cuatro grupos —cómo se separa la pieza (8 filas), dónde va el
  corte (4), corregir la silueta (4), qué cuenta como pieza (3)— que son las
  cuatro preguntas que se hacen ahí, en el orden en que se hacen.

  Lo que lo hacía urgente no era el número: **el aviso de «tu mesa tiene color»
  salía en la fila 4 y el desplegable que lo arregla estaba en la 11**, con diez
  controles por en medio. Quien leía el aviso tenía que buscar de qué hablaba.

  `tests/test_detection_groups.cpp` fija las dos cosas: que ninguna fila quede
  suelta fuera de un grupo, y que **la sugerencia y su control vivan en el mismo
  grupo**.
- [ ] **C5 · Quedan 49 colores escritos a mano** fuera de `ui/theme.h`. Hay un
  trinquete en `tests/test_palette_guard.cpp` que impide que suban; hay que ir
  bajándolos y bajando el tope.
- [ ] **C6 · `main_window.cpp:692-779` pone 10 entradas antes del único
  separador** del menú.
- [ ] **C7 · `inspectButton_->setDefault(true)` sobre un `QMainWindow`** promete
  un Enter que no puede cumplir.

## D. Documentación

Sale de una auditoría con el mapa de contenidos medido.

- [x] **D1 · Arreglar las contradicciones antes de mover nada.** Hecho. Las
  cuatro, y las cuatro verificadas contra el código antes de tocarlas:
  - `README.md:248` dice que la separación de piezas pegadas aguanta «hasta un
    13 % de solape. Más allá se rinde», y `ARQUITECTURA.md:4266` dice «se rinde
    a partir del 19 %». **Verificado en `tests/test_split_touching.cpp:164-166`:
    lo medido es 35 px (13 %) → 2 piezas y 50 px (19 %) → 1 pieza.** Entre 13 %
    y 19 % **no se midió nada**, así que el «más allá se rinde» del README
    afirma más de lo que se probó.
  - `ARQUITECTURA.md:4977` abre el capítulo de mejoras con «ninguna de estas
    cosas está hecha» y en su propia lista hay tres marcadas como hechas.
  - El mapa de calor de diferencia y la exportación del histórico seguían
    listados como pendientes. **Verificado que existen y están conectados**:
    `vision/difference_map.cpp` lo usa `ui/inspection_result_dialog.cpp`, y
    `domain/shift_report.cpp` lo usa `ui/history_dialog.cpp`, los dos con
    prueba. Marcados como hechos.
- [x] **D2 · Sacar la bitácora de defectos de ARQUITECTURA.** Hecho, y el
  hallazgo era exacto: **1 191 líneas** de narración de defectos dentro de un
  capítulo titulado «Persistencia» cuyo contenido de persistencia real ocupaba
  **veintiuna**. Ahora en [BITACORA.md](BITACORA.md).

  De paso salió otra: las otras 165 líneas de ese capítulo eran **menús, teclado
  y barra de botones**, o sea interfaz. Son el capítulo 11 nuevo. Lo único que
  se quedó en Persistencia fue la persistencia — incluida «volver a donde lo
  dejaste», que sí lo es y por eso lleva una nota diciéndolo.

  `ARQUITECTURA.md`: **5 119 → 3 955 líneas**. Y la guardia de rutas siguió
  marcando 29 y 0 rotas después de mover todo, que es exactamente para lo que se
  puso el trinquete el paso anterior.
- [~] **D3 · Partir el README.** Hecha la mitad grande: el catálogo de las 32
  herramientas —**409 líneas, una cuarta parte del manual entero**— estaba metido
  dentro de un solo paso de nueve. Ahora es [HERRAMIENTAS.md](HERRAMIENTAS.md) y
  en el README queda la tabla de las cinco familias con un enlace.

  **README: 1 670 → 1 273 líneas.** Falta el resto del paso 5, que sigue siendo
  el más largo con diferencia.
- [ ] **D4 · Quitar del README las secciones «Fase 1…6» (214 líneas).** Es
  documentación de ingeniería dentro de un manual de uso, y repite
  ARQUITECTURA casi entera.
- [x] **D5 · Ampliar `tests/test_readme_paths.cpp` antes de mover nada.** Hecho,
  y encontró cosas. Ahora lee **toda** la documentación en vez de solo el README,
  y el `EXPECT_GT(checked, 5)` —que dejaba perder quince rutas de veintiuna sin
  fallar— es un trinquete en 29.

  Al ampliarla saltaron **cuatro rutas de ARQUITECTURA que nunca había mirado
  nadie**. Tres eran falsos positivos míos: una tabla que enseña rutas VIEJAS
  frente a las nuevas, y estaban en cursiva como si fueran instrucciones. Ahora
  van entre comillas de código, que es lo que son — cadenas del pasado, no
  sitios a los que ir. La cuarta era real y la guardia no podía verla:
  `Configurar ▸ Rendimiento` es una **pestaña**, no una acción de menú, así que
  ahora la guardia también abre el diálogo de Configurar y recoge sus pestañas.

  Resultado: **de 21 rutas vigiladas a 29, y cero rotas.**

## E2. Una detección, no tres — CERRADO

Persiguiendo «sigue habiendo problemas con el color del fondo y el de la pieza, y
el cómo detecta las piezas» aparecieron **tres sitios** que segmentaban con los
valores de fábrica en vez de con los configurados. Los tres arreglados, y los
tres con guardia.

- [x] **Abrir el editor de plantilla.** Analizaba sin configuración para sacar el
  fixture. Sobre mesa de color no daba un fixture torcido: **el editor no abría**,
  mientras la ventana principal enseñaba la pieza bien detectada al lado.
- [x] **El asistente de registro.** Construía su sesión sin configuración, así que
  **aprendía** la pieza con la detección de fábrica mientras «Registrar y activar»
  —que hace lo mismo— usaba la del operador. Es el peor de los tres: la referencia
  nace torcida y luego se compara contra inspecciones bien detectadas.
- [x] **Cargar un perfil de detección.** Devolvía cuatro de los ocho campos a
  fábrica.

`tests/test_same_detection_everywhere.cpp` no vigila una llamada: vigila el
patrón. Recorre los 68 ficheros y exige que nadie analice un frame sin decir cómo
detectar la pieza. Lee el **código fuente** y no el binario, así que además es
inmune a la trampa de `-Werror` dejando el binario viejo en pie.

## E. Límites medidos que siguen ahí

- [ ] **E1 · Arandela de plástico traslúcido.** No se detecta ni por claridad ni
  por color: a través de ella se ve el fondo. Se ve en `arandelas-1.png`.
- [ ] **E2 · Dos ruedas dentadas que se solapan.** `engranajes-1.jpg`: la
  detección las funde en una pieza de 210×406 y, separándolas, el corte se lleva
  dientes (el recuento pasa de 30 a 31 según el radio interior). La aplicación
  **se niega, que es lo correcto**; lo que falta es una forma de medirlas.
- [ ] **E3 · Un tornillo roscado pierde las cotas de su cabeza.** Al darlo por
  periódico se apagan todas las cotas de contorno. Se intentó apagar solo el
  tramo roscado y salió peor (ver `[!]` abajo).
- [ ] **E4 · Engranaje con agujeros de aligeramiento.** El radio interior de la
  propuesta automática sale al 55 % del exterior y en esas ruedas cae encima de
  los agujeros. Hay que dibujar la herramienta a mano.
- [ ] **E5 · El ancla de orientación solo se aplica si `autoOrient` está
  encendido**, y si no, se ignora en silencio. Informado, sin confirmar con el
  operador si es lo que se encontró.
- [ ] **E6 · Conteo de agujeros en `describeContour`.** Un umbral de área
  relativa del 3 % funciona en la moneda, en `ojo-5` y en `arandelas-1`, pero da
  2 en vez de 1 en `tornillo-ojo-3` porque la marca estampada «C15» parece un
  agujero pasante en una silueta 2D.
- [ ] **E7 · `runRegion` no coincide en área con la silueta de la aplicación.**
  Usa su propio Otsu local y la diferencia llega al 52 % en cotas ya guardadas.
  **Decisión del dueño del proyecto, aparcada a propósito.**

## F. Ya está hecho — no volver a investigarlo

Aquí para que nadie gaste otra vez el tiempo que ya se gastó.

- [x] **Zona mínima (MZC) en redondez y rectitud.** Un informe externo lo dio
  por pendiente; **es falso**. Ver `tool_executor.cpp:3288-3404` y
  `tool_geometry.cpp:490`: las dos dan el valor por zona mínima, que es el de la
  norma.
- [x] **Las herramientas miden sobre perfiles 1D, no sobre la máscara.** Otro
  punto que un informe externo dio por pendiente. Hay 10 usos de
  `axialProfile`/`radialProfile` en el ejecutor. Lo que sí usa la máscara es la
  clasificación de forma y la medición automática, y ahí es lo correcto.
- [x] **Afinado subpíxel del borde.** Existe (`vision/subpixel_edge.h`), y nace
  apagado a propósito: encenderlo mueve todas las cotas de una pieza ya
  registrada.
- [x] **Clave de color de fondo** para mesas de color. Sobre cartón rojo pasa de
  7 a 20 piezas detectadas.
- [x] **La herramienta de Rosca sabe decir que no.** Antes decía que sí en las
  16 fotos del banco, con perlas como «paso = 1,3 px» sobre arandelas.
- [x] **Rosca y Engranaje entran en la medición automática.**

## G. Se intentó y no salió

- [!] **Apagar solo el tramo roscado** en vez de la pieza entera, para conservar
  las cotas de la cabeza del tornillo. El tramo de eje sobre el que la Rosca
  consigue medir **no delimita la rosca**: en `tornillo-1.png` la rosca va del
  0 % al 89 % del eje y la colocación ganadora fue del 30 % al 100 %. Volvían
  nueve arcos que eran crestas de filete separadas exactamente un paso.
- [!] **Detectar la rosca por el rizado de la silueta.** No separaba nada: una
  arandela lisa daba 0,98 de repetición y la rosca de verdad 0,88, porque la
  correlación la dominaba la envolvente de la pieza.
- [!] **Colapsar las propuestas de valor repetido.** Rompía el octógono y el
  dodecágono: las N caras iguales de un polígono regular **sí** son N cotas.
- [!] **Cortar la distancia al color de fondo con Otsu a secas.** Sobre fondo
  blanco marcaba solo los aros de nylon de las tuercas y dejaba el cromado del
  lado del fondo — y el recuento seguía diciendo «100 piezas».
- [!] **Igualar la iluminación (`even_light`) antes de segmentar.** Medía peor en
  las 7 imágenes; en `engranaje-1` el vaivén pasaba del 0,8 % al 105,2 %.
