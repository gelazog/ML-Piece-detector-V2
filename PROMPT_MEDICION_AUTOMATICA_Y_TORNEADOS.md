# PROMPT — Medición automática y piezas torneadas (roscas, engranajes, diámetros)

Documento de planificación para la siguiente ronda de **PC Inspector**.
Continúa [PROMPT_MAESTRO_PC_INSPECTOR.md](PROMPT_MAESTRO_PC_INSPECTOR.md) (las 6
fases base) y las rondas de pulido posteriores. Pensado para consumirse **dentro
de `/loop`, ítem por ítem**.

Sale de dos peticiones concretas:

1. Un **botón que tome las medidas automáticamente** de una pieza y sus
   contornos.
2. Una función que **ayude a medir y lea mejor tornos, roscas de tornillo,
   engranajes, el diámetro y el radio**.

---

## Principios (heredados, no negociables)

1. **No reestructurar la arquitectura por capas** (`core/ camera/ vision/ ml/
   database/ domain/ inspection_editor/ engine/ repositories/ ui/`).
2. **Compilar y probar de verdad** antes de cerrar un ítem: `cmake --build
   --preset mingw-release` limpio bajo `-Werror`, `ctest --preset
   mingw-release` en verde, y humo de la app.
3. **Un test por lógica nueva no trivial.** Todo lo de aquí es cálculo puro:
   se prueba con **piezas sintéticas generadas en el test** (una rosca, un
   engranaje y un eje dibujados con parámetros conocidos), que es la única
   forma de saber si una medida es correcta y no solo estable.
4. **Commits atómicos por ítem**, mensaje en español, sin firma.
5. **Actualizar README, ARQUITECTURA y memoria** al cerrar cada ítem.
6. Al terminar el backlog entero, **borrar este archivo**.

---

## Decisión de diseño que gobierna todo el documento

**La medición automática no es una vía de medida aparte: genera propuestas de
herramientas.**

Es tentador que el botón imprima una lista de números. Sería un callejón sin
salida: esos números no tendrían tolerancia, no darían veredicto, no se
guardarían en la plantilla, no seguirían a la pieza con el fixture y no
aparecerían en el histórico. Todo eso ya existe y funciona para las
herramientas.

Así que el botón **rellena la lista de herramientas** con lo que ha encontrado
(círculos en los agujeros, calibres entre caras paralelas, reglas del contorno
envolvente, ángulos en las esquinas), cada una con su tolerancia sugerida. El
operador revisa, borra lo que sobra y ajusta. Pasa de *dibujar veinte
herramientas a mano* a *revisar veinte propuestas*, que es donde está el ahorro
real, y no se inventa ningún camino paralelo que luego haya que mantener.

Lo mismo con las piezas torneadas: **rosca, engranaje, eje y arco son
herramientas nuevas**, no un modo especial de la aplicación.

---

## Lo que hay que decir en voz alta antes de empezar

Estas medidas se sacan de una **silueta 2D**. Eso impone límites que no son
negociables y que la interfaz tiene que comunicar, o el operador confiará en
números falsos:

- **Iluminación.** Un diámetro, un paso de rosca o un módulo de engranaje solo
  salen bien con el borde **limpio y de alto contraste**: contraluz
  (retroiluminación) o fondo mate uniforme. Con luz frontal sobre metal
  brillante, el borde que ve la cámara no es el borde de la pieza.
- **Perpendicularidad.** La cámara tiene que estar de frente al plano de la
  pieza. Inclinada, un círculo se lee como elipse y el diámetro sale corto. Ya
  existe `MarkerScale::quality` (uniformidad del marcador ArUco) para medir
  esto: **las herramientas nuevas deben avisar cuando la calidad es baja**.
- **Sin calibración no hay mm.** El paso de una rosca en píxeles no le sirve a
  nadie, y el **módulo de un engranaje directamente no existe** sin escala
  real. Estas herramientas piden calibración y lo dicen cuando falta, en vez
  de dar un número sin unidad.
- **Vista correcta por herramienta:** el engranaje se mide **de cara**, la
  rosca y el eje **de perfil**. Una rosca vista de frente no tiene paso
  medible.

