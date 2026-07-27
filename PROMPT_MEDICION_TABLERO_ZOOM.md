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
3. **Dos excepciones** que **no** pasan por `imageToWidget` y hay que ajustar a
   mano al meter zoom (si no, se descuadran):
   - `paintLiveOverlay()`: usa `painter.translate(target.topLeft())` +
     `painter.scale(target.width()/image_.width(), …)` directo.
   - El **radio del círculo** en `paintTool()`: usa
     `targetRect().width() / image_.width()` como factor de escala.
   (Un patrón alternativo, más limpio: centralizar el factor en un helper
   `viewScale()` y que ambos lo usen.)
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

- [ ] **Z1 — Zoom con la rueda, centrado en el cursor**. Añadir `zoomFactor_` y
  `panOffset_` a `EditorCanvas`; `targetRect()` los aplica sobre el rect
  aspect-fit actual y `imageToWidget`/`widgetToImage` siguen siendo el único
  punto de conversión (ver diagnóstico 2). `wheelEvent` multiplica el factor
  (p. ej. ×1.15 por muesca, límites 0.2×–20×) **ajustando el paneo para que el
  punto bajo el cursor no se mueva**. Arreglar de paso las **dos excepciones**
  del diagnóstico 3.
  Skills: `qt-cpp-review`, `qt-ui-design`, `systematic-debugging` (los bugs de
  transformación son sutiles: comprobar que dibujar/mover/handles siguen
  cayendo donde deben con zoom ≠ 1).
  Archivos: `inspection_editor/canvas/editor_canvas.{h,cpp}`.

- [ ] **Z2 — Paneo (arrastrar la vista)**. Con zoom > ajuste, permitir mover la
  imagen: arrastre con **botón central** o **espacio + arrastre izquierdo**
  (sin robarle el arrastre al dibujo), cursor de mano mientras dura, y
  **límites** para que la imagen no se pierda fuera del widget.
  Skills: `qt-cpp-review`, `qt-ui-design`.
  Archivos: `editor_canvas.{h,cpp}`.

- [ ] **Z3 — Controles y atajos de vista**. `Ctrl++` / `Ctrl+-` zoom,
  `Ctrl+0` = ajustar a ventana, `Ctrl+1` = 100 %, doble clic = ajustar.
  Indicador del **porcentaje de zoom** en la barra de estado (junto a los
  indicadores de S4) y reset del zoom al cambiar de pieza/imagen.
  Skills: `qt-cpp-review`.
  Archivos: `editor_canvas.{h,cpp}`, `ui/main_window.cpp` (atajos + indicador).

### T. Tablero de referencia centrado (centro = 0)

- [ ] **T1 — Geometría del tablero (lógica pura, con test)**. Nuevo
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

- [ ] **T2 — Overlay del tablero en el lienzo**. Dibujar ejes X/Y y grilla con
  el **0 en el centro**, paso adaptativo (1/5/10/25 mm o px según zoom, para que
  nunca sature), etiquetas en la unidad activa, líneas finas semitransparentes
  y **cuadrante marcado** (+X derecha, +Y arriba, como en metrología). Activable
  desde **Ver ▸ Tablero de referencia**, persistido.
  Skills: `qt-cpp-review`, `qt-ui-design`.
  Archivos: `editor_canvas.{h,cpp}`, `ui/main_window.cpp` (acción de menú).

- [ ] **T3 — Lectura en vivo de posición y ángulo de la pieza**. Mostrar de
  forma continua **cuánto está descentrada y girada** la pieza respecto al
  origen del tablero: `dx`, `dy`, radio y ángulo (con unidades). Reutiliza
  `Fixture.origin` / `angleDeg`, que ya se calculan cada frame. Se muestra en el
  panel lateral o junto al banner, no en un diálogo.
  Skills: `qt-cpp-review`, `computer-vision-opencv`.
  Archivos: `ui/main_window.cpp` (usa `AnalysisOverlay`), `analysis_overlay.h`.

- [ ] **T4 — Coordenadas bajo el cursor**. Mientras el ratón está sobre el
  lienzo con el tablero activo, mostrar las coordenadas del punto en el sistema
  centrado (`x=+12.4 mm  y=−3.1 mm`). Requiere `setMouseTracking(true)` (hoy
  está en `false`) — comprobar que no penaliza el repintado en vivo.
  Skills: `qt-cpp-review`, `systematic-debugging`.
  Archivos: `editor_canvas.{h,cpp}`.

- [ ] **T5 — Herramienta "Posición" (`ToolType::Position`)**. Herramienta nueva
  siguiendo el patrón ya establecido (T1–T6 del backlog anterior): marca un
  punto/rasgo y **mide su desviación respecto al origen del tablero** (dx, dy o
  radio+ángulo, elegible), con tolerancias propias. Es lo que convierte el
  tablero en un criterio OK/NG y no solo en una ayuda visual.
  Skills: `cpp-coding-standards`, `cpp-testing`.
  Archivos: `inspection_editor/tools/tool_{types,geometry}.*`,
  `execution/tool_executor.*`, `canvas/{editor_canvas,tool_icons}.cpp`,
  `ui/main_window.cpp` (botón + atajo).

### M. Modos de medición y registro

- [ ] **M1 — Modo de medición por pieza (esquema + dominio)** *(pide
  confirmación de nomenclatura)*. Enum `MeasurementMode { Real, Special }`
  (nombres visibles a confirmar con el usuario: p. ej. *"Posición real
  (personalizada)"* vs *"Especial (tablero centrado)"*). Persistir por pieza con
  **`kMigrationV5`** (`ALTER TABLE Pieces ADD COLUMN measurement_mode TEXT NOT
  NULL DEFAULT 'real'`) + getter/setter en `PieceRepository`, siguiendo el
  patrón de `orientation_offset` (v3).
  Skills: `sqlite-database-expert`, `cpp-testing`.
  Archivos: `database/schema.cpp`, `repositories/piece_repository.*`,
  `domain/` (enum + nombres legibles), `tests/test_database.cpp`.

- [ ] **M2 — Elegir el modo al registrar** *(pide confirmación de flujo)*. El
  registro (`onRegisterLiveClicked`) pregunta el modo junto con el nombre —o en
  el paso siguiente— y lo guarda con la pieza. Al seleccionar una pieza, la UI
  **refleja su modo**: en *Especial* el tablero aparece encendido y con las
  lecturas centradas; en *Real*, todo queda como hoy.
  Skills: `qt-ui-design`, `qt-cpp-review`.
  Archivos: `ui/main_window.cpp`, `ui/registration_wizard.*` (si se quiere
  también en el asistente).

- [ ] **M3 — Indicador de modo siempre visible**. Que el operador nunca dude en
  qué modo está: etiqueta junto al combo de pieza y/o en la barra de estado
  (mismo espíritu que los indicadores de S4), con tooltip explicando qué implica
  cada modo.
  Skills: `qt-ui-design`.
  Archivos: `ui/main_window.cpp`.

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

- [ ] **G-A — Revisión de diseño antes de M4**. Antes de cerrar el bloque M,
  pasar el diseño de los dos modos por `grill-me` (desafiar supuestos: ¿qué pasa
  con piezas simétricas? ¿el tablero se ancla a la imagen o a la pieza? ¿qué ve
  el operador si cambia de modo con herramientas ya dibujadas?) y por
  `qt-cpp-review` los widgets nuevos.
  Skills: `grill-me`, `qt-cpp-review`.

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
