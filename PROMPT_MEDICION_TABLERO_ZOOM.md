# PROMPT — Ronda: modos de medición, tablero centrado (centro = 0) y zoom

Documento de planificación para la siguiente ronda de **PC Inspector**. Cubre
tres cosas pedidas por el usuario, en este orden de dependencia:

1. **Zoom y navegación** del lienzo (base de precisión para todo lo demás).
2. **Tablero de referencia centrado**: una grilla con **origen (0,0) en el
   centro** que permite medir la pieza *por su posición central* — desviación
   en X/Y, distancia radial y ángulo.
3. **Dos modos de medición y registro**: **Especial** (reglas específicas sobre
   el tablero centrado) y **Real / personalizada** (libre, lo de hoy).

Está pensado para **consumirse dentro de `/loop`** ítem por ítem, o repartirse
entre varios agentes.

> **Nota de desambiguación:** este "tablero" **no** es el tablero de ajedrez de
> calibración de distorsión (aquel se eliminó del backlog anterior a petición
> del usuario). Aquí *tablero* = **sistema de referencia visual centrado** para
> medir posición y orientación, al estilo del *reference frame / datum* de la
> metrología industrial.

---

## Diagnóstico del estado actual (verificado en código, 2026-07-27)

Punto de partida real, para no redescubrirlo en cada iteración:

1. **No hay zoom ni paneo.** `EditorCanvas::targetRect()` hace *aspect-fit* puro
   (`target.scale(size(), Qt::KeepAspectRatio)` + centrado). No existe
   `wheelEvent`.
2. **Hay un único punto de conversión de coordenadas**: `imageToWidget()` y
   `widgetToImage()`, ambos derivados de `targetRect()`. **Casi todo el pintado
   y todo el ratón pasan por ahí** → el zoom se implementa modificando *solo*
   esas tres funciones (+ rueda/paneo). Esto hace el ítem barato y de bajo
   riesgo.
3. ~~Dos excepciones que no pasan por `imageToWidget`~~ — **CORREGIDO al
   implementar Z1 (2026-07-27)**: los dos sitios que no usan `imageToWidget`
   (`paintLiveOverlay()`, que hace `translate`+`scale`, y el radio del círculo en
   `paintTool()`) **sí derivan de `targetRect()`**, así que al aplicar el zoom
   dentro de `targetRect()` se ajustan solos. No hubo que tocarlos: el canvas
   entero cuelga de una única función.
4. **El canvas se usa en DOS modos**: imagen fija del editor (`setScene`) y
   vídeo en vivo de la ventana principal (`setFrame` + `setLivePiece`). El zoom
   debe funcionar en ambos y **no romper el anclaje** de las herramientas.
5. **El sistema "centro = 0" YA EXISTE internamente**: `vision::Fixture`
   (`origin` = centroide de la máscara + `angleDeg`) y
   `toPieceCoords()`/`toImageCoords()` definen coordenadas de pieza cuyo origen
   *es* el centro de la pieza. Las herramientas ya se guardan en ese sistema.
   **Lo que falta es hacerlo visible y medible**: no hay overlay de ejes/grilla
   ni ninguna lectura de "cuánto está descentrada / girada" la pieza.
6. **La escala px→mm ya existe** (`domain::ScaleCalibration`, ArUco en vivo,
   homografía por-punto): el tablero puede rotularse en mm cuando hay
   calibración y en px cuando no, reutilizando `formatLength`.
7. **El esquema de BD está en v4** (`kMigrationV4`), con patrón de migración
   claro en `database/schema.cpp` → el modo de medición por pieza entra como
   **`kMigrationV5`** (`ALTER TABLE Pieces ADD COLUMN measurement_mode …`).
8. **El registro (`onRegisterLiveClicked`) no pregunta nada de medición**: pide
   nombre, captura 30 referencias y guarda. Ahí es donde entra la elección de
   modo.

---

## Principios (heredados, no negociables)

1. **No reestructurar la arquitectura por capas** (`core/ camera/ vision/ ml/
   database/ domain/ inspection_editor/ engine/ repositories/ ui/`).