---

## Backlog

Formato: casilla, ID, tarea, por qué, algoritmo, archivos, cómo se verifica.

### F. Fundamentos de ajuste (habilitan todo lo demás)

- [x] **F1 — Ajuste de círculo Taubin, y robusto.** El Círculo actual usa
  ajuste **Kasa** (`tool_executor.cpp`, ~línea 319). Kasa es conocido por
  **sesgar el radio hacia abajo en arcos parciales**: sobre una circunferencia
  completa da bien, pero sobre el arco de una esquina —que es justo lo que pide
  el ítem del radio— falla. **Taubin** es prácticamente insesgado y cuesta lo
  mismo. Añadir además una capa robusta (IRLS con pesos de Huber, o RANSAC) que
  descarte los puntos que mete una rebaba o un reflejo.
  Nuevo `vision/fitting.{h,cpp}`:
  `fitCircleTaubin(points) -> {center, radius, rmsResidual}` y
  `fitCircleRobust(points, maxOutlierFraction) -> {..., inlierCount}`.
  Migrar el Círculo a usarlo (su redondez y su diámetro mejoran solos).
  Verificación: arcos sintéticos de 30°, 90°, 180° y 360° con ruido gaussiano
  conocido — se comprueba que el error de radio de Taubin es menor que el de
  Kasa en los arcos cortos, y con puntos atípicos añadidos a mano que el
  robusto los ignora. **Este ítem debe hacer visible la mejora con números en
  el propio test**, no solo pasar.
  Skills: `cpp-coding-standards`, `cpp-testing`.

- [x] **F2 — Ajuste de recta robusto.** Mínimos cuadrados totales (PCA sobre
  los puntos, que trata bien las rectas verticales) + rechazo de atípicos.
  `fitLineRobust(points) -> {point, direction, rmsResidual, inlierCount}`.
  Lo necesitan el eje, la rosca (flancos) y la descomposición del contorno.
  Archivos: `vision/fitting.{h,cpp}`.
  Verificación: rectas sintéticas en todas las orientaciones (incluida la
  vertical, donde `y=mx+b` se rompe) y con atípicos.

- [x] **F3 — Perfil radial y perfil axial.** *(Van en
  `inspection_editor/execution/profiles.*`, no en `vision/` como decía este
  documento: usan `detectEdges`, que está en esa capa, y `pci_vision` está por
  debajo — ponerlos en vision invertiría la dependencia.)* Son las dos formas de "recorrer un
  borde" que piden las herramientas nuevas, y el Círculo ya tiene media
  implementación del primero enterrada en su ejecutor. Extraer y generalizar:
  - `radialProfile(gray, center, rMin, rMax, rayCount) -> vector<{angle,
    radius, strength}>`: para cada ángulo, dónde está el borde. Base del
    engranaje y del arco.
  - `axialProfile(gray, axisFrom, axisTo, side, stations, reach) ->
    vector<{t, offset, strength}>`: para cada estación a lo largo de un eje,
    a qué distancia está el borde de ese lado. Base del eje y de la rosca.
  Ambos reutilizan `detectEdges` (subpíxel) que ya existe.
  Archivos: `vision/fitting.{h,cpp}` o `vision/profiles.{h,cpp}`.
  Verificación: sobre imágenes sintéticas con el borde en una posición exacta
  conocida, el perfil la recupera con error subpíxel.

- [x] **F4 — Periodo dominante de una señal.** *(En `vision/periodicity.*`. Se
  añadió un modo **circular** que el plan no preveía: el perfil radial de un
  engranaje cierra sobre sí mismo y el de una rosca no, y tratarlos igual
  desperdicia muestras en el primero.)* La rosca y el engranaje son
  **señales periódicas**: el perfil de una rosca a lo largo del eje se repite
  cada paso, y el perfil radial de un engranaje se repite cada diente. El paso
  y el número de dientes salen los dos del mismo cálculo.
  `dominantPeriod(signal, minPeriod, maxPeriod) -> {period, confidence}` por
  **autocorrelación** con refinamiento parabólico del pico (más estable que
  contar picos, que se descuadra con un solo diente mellado).
  Verificación: señales sintéticas de periodo conocido, con ruido, con
  tendencia superpuesta y con un periodo defectuoso — el periodo estimado
  aguanta y la confianza baja cuando debe.

### A. Medición automática

- [x] **A1 — Descomposición del contorno en primitivas.** *(Se hizo con barrido voraz + ajuste de fronteras, no con detección de esquinas ni partición recursiva: ver ARQUITECTURA.)* El contorno que ya
  entrega `analyzeFrame` es una lista de puntos; para medir hace falta saber
  **qué trozos son rectas y qué trozos son arcos**. Es el paso que convierte
  "una silueta" en "dos caras paralelas, cuatro esquinas redondeadas y tres
  agujeros".
  Algoritmo: remuestrear el contorno a paso uniforme → detectar esquinas
  (máximos de curvatura sobre el contorno suavizado) → para cada tramo entre
  esquinas, ajustar recta (F2) y círculo (F1) y quedarse con el de menor
  residuo por punto, exigiendo un ángulo de arco mínimo (~20°) para aceptar un
  círculo, porque por debajo cualquier recta corta se ajusta igual de bien a un
  arco enorme y sale un radio absurdo.
  Los **agujeros** salen de `findContours` con `RETR_CCOMP`: el nivel 1 de la
  jerarquía son los huecos internos.
  Nuevo `vision/geometry_features.{h,cpp}`:
  `decomposeContour(contour) -> vector<Primitive{Line|Arc, puntos, ajuste}>`,
  `findHoles(mask) -> vector<PieceContour>`.
  Verificación: siluetas sintéticas de geometría conocida (rectángulo con
  esquinas redondeadas de radio R, pieza en L, disco con tres agujeros) — se
  comprueba que salen el número de rectas y arcos correcto y que los radios
  coinciden con los dibujados.

- [x] **A2 — Generador de propuestas.** Convierte las primitivas de A1 en
  herramientas listas para usar:
  - `minAreaRect` de la pieza → dos **Reglas** (largo y ancho).
  - Cada agujero y cada arco con ángulo suficiente → un **Círculo**.
  - Cada par de rectas casi paralelas y enfrentadas (normales opuestas y
    solape al proyectarlas) → un **Calíper** cruzándolas por el punto medio de
    su zona común.
  - Cada par de rectas adyacentes con ángulo lejos de 0°/180° → un **Ángulo**
    en su intersección.
  Cada propuesta lleva su tolerancia sugerida (`suggestTolerances`, ya existe),
  un nombre legible ("Ø agujero 1", "Ancho A-B") y **una frase de por qué se
  propone**, que es lo que permite al operador decidir rápido.
  Hay que **deduplicar** (no proponer dos calibres midiendo el mismo par) y
  **acotar el número** (máximo ~12, ordenadas por tamaño del rasgo): cincuenta
  propuestas son tan inútiles como ninguna.
  Nuevo `inspection_editor/auto_measure.{h,cpp}`, Qt-free y por tanto
  testeable: `proposeTools(gray, mask, fixture, opciones) ->
  vector<AutoProposal{ToolConfig, ToolGeometry, double medida, string porQue}>`.
  Verificación: sobre las mismas siluetas sintéticas de A1, las propuestas
  esperadas aparecen y sus medidas coinciden con la geometría dibujada.

- [x] **A3 — Botón "Medir automáticamente" con revisión.** *(Confirmado por el
  usuario: diálogo de revisión, no inserción directa.)* Botón en el editor que ejecuta
  A2 sobre el fotograma actual y abre un **diálogo de revisión**: la lista de
  propuestas con casilla, su medida, su tolerancia sugerida y el porqué;
  resaltando en el lienzo la que está seleccionada. Al aceptar, las marcadas se
  insertan como herramientas **en un solo paso deshacible**
  (`commitUndoState`).
  Insertarlas directamente sin revisión sería más rápido de programar y peor de
  usar: dejaría al operador borrando a mano lo que no pidió.
  Archivos: `inspection_editor/editor_window.*`, nuevo diálogo en
  `inspection_editor/` o `ui/`.
  Verificación: prueba de gesto en `pci_gui_tests` (el diálogo se llena, marcar
  y aceptar inserta exactamente las marcadas, deshacer las quita todas juntas).
  Skills: `qt-ui-design`, `qt-cpp-review`.