2. **Compilar y probar de verdad** antes de dar un ítem por terminado:
   `cmake --build --preset mingw-release` limpio (`-Werror`) +
   `ctest --preset mingw-release` en verde + smoke test. Matar
   `pc_inspector.exe` / `pci_tests.exe` / `ctest` colgados antes de recompilar
   (bloquean el `.exe` con "Permission denied" al enlazar).
3. **Un test por lógica nueva no trivial**. La **geometría del tablero es lógica
   pura** (sin Qt): va en `vision/` o `domain/` y **se testea** (ideal para
   `test-driven-development`). Los widgets de Qt puros no llevan test unitario.
4. **Commits atómicos por ítem**, mensaje en español, **sin firma**. Evitar
   comillas dobles en el mensaje al usar here-strings de PowerShell.
5. **Actualizar README y memoria** al cerrar cada ítem.
6. Ítems marcados **(pide confirmación)** cambian el flujo del operador:
   preguntar con `AskUserQuestion` antes, no asumir.

### Principios de UX para esta ronda (que sea *intuitivo*)

- **El zoom no debe sorprender**: rueda = zoom **hacia el cursor** (no al centro
  de la ventana), el contenido bajo el puntero se queda quieto.
- **Siempre hay salida**: un gesto para "ajustar a ventana" y otro para "100%",
  visibles y con atajo. Nunca se puede perder la imagen fuera de vista.
- **El tablero informa, no estorba**: se puede ocultar, no tapa la pieza (líneas
  finas/semitransparentes) y sus etiquetas van en la unidad activa (mm o px).
- **El modo se ve de un vistazo**: el operador debe saber siempre en qué modo
  está sin abrir un diálogo.
- **Valores por defecto sensatos**: quien no toque nada debe seguir trabajando
  exactamente como hoy (modo Real, tablero oculto, zoom 100 % ajustado).

---

## Backlog

Formato: casilla, ID, tarea, por qué, skills, archivos.

### Z. Zoom y navegación del lienzo

- [x] **Z1 — Zoom con la rueda, centrado en el cursor**. HECHO. `zoom_` + `pan_`
  en `EditorCanvas`; `fitRect()` (encuadre ajustado) y `targetRect()` (= fit con
  zoom y desplazamiento) — `imageToWidget`/`widgetToImage` y todo el pintado
  cuelgan de ahí, así que el zoom se propagó **solo** al resto del canvas
  (incluidas las supuestas excepciones, ver diagnóstico 3 corregido).
  `wheelEvent` aplica ×1.15 por muesca y `zoomAt()` corrige el desplazamiento
  para que **el punto bajo el cursor no se mueva**.
  Decisiones tomadas: **límites 1×–20×** (por debajo del ajuste no aporta en
  inspección: el ajuste ya muestra la imagen entera); al volver a 1× la vista se
  **recentra sola**; `setScene` conserva el zoom si la imagen mantiene el tamaño
  (para que "Actualizar desde cámara" no lo pierda) y `clearLive` lo reinicia.
  Skills: `qt-cpp-review`, `qt-ui-design`, `systematic-debugging` (los bugs de
  transformación son sutiles: comprobar que dibujar/mover/handles siguen
  cayendo donde deben con zoom ≠ 1).
  Archivos: `inspection_editor/canvas/editor_canvas.{h,cpp}`.

- [x] **Z2 — Paneo (arrastrar la vista)**. HECHO. Arrastre con **botón central**
  o **Ctrl + botón izquierdo** (se eligió Ctrl en vez de la barra espaciadora:
  no requiere que el canvas tenga el foco del teclado ni compite con los atajos
  de la ventana), con cursor de mano mientras dura y **sin robarle el arrastre
  al dibujo ni al marco de selección**. `clampedPan()` limita el
  desplazamiento para que nunca se descubra el fondo; si con el zoom actual la
  imagen cabe en un eje, en ese eje queda centrada. El límite se aplica dentro
  de `targetRect()`, así que **redimensionar la ventana tampoco puede dejar la
  imagen fuera de vista**. Funciona con la edición bloqueada (mirar no edita).

- [x] **Z3 — Controles y atajos de vista**. HECHO. **Barra de zoom visible**
  (pedida por el usuario: *"para el zoom tambien agregale la opcion de + y
  menos, min y max"*) en la barra inferior de la ventana principal y bajo el
  lienzo del editor de plantillas: `⤢` (mínimo = ajustar a la ventana), `−`,
  **porcentaje**, `+`, `⛶` (máximo, 20×). Los botones se deshabilitan solos al
  llegar a cada tope, así que el operador **ve** dónde está el límite en vez de
  descubrirlo pulsando. Atajos: `Ctrl++` / `Ctrl+-` (secuencias estándar de Qt),
  `Ctrl+0` mínimo/ajustar, `Ctrl+1` 100 % (píxeles reales), `Ctrl+2` máximo, y
  **doble clic = ajustar**. El zoom se reinicia al cambiar de pieza.
  Decisiones: el porcentaje mostrado es la **escala real** (px de pantalla por
  px de imagen), no el factor interno `zoom_` — así "100 %" significa lo que el
  operador espera y el ajuste a ventana puede leerse, p. ej., 62 %.
  `EditorCanvas` emite `viewChanged()` (también al redimensionar, porque cambia
  el encuadre ajustado) y expone `displayScale()` / `atMinZoom()` / `atMaxZoom()`
  para que la UI no duplique la aritmética del zoom.
  Archivos: `editor_canvas.{h,cpp}`, `ui/main_window.{h,cpp}`,
  `inspection_editor/editor_window.cpp`.

### T. Tablero de referencia centrado (centro = 0)

- [x] **T1 — Geometría del tablero (lógica pura, con test)**. HECHO. Nuevo
  `vision/board_frame.{h,cpp}` (o `domain/`): dado un **origen** y un **ángulo
  de referencia**, convierte imagen ↔ tablero y expone la lectura de un punto
  como **(dx, dy, radio, ángulo)**, en px y en mm si hay escala. Sin Qt:
  testeable.

  **DECIDIDO por el usuario (2026-07-27): el origen es ELEGIBLE**, con los tres
  modos implementados —
  **(a) centro de la pieza** (`Fixture.origin`, sigue a la pieza: mide
  desviaciones internas respecto a su propio centro),
  **(b) centro de la imagen** (fijo en pantalla: mide cuánto se desvía la pieza
  respecto al centro del campo de visión, para centrado en un jig) y
  **(c) punto fijado a mano** por el operador.
  El enum del origen vive junto a la geometría del tablero y la elección se
  persiste (con la pieza, junto al modo de medición de M1).
  Skills: `test-driven-development` (test primero), `cpp-testing`,
  `computer-vision-opencv`, `cpp-coding-standards`.
  Archivos: `vision/board_frame.*` (nuevo), `tests/test_vision.cpp`.

  **Cómo quedó**: `BoardConfig{origin, fixedPoint, followPieceAngle}` (lo que
  elige el operador y se persistirá en M1) → `resolveBoardFrame(config, fixture,
  pieceFound, imageSize)` → `BoardFrame{origin, angleDeg}` para el frame actual.
  Lecturas: `readPoint`, `toImagePoint` (inversa exacta), `readPiece` y
  `pieceAngleOffset`, más `toMillimeters` y `niceGridStep` (escalones 1-2-5, ya
  listo para la grilla adaptativa de T2) y `originKey`/`originFromKey` para
  persistir. **Convenios fijados**: +X derecha, **+Y arriba** (se invierte la y
  de la imagen), ángulo en (−180, 180] antihorario positivo; el ángulo del
  tablero usa el mismo convenio que `Fixture::angleDeg`.
  **Decisiones de robustez**: sin pieza detectada, el origen "centro de pieza"
  cae al centro de la imagen y los ejes dejan de girar (el tablero sigue siendo
  usable justo cuando falla la detección); sin calibración, `toMillimeters`
  devuelve píxeles en vez de inventar milímetros.
  8 tests nuevos (146/146). Sin cambios en el README: aún no hay nada visible
  para el operador (llega en T2).

- [x] **T2 — Overlay del tablero en el lienzo**. HECHO. Dibujar ejes X/Y y grilla con
  el **0 en el centro**, paso adaptativo (1/5/10/25 mm o px según zoom, para que
  nunca sature), etiquetas en la unidad activa, líneas finas semitransparentes
  y **cuadrante marcado** (+X derecha, +Y arriba, como en metrología). Activable
  desde **Ver ▸ Tablero de referencia**, persistido.
  Skills: `qt-cpp-review`, `qt-ui-design`.
  Archivos: `editor_canvas.{h,cpp}`, `ui/main_window.cpp` (acción de menú).

  **Cómo quedó**: `EditorCanvas::setBoardVisible/setBoardConfig/boardFrame()` +
  `paintBoard()`, que se pinta justo tras la imagen (queda **por debajo** de la
  pieza y de las herramientas). El paso se elige en la unidad activa a partir de
  las cuatro esquinas de la vista, apuntando a una línea cada ~90 px de
  pantalla, con salvaguarda de 500 líneas por eje. Menú **Ver ▸ Tablero de
  referencia (centro = 0)** + submenú **Origen del tablero** (pieza / imagen /
  punto fijado a mano, este último se marca con un clic reutilizando el modo de
  selección de punto) y **Ejes girados con la pieza**; todo persistido en
  Settings (`board_visible`, `board_origin`, `board_follow`, `board_fixed_x/y`).
  **Verificado con render offscreen** (arnés desechable en el scratchpad que
  instancia el canvas y guarda PNG): eso destapó tres defectos que el compilador
  no ve y se corrigieron —
  (1) `niceGridStep` redondeaba **hacia arriba** y dejaba la grilla a la mitad de
  densidad: ahora elige el escalón 1-2-5 **más cercano** (cortes en las medias
  geométricas; test actualizado);
  (2) las etiquetas usaban `formatLength`, que añade el equivalente en px entre
  paréntesis (`-50.00mm (200.0px)`) y saturaba: ahora son compactas (`+20.0 mm`);
  (3) las marcas **+X/+Y** se salían de la vista con los ejes girados: ahora se
  colocan donde cada semieje positivo cruza el borde.

- [x] **T3 — Lectura en vivo de posición y ángulo de la pieza**. HECHO. Mostrar de
  forma continua **cuánto está descentrada y girada** la pieza respecto al
  origen del tablero: `dx`, `dy`, radio y ángulo (con unidades). Reutiliza
  `Fixture.origin` / `angleDeg`, que ya se calculan cada frame. Se muestra en el
  panel lateral o junto al banner, no en un diálogo.
  Skills: `qt-cpp-review`, `computer-vision-opencv`.
  Archivos: `ui/main_window.cpp` (usa `AnalysisOverlay`), `analysis_overlay.h`.

  **Cómo quedó**: banda `boardReadoutLabel_` justo debajo del banner de
  veredicto (nunca un diálogo), visible solo con el tablero encendido, que se
  actualiza en cada análisis con `readPiece` + `pieceAngleOffset` sobre el mismo
  `boardFrame()` que se está dibujando. Unidades: las activas de la UI.
  **Decisión de honestidad**: con el origen *en la propia pieza* la desviación
  es 0 por definición, así que en ese modo no se muestra un "0,0" que parecería
  un fallo, sino "el cero viaja con la pieza" + el giro. Al cortar la
  transmisión se limpia el fixture y la banda pasa a "sin pieza detectada".
  No hizo falta tocar `AnalysisOverlay`: el centroide y el ángulo ya viajaban en
  él y `liveFixture_` ya se construye con ellos.

- [x] **T4 — Coordenadas bajo el cursor**. HECHO. Mientras el ratón está sobre el
  lienzo con el tablero activo, mostrar las coordenadas del punto en el sistema
  centrado (`x=+12.4 mm  y=−3.1 mm`). Requiere `setMouseTracking(true)` (hoy
  está en `false`) — comprobar que no penaliza el repintado en vivo.
  Skills: `qt-cpp-review`, `systematic-debugging`.
  Archivos: `editor_canvas.{h,cpp}`.

  **Cómo quedó**: recuadro junto al cursor con `x` e `y` en el sistema centrado
  y en la unidad activa, dibujado al final de `paintBoard` (se voltea al otro
  lado si se saldría de la vista). **`setMouseTracking` se enciende y se apaga
  con el tablero**, no de forma permanente: sin tablero no hay ni un repintado
  extra, y con tablero en vivo el lienzo ya se repinta por frame. `leaveEvent`
  borra la lectura al salir del lienzo. El formateo compacto de T2 se extrajo a
  `boardValueText()` y ahora lo comparten la grilla y el cursor. Verificado con
  el mismo render offscreen inyectando un `QMouseEvent`: con origen en el centro
  de la imagen y ejes a 20°, el cursor sobre (620, 200) del widget da
  `x +22.3 mm  y +32.0 mm`, que coincide con el cálculo a mano.

  **Limitación conocida (a revisar en G-B)**: el tablero solo se enciende desde
  la ventana principal; el diálogo del editor de plantilla no tiene barra de
  menú y de momento se queda sin él.

- [x] **T5 — Herramienta "Posición" (`ToolType::Position`)**. HECHO. Herramienta nueva
  siguiendo el patrón ya establecido (T1–T6 del backlog anterior): marca un
  punto/rasgo y **mide su desviación respecto al origen del tablero** (dx, dy o
  radio+ángulo, elegible), con tolerancias propias. Es lo que convierte el
  tablero en un criterio OK/NG y no solo en una ayuda visual.
  Skills: `cpp-coding-standards`, `cpp-testing`.
  Archivos: `inspection_editor/tools/tool_{types,geometry}.*`,
  `execution/tool_executor.*`, `canvas/{editor_canvas,tool_icons}.cpp`,
  `ui/main_window.cpp` (botón + atajo).

  **Cómo quedó**: `PositionGeometry{point, axis}` con `PositionAxis{Radial, X,
  Y}`; el punto vive en coordenadas de pieza (viaja con ella) y en cada frame se
  lleva a la imagen y se lee contra el tablero. `measured` = radio, |dx| o |dy|
  según el eje, así que las tolerancias de siempre (mín/máx) ya sirven de
  criterio OK/NG. Botón, icono de diana, atajo **0** y también modo en el editor
  de plantilla; el spin de parámetro pasa a ser el eje (1 radial / 2 X / 3 Y).
  **Decisión de contrato**: `runTool`/`runTools` reciben ahora un
  `const vision::BoardFrame*` opcional. Lo resuelven el análisis en vivo
  (`buildOverlay`), la sugerencia de tolerancias al crear la herramienta y
  **también `InspectionEngine`** (nuevo `EngineOptions::board` +
  `setBoardConfig`, que `MainWindow` mantiene sincronizado): si el motor no
  recibiera el mismo tablero, el veredicto no coincidiría con la lectura en
  vivo. Sin tablero se cae a un cero en la propia pieza con ejes de la imagen.
  **Honestidad sobre su alcance**: con el cero *en la pieza* la desviación de un
  rasgo anclado es constante; la descripción de la herramienta lo dice y
  recomienda centro de imagen o punto fijado. 3 tests nuevos (149/149) y render
  offscreen para comprobar el dibujo (diana + línea punteada al cero).

### M. Modos de medición y registro

- [x] **M1 — Modo de medición por pieza (esquema + dominio)**. HECHO *(la
  nomenclatura y el alcance los confirmó el usuario, ver abajo)*. Enum `MeasurementMode { Real, Special }`
  (nombres visibles a confirmar con el usuario: p. ej. *"Posición real
  (personalizada)"* vs *"Especial (tablero centrado)"*). Persistir por pieza con
  **`kMigrationV5`** (`ALTER TABLE Pieces ADD COLUMN measurement_mode TEXT NOT
  NULL DEFAULT 'real'`) + getter/setter en `PieceRepository`, siguiendo el
  patrón de `orientation_offset` (v3).
  Skills: `sqlite-database-expert`, `cpp-testing`.
  Archivos: `database/schema.cpp`, `repositories/piece_repository.*`,
  `domain/` (enum + nombres legibles), `tests/test_database.cpp`.

  **DECIDIDO por el usuario (2026-07-27)**: los modos se llaman **«Posición real
  (personalizada)»** y **«Especial (tablero centrado)»**, y el tablero
  (origen, punto fijado y ejes) se guarda **por pieza**, junto al modo — no como
  ajuste global. Al elegir la pieza, la UI se configurará sola (eso es M2).

  **Cómo quedó**: `domain/measurement_mode.{h,cpp}` con
  `MeasurementMode{Real, Special}`, `modeKey`/`modeFromKey` (claves estables
  `real`/`special`, con caída a Real si la clave es desconocida) y
  `modeLabel`/`modeDescription` para la UI. Lógica pura: `domain` NO puede
  depender de `vision` (es al revés), así que la pareja modo + tablero vive en
  `repositories::PieceMeasurement{mode, board}` con
  `savemeasurement`/`loadMeasurement`. `kMigrationV5` añade a `Pieces`:
  `measurement_mode`, `board_origin`, `board_fixed_x/y` y `board_follow_angle`,
  con valores por defecto que **reproducen exactamente el comportamiento
  anterior**. `pci_repositories` pasa a enlazar `pci_vision` en PUBLIC porque su
  cabecera ya expone `vision::BoardConfig`.
  Verificado además sobre la **base de datos real** del build (que estaba en
  v4 con piezas guardadas): migró a v5 y las columnas nuevas están ahí.
  3 tests nuevos → 151/151.

- [x] **M2 — Elegir el modo al registrar**. HECHO *(flujo confirmado por el usuario)*. El
  registro (`onRegisterLiveClicked`) pregunta el modo junto con el nombre —o en
  el paso siguiente— y lo guarda con la pieza. Al seleccionar una pieza, la UI
  **refleja su modo**: en *Especial* el tablero aparece encendido y con las
  lecturas centradas; en *Real*, todo queda como hoy.
  Skills: `qt-ui-design`, `qt-cpp-review`.
  Archivos: `ui/main_window.cpp`, `ui/registration_wizard.*` (si se quiere
  también en el asistente).

  **DECIDIDO por el usuario (2026-07-27)**: (1) el modo se elige **al registrar
  Y se puede cambiar después** desde **Pieza ▸ Modo de medición…**; (2) al
  seleccionar una pieza **manda la pieza**: en Especial se aplica su tablero y
  se enciende, en Real se apaga.

  **Cómo quedó**: nuevo `ui/measurement_mode_dialog.*` (modo + origen del
  tablero + ejes girados, con la explicación de cada modo a la vista), usado en
  los dos sitios. En el registro aparece justo después del nombre y **se aplica
  ya**, para que el operador capture las 30 referencias viendo el tablero con el
  que se va a medir; cancelarlo cancela el registro. `applyMeasurement()` es el
  **único** punto que cambia el estado de medición (canvas, motor, visibilidad
  del tablero y menús, con `QSignalBlocker` para no reguardar en cascada) y
  `persistBoardConfig()` escribe en la pieza seleccionada y, además, en Settings
  como valor por defecto de sesión cuando aún no hay pieza. Al arrancar se llama
  `loadMeasurementForSelectedPiece()` porque `loadPieceList()` puede no disparar
  la señal del combo. Verificado con render offscreen del diálogo. 151/151.
  Nota: el asistente de registro (`registration_wizard`) **no** pregunta el
  modo; se queda con el valor por defecto y se cambia luego desde el menú.

- [x] **M3 — Indicador de modo siempre visible**. HECHO. Que el operador nunca dude en
  qué modo está: etiqueta junto al combo de pieza y/o en la barra de estado
  (mismo espíritu que los indicadores de S4), con tooltip explicando qué implica
  cada modo.
  Skills: `qt-ui-design`.
  Archivos: `ui/main_window.cpp`.

  **Cómo quedó**: etiqueta tipo *chip* **junto al combo de pieza** (donde se
  decide el modo, no perdida en la barra inferior): gris «Posición real» o cian
  «Especial (tablero)», con el mismo cian del tablero para que se asocien de un
  vistazo. El tooltip explica qué implica el modo y **dónde se cambia**
  (Pieza ▸ Modo de medición…). Se actualiza desde `applyMeasurement()`, que ya
  es el único punto que cambia el estado, más una llamada al arrancar para que
  el chip no salga vacío cuando no hay pieza seleccionada.

- [ ] **M4 — Reglas específicas del modo Especial**. Lo que hace que "especial"
  signifique algo: **tolerancias de centrado y orientación por pieza**
  (desviación máx. en X/Y o radial, y desviación angular máx.) que se evalúan en
  cada inspección y **entran en el veredicto OK/NG** junto a las herramientas.
  Reutiliza `domain::combineVerdict` añadiendo un check nuevo, sin tocar su
  contrato con las herramientas.
  Skills: `cpp-testing`, `test-driven-development`, `sqlite-database-expert`
  (dónde guardar las tolerancias: columnas en `Pieces` o tabla propia).
  Archivos: `domain/verdict.*`, `engine/inspection_engine.*`,
  `repositories/piece_repository.*`, `ui/` (edición de esas tolerancias).

### G. Cierre de la ronda

- [x] **G-A — Revisión de diseño antes de M4**. HECHO. Antes de cerrar el bloque M,
  pasar el diseño de los dos modos por `grill-me` (desafiar supuestos: ¿qué pasa
  con piezas simétricas? ¿el tablero se ancla a la imagen o a la pieza? ¿qué ve
  el operador si cambia de modo con herramientas ya dibujadas?) y por
  `qt-cpp-review` los widgets nuevos.
  Skills: `grill-me`, `qt-cpp-review`.

  **Nota de método**: la skill `grill-me` es una *entrevista* con
  `disable-model-invocation: true` — la lanza el usuario, no el agente. La
  revisión se hizo igualmente, respondiendo a las tres preguntas del plan, y
  **dos hallazgos se corrigieron en el acto**:

  1. *¿Qué pasa con piezas simétricas?* Con «ejes girados con la pieza», el
     ángulo del fixture puede saltar 180° en piezas simétricas y la desviación
     angular daría ±180 espurios. Ya hay tres mitigaciones en el proyecto
     (rasgo distintivo, `FixtureStabilizer` y la anisotropía del fixture), pero
     **`buildOverlay` construye `liveFixture_` sin propagar `anisotropy`**, así
     que la UI no puede saber si el ángulo es de fiar. → **Pendiente para M4**:
     propagar la anisotropía y no aplicar la tolerancia angular cuando el eje no
     está definido, en vez de dar NG falsos.
  2. *¿El tablero se ancla a la imagen o a la pieza?* Es elegible, pero la
     combinación **Especial + cero en la pieza no mide desviación de posición**
     (es 0 por definición). → **CORREGIDO**: el diálogo de modo muestra un aviso
     naranja en cuanto se elige esa combinación, explicando qué usar en su lugar.
  3. *¿Qué ve el operador si cambia de modo con herramientas ya dibujadas?* Las
     herramientas siguen midiendo, pero las de **Posición** tenían tolerancias
     sugeridas respecto al cero anterior: al cambiar el origen medirían otra cosa
     **en silencio**. → **CORREGIDO**: al cambiar el origen (desde el diálogo o
     desde Ver ▸ Origen del tablero) se avisa con un cuadro de diálogo indicando
     cuántas herramientas de Posición hay que revisar.

- [ ] **G-B — README + memoria + limpieza**. Documentar zoom, tablero y modos en
  el README (sección de uso, con los atajos), y una pasada de coherencia sobre
  lo añadido.
  Skills: `qt-cpp-docs` (si `board_frame` crece lo suficiente).

---

## Skills disponibles y cuándo usarlas

**Instaladas en el proyecto** (`.agents/skills/`) — incluye dos añadidas en esta
ronda:

| Skill | Úsala para |
|---|---|
| `computer-vision-opencv` | **(nueva)** T1, T3, T5 — coordenadas, homografía, medidas en mm. |
| `qt-ui-design` | **(nueva, de The Qt Company)** Z1–Z3, T2, M2/M3 — que la UI sea intuitiva. |
| `test-driven-development` | T1 y M4: lógica pura nueva → test primero. |
| `cpp-testing` | Todo ítem con lógica no trivial antes de darlo por hecho. |
| `cpp-coding-standards` | Cualquier código C++ nuevo. |
| `qt-cpp-review` | Pasada de revisión sobre canvas/diálogos antes de cerrar cada ítem. |
| `sqlite-database-expert` | M1 y M4 (migración v5, nuevas columnas). |
| `systematic-debugging` | Z1 y T4: bugs de transformación de coordenadas y de repintado. |
| `qt-cpp-docs` | G-B, si `board_frame` merece referencia aparte. |
| `cmake` | Al añadir archivos nuevos (`board_frame.*`) a los targets. |
| `grill-me` | G-A: desafiar el diseño de los dos modos antes de implementar M4. |
| `find-skills` | Buscar/instalar más skills si aparece un hueco. |

Descartadas tras buscar en el ecosistema: las skills de UX genérico encontradas
tienen <100 instalaciones y autores desconocidos; `pyqt6-ui-development-rules`
es de Python, no aplica a Qt/C++.

---

## Plan de ejecución con múltiples agentes

Los dos cuellos de botella son **`editor_canvas.cpp`** (zoom, overlay, cursor) y
**`main_window.cpp`** (cableado). Por tanto:

| Agente | Ítems | Archivos (disjuntos) |
|---|---|---|
| A | Z1–Z3 | `canvas/editor_canvas.*` (dueño exclusivo del canvas en esta fase) |
| B | T1 | `vision/board_frame.*` (nuevo) + `tests/test_vision.cpp` |
| C | M1 | `database/schema.cpp`, `repositories/piece_repository.*`, `tests/` |

T2/T4 (overlay y cursor) **no pueden ir en paralelo con Z1–Z3**: los tres tocan
el mismo canvas → van **después**, en serie, sobre el zoom ya terminado. M2–M4 y
T3/T5 se integran en serie en `main_window`/`engine`. Cada agente compila, corre
`ctest` y deja el árbol **sin commitear**; la sesión principal integra y hace
**un commit por ítem**.

---

## Uso dentro de `/loop`

Modo dinámico (`/loop` sin intervalo). En cada iteración: abrir este archivo,
tomar el **primer ítem sin marcar** en el orden **Z → T → M → G**, implementarlo
completo (código + test + build + ctest + smoke test + README + memoria),
**marcar su casilla `[x]`**, commit + push **sin firma**, y solo entonces pasar
al siguiente. Parar cuando no queden casillas o con `ScheduleWakeup stop:true`.

El orden **no es negociable en su tramo inicial**: Z antes que T (el tablero fino
sin zoom no se puede leer) y T antes que M (el modo Especial no significa nada
sin tablero).

---

## Fuentes / referencias de coherencia

- **Reference frame / fixturing** en visión industrial (Cognex VisionPro,
  MVTec Halcon): las herramientas se anclan a un sistema de coordenadas de la
  pieza en vez de a píxeles absolutos — es exactamente lo que ya hace
  `vision::Fixture`, y el tablero simplemente lo **hace visible y medible**.
- **Datum / origen** de la metrología dimensional: medir desviaciones respecto a
  un origen declarado (aquí, el centro) en vez de entre puntos sueltos.
- El resto sale del **diagnóstico del código de este repo** (ver arriba), no de
  alcance inventado.