- [ ] **A4 — Ver y exportar el contorno.** La otra mitad de la petición ("y los
  contornos"). Superponer el contorno detectado con su descomposición en
  colores (recta / arco) y un resumen: perímetro, área, número de agujeros,
  envolvente. Botón para **exportar el contorno a CSV** (x, y en mm si hay
  calibración, si no en px) para llevárselo a un CAD.
  Archivos: `inspection_editor/canvas/editor_canvas.*` (capa de dibujo),
  `editor_window.*` (exportar).
  Verificación: el CSV exportado de una silueta sintética se vuelve a leer y
  encierra el área esperada; render de la superposición en `pci_gui_tests`.

### T. Piezas torneadas

- [x] **T1 — Herramienta Arco (radio).** El "radio" que pide el usuario. Se
  trazan tres puntos sobre un arco (inicio, medio, fin) y la herramienta mide
  **R, Ø y error de forma**. Es el equivalente óptico de un radio-gauge, y sin
  F1 (Taubin) no sale bien: es justo el caso de arco parcial donde Kasa sesga.
  Algoritmo: círculo provisional por los tres puntos → barrido radial (F3)
  acotado al sector → `fitCircleRobust` (F1) sobre los bordes encontrados.
  `ToolType::Arc` + `ArcGeometry{start, mid, end, searchBand}`.
  Archivos: el recorrido completo de una herramienta nueva —
  `tools/tool_types.h`, `tools/tool_geometry.{h,cpp}` (+ JSON),
  `execution/tool_executor.cpp`, `canvas/canvas_geometry.cpp` (manijas, puntos
  de referencia, distancia), `canvas/editor_canvas.cpp` (dibujo y gesto de
  creación), `canvas/tool_icons.cpp`, `ui/main_window.*`.
  Verificación: arcos sintéticos de radio conocido y ángulo variable.

- [x] **T2 — Herramienta Eje / Diámetro (torno).** Una pieza torneada vista de
  perfil son **dos bordes casi paralelos**. La herramienta mide **Ø,
  conicidad** (si el eje no es cilíndrico) y **rectitud** en un solo trazo,
  que es lo que se comprueba al salir del torno.
  Algoritmo: N estaciones a lo largo del eje trazado; en cada una, búsqueda
  perpendicular a los dos lados (F3) → dos nubes de puntos → `fitLineRobust`
  (F2) a cada lado. Ø = distancia media entre las dos rectas; conicidad =
  ángulo entre ellas; rectitud = residuo máximo.
  Medir con un solo calíper en un punto no distingue un eje cónico de uno
  cilíndrico; por eso la herramienta es distinta y no un preset del calíper.
  `ToolType::Shaft` + `ShaftGeometry{axisFrom, axisTo, searchBand, stations}`.
  Verificación: ejes sintéticos cilíndricos y cónicos de dimensiones conocidas.

- [x] **T3 — Herramienta Rosca.** Mide **paso, Ø mayor (exterior), Ø menor (de
  fondo) y ángulo de flanco**, que son los cuatro números con los que se
  identifica y se acepta una rosca.
  Algoritmo:
  1. Perfil axial (F3) a lo largo del eje de la rosca, un lado.
  2. Quitar la tendencia (recta ajustada al perfil): separa la conicidad del
     rizado periódico.
  3. **Paso** = periodo dominante del perfil sin tendencia (F4), en px → mm.
  4. **Ø mayor** = 2 × media de las crestas; **Ø menor** = 2 × media de los
     valles (medias de los extremos detectados, no del máximo suelto, para que
     una rebaba no defina el diámetro).
  5. **Ángulo de flanco**: en cada periodo, ajustar recta (F2) al flanco de
     subida y al de bajada entre valle y cresta; el ángulo entre ellas se
     compara con 60° (métrica ISO) y 55° (Whitworth).
  6. Con calibración, **proponer la designación** (M6×1, M8×1.25…) buscando el
     par (Ø mayor, paso) más cercano de una tabla métrica estándar, y decir
     cuánto se aparta. Sin calibración, **negarse a dar designación**.
  `ToolType::Thread` + `ThreadGeometry{axisFrom, axisTo, side, searchBand}`.
  Verificación: imagen sintética de un perfil de rosca trapezoidal con paso,
  diámetros y ángulo **dibujados a partir de parámetros conocidos**, con y sin
  ruido; se exige recuperar el paso con error < 2 % y el ángulo con < 3°.
  Skills: `computer-vision-opencv`, `cpp-testing`.

- [x] **T4 — Herramienta Engranaje.** Mide **número de dientes (z), Ø de cabeza
  (Da), Ø de raíz (Df), módulo (m), Ø primitivo (Dp) y excentricidad**.
  Algoritmo:
  1. Perfil radial (F3) desde el centro, con muchos rayos (~1440).
  2. **z** = periodo dominante del perfil en θ (F4), redondeado a entero;
     contraste con el conteo de máximos por histéresis y **avisar si no
     coinciden** en vez de elegir en silencio.
  3. **Da** = 2 × media de los máximos; **Df** = 2 × media de los mínimos.
  4. **m** = Da/(z+2), con comprobación cruzada m = (Da−Df)/2,25. Si las dos
     estimaciones discrepan, el engranaje **no es un recto normalizado sin
     corrección de perfil** y hay que decirlo, no dar un módulo inventado.
  5. **Dp** = m·z. **Excentricidad** = dispersión de los máximos tras reajustar
     el centro minimizando esa dispersión.
  El módulo **exige calibración**: sin mm no se calcula.
  `ToolType::Gear` + `GearGeometry{center, rMin, rMax}`.
  Verificación: engranajes sintéticos dibujados con z y módulo conocidos
  (varios z, incluidos primos y con un diente dañado) — se exige z exacto y
  módulo con error < 3 %, y que el diente dañado baje la confianza sin
  cambiar z.
  Skills: `computer-vision-opencv`, `cpp-testing`.

- [x] **T5 — Aviso de condiciones de medida.** Estas cinco herramientas dan
  números creíbles con datos malos, que es la peor forma de fallar. Al
  ejecutarlas, comprobar y avisar en el resultado cuando: no hay calibración
  px→mm y la medida la necesita; `MarkerScale::quality` es baja (cámara
  inclinada); el contraste del borde a lo largo del perfil es pobre; o el
  número de puntos válidos no llega al mínimo.
  Archivos: `execution/tool_executor.cpp`, `vision/quality_metrics.*`.
  Verificación: cada aviso se dispara con su caso sintético y **no** con el
  caso bueno.

### D. Cierre

- [ ] **D1 — Documentación.** README (herramientas nuevas y el botón, con las
  condiciones de iluminación y vista bien visibles) y ARQUITECTURA (sección
  nueva: cómo se mide una rosca y un engranaje a partir de una silueta, con la
  justificación de cada elección — Taubin frente a Kasa, autocorrelación frente
  a contar picos). Tabla de herramientas ampliada a catorce.

- [ ] **D2 — Repaso de coherencia.** Que las herramientas nuevas cumplan lo que
  ya cumplen las diez viejas: geometría en coordenadas de pieza (siguen a la
  pieza al girar), manijas y distancia de clic en `canvas_geometry`, invariancia
  al giro probada, redondeo y unidades con el mismo formato, icono, tooltip
  descriptivo, tolerancias sugeridas y guardado/cargado en plantilla. Un test
  por cada punto, siguiendo los que ya existen por herramienta.

- [ ] **D3 — Borrar este archivo** cuando todo lo anterior esté marcado.

---

## Orden recomendado

`F1 → F2 → F3 → F4` primero: son la base de todo y cada uno se prueba solo.
Luego `T1` (la herramienta nueva más pequeña, que estrena el recorrido completo
de "añadir un tipo de herramienta" y deja el camino trillado para las demás),
`T2`, `T3`, `T4`, `T5`. La medición automática (`A1..A4`) va después porque
reutiliza F1 y F2 y, con las herramientas torneadas ya dentro, puede proponer
también arcos y ejes. Cierre con `D1..D3`.
