# PROMPT — Ronda de mejoras: herramientas de trazado, opciones, secciones y calibración

Documento de planificación para la siguiente ronda de trabajo sobre **PC
Inspector**. Es la continuación natural de
[PROMPT_MAESTRO_PC_INSPECTOR.md](PROMPT_MAESTRO_PC_INSPECTOR.md) (las 6 fases
base, completadas) y de las rondas de pulido posteriores documentadas en el
[README](README.md) y en la memoria del proyecto. Está pensado para
**pegarse tal cual como prompt** en una sesión nueva, repartirse entre
**varios agentes**, o consumirse **dentro de `/loop`** ítem por ítem.

No inventa alcance nuevo: cada ítem sale de una limitación ya documentada en
el README ("Limitaciones conocidas" de cada fase), de una promesa del prompt
original nunca cumplida, o de un patrón real de software de visión industrial
(Cognex VisionPro, MVTec Halcon) usado como referencia — con la fuente citada
en cada caso.

---

## Principios (heredados, no negociables)

1. **No reestructurar la arquitectura por capas** (`core/ camera/ vision/ ml/
   database/ domain/ inspection_editor/ engine/ repositories/ ui/`). Todo lo
   de este documento se implementa *dentro* de esa estructura.
2. **Compilar y probar de verdad** antes de dar un ítem por terminado:
   `cmake --build --preset mingw-release` limpio (sin warnings, `-Werror`
   activo) + `ctest --preset mingw-release` en verde + smoke test de la app
   (abre y no se cierra sola).
3. **Un test por lógica nueva no trivial** (geometría, cálculo, repositorio).
   Los widgets de Qt puros no necesitan test unitario; su lógica de datos sí.
4. **Commits atómicos por ítem**, mensaje en español, formato ya establecido
   en este repo (`git log` tiene 25+ ejemplos a seguir).
5. **Actualizar README y memoria** al cerrar cada ítem — son la fuente de
   verdad para la siguiente sesión/agente.
6. Ítems marcados **(pide confirmación)** son cambios de comportamiento
   visible o de flujo de trabajo del operador: preguntar antes con
   `AskUserQuestion`, no asumir.

---

## Backlog

Formato: casilla para ir marcando, ID, tarea, por qué, skill(s) a usar,
archivos principales que toca.

### A. Herramientas de trazado

- [x] **T1 — Línea-Línea (Line-to-Line)**. El prompt original pedía
  *"Point-to-Line / Line-to-Line: distancia o ángulo entre puntos y líneas de
  referencia"*; solo se implementó Punto-Línea. Cognex VisionPro y Halcon
  tratan "line-to-line angle/distance" como herramienta separada del caliper.
  Añadir `ToolType::LineToLine`: dos líneas de referencia (4 puntos), mide
  **distancia** entre ellas (paralelas) y **ángulo** (si no lo son).
  Skills: `cpp-coding-standards`, `cpp-testing`, `qt-cpp-review`.
  Archivos: `inspection_editor/tools/tool_{types,geometry}.*`,
  `inspection_editor/execution/tool_executor.*`,
  `inspection_editor/canvas/{editor_canvas,tool_icons}.cpp`,
  `src/ui/main_window.*` (solo para cablear el botón nuevo).

- [x] **T2 — Herramienta de Ángulo**. Complementa T1: ángulo de una esquina
  (vértice + dos puntos) o entre dos segmentos sueltos, salida directa en
  grados con tolerancia en grados (no en px). Común en catálogos de
  herramientas de medición de VisionPro/Halcon junto al caliper.
  Skills: `cpp-coding-standards`, `cpp-testing`.
  Archivos: igual que T1.

- [x] **T3 — Handles de edición fina**. Hoy solo se puede mover el conjunto
  completo de una herramienta; no se puede arrastrar un extremo individual
  (un punto del Caliper, el radio del Círculo, una esquina del rectángulo del
  Blob) sin borrar y volver a dibujar. Añadir "handles" (círculos pequeños en
  los puntos clave) que se puedan arrastrar independientemente en modo
  Mover/Elegir.
  Skills: `qt-cpp-review`.
  Archivos: `inspection_editor/canvas/editor_canvas.*`.

- [x] **T4 — Blob poligonal**. Hoy la región del Blob es un rectángulo
  alineado a los ejes de la pieza. Añadir una variante de región poligonal
  libre (clics sucesivos para cerrar el polígono) para zonas irregulares —
  patrón "polygon ROI" habitual en blob analysis industrial.
  Skills: `cpp-coding-standards`, `cpp-testing`.
  Archivos: `tool_geometry.*`, `tool_executor.cpp` (máscara con
  `cv::fillPoly` en vez de `fillConvexPoly` de rectángulo),
  `editor_canvas.cpp`.

- [ ] **T5 — Snap visual al borde más cercano**. Mientras se arrastra un
  Caliper/Regla/Borde liso, resaltar en vivo el borde detectado más cercano
  al cursor (usa `detectEdges` ya existente sobre una banda alrededor del
  puntero). Mejora directa de precisión al dibujar, inspirada en cómo
  VisionPro resalta candidatos de edge en su editor.
  Skills: `qt-cpp-review`.
  Archivos: `editor_canvas.cpp` (preview de creación),
  `execution/edge_detection.h` (reutilizar, no duplicar).

- [ ] **T6 — Duplicar / copiar-pegar herramienta**. Con varias plantillas por
  pieza ya soportadas (fase de plantillas múltiples), falta poder reutilizar
  una herramienta entre plantillas o piezas: `Ctrl+D` duplica la seleccionada
  con un pequeño desplazamiento; copiar/pegar entre plantillas de la misma
  pieza.
  Skills: `cpp-testing` (lógica de duplicado en `UndoStack`-friendly).
  Archivos: `main_window.cpp` (atajo + `ToolRepository::save` a otra
  plantilla), `editor_window.cpp` (mismo patrón dentro del editor).

### B. Opciones y configuración

- [ ] **O1 — Diálogo "Preferencias" unificado** *(pide confirmación de
  alcance)*. Hoy varios valores están fijos en código o dispersos: intervalo
  de auto-inspección (1000 ms fijo en `main_window.cpp`), `kSigma` de
  anomalía (3.0 fijo en `EngineOptions`), objetivo/mínimo de capturas del
  registro (30/5 fijos), tamaño de miniatura. Centralizarlos en
  `Cámara ▸ Preferencias…`, persistidos en `Settings` igual que el resto de
  ajustes.
  Skills: `sqlite-database-expert` (nuevas claves en `Settings`),
  `qt-cpp-review`.
  Archivos: `ui/preferences_dialog.*` (nuevo), `engine/*Options` (quitar
  los defaults hardcodeados donde tenga sentido parametrizar).

- [ ] **O2 — Controles reales de cámara (brillo/exposición/enfoque)**. Hoy no
  existe ningún control de la fuente: solo el post-proceso de
  `Detección…`. Exponer `CAP_PROP_BRIGHTNESS`, `EXPOSURE`, `FOCUS`,
  `AUTOFOCUS` de `cv::VideoCapture` en un panel simple; fallar en silencio
  (deshabilitar el control) si la cámara no soporta la propiedad — mismo
  patrón de `Result` + log ya usado en el proyecto.
  Skills: `computer-vision-opencv`, `systematic-debugging` (las propiedades
  de `VideoCapture` fallan distinto según el backend, hay que probarlo).
  Archivos: `camera/camera_controller.*`, `ui/main_window.cpp` (panel nuevo).

- [ ] **O3 — Perfiles de detección guardables**. Los ajustes de
  `Detección…` (umbral, polaridad, kernels) son un único set global. Permitir
  guardar/nombrar perfiles ("luz brillante", "contraluz") y elegir uno por
  pieza — reutiliza `SegmentationOptions`, solo cambia la persistencia (de
  una fila fija en `Settings` a una tabla `DetectionProfiles`).
  Skills: `sqlite-database-expert` (migración de esquema, seguir el patrón
  `kMigrationV5` de `database/schema.cpp`), `cpp-testing`.
  Archivos: `database/schema.*`, `repositories/` (repositorio nuevo o
  extender `SettingsRepository`), `ui/detection_dialog.*`.

- [ ] **O4 — Exportar/Importar configuración**. Volcar a un `.json` toda la
  config no ligada a una pieza concreta (calibración, detección, atajos,
  preferencias) para clonarla a otra PC de la línea sin repetir todo a mano.
  Skills: `cpp-coding-standards`.
  Archivos: `repositories/settings_repository.*` (método de export/import),
  `ui/main_window.cpp` (acciones de menú Archivo).

### C. Secciones de la interfaz

- [ ] **S1 — Pantalla de Historial de inspecciones**. `InspectionRepository`
  ya tiene `recentForPiece`/`todayStats` en el backend, pero **no hay ninguna
  UI para verlos** más allá del contador en la barra de estado. Añadir
  `Inspección ▸ Ver historial…`: tabla (fecha, veredicto, similitud,
  miniatura), filtro por pieza y rango de fechas, exportar a CSV.
  Skills: `sqlite-database-expert`, `qt-cpp-review`.
  Archivos: `ui/history_dialog.*` (nuevo), `repositories/
  inspection_repository.*` (añadir consulta paginada/filtrada si falta).

- [ ] **S2 — Panel de estadísticas simple**. Reutiliza la tabla
  `Statistics` (ya existe en el esquema desde la fase 4, sin consumidor de
  UI): conteo OK/NG por día y una tendencia simple (lista o barras con
  `QPainter`, sin librería de gráficos nueva).
  Skills: `qt-cpp-review`.
  Archivos: `ui/history_dialog.*` (puede vivir junto a S1) o
  `ui/stats_widget.*`.

- [ ] **S3 — Paneles reubicables (`QDockWidget`)**. El panel de comparación
  "Pieza registrada / actual" y la futura sección de historial hoy son fijos
  en el layout. Migrarlos a `QDockWidget` con `QMainWindow::saveState` /
  `restoreState` (persistido en `Settings`) para que el operador acomode la
  pantalla a su gusto y quede guardado.
  Skills: `qt-cpp-review`.
  Archivos: `ui/main_window.*`.

- [ ] **S4 — Barra de estado con indicadores de icono**. Hoy es texto plano.
  Añadir 3 indicadores pequeños (cámara / base de datos / modelo ONNX)
  verde-rojo con tooltip del detalle — mismo espíritu que los iconos ya
  dibujados en código para las herramientas (`tool_icons.cpp`), sin assets
  externos.
  Skills: `qt-cpp-review`.
  Archivos: `ui/main_window.cpp`.

### D. Calibración

- [ ] **D1 — Calibración ligada a cámara + resolución**. `calib_mm_per_px`
  hoy es **un valor global** en `Settings`. Si el operador cambia de cámara o
  de resolución, la escala queda obsoleta **sin aviso** — bug silencioso de
  precisión. Guardar la calibración con clave compuesta (backend+índice de
  cámara, ancho, alto) y avisar/ofrecer recalibrar si el frame actual no
  coincide con la resolución calibrada.
  Skills: `sqlite-database-expert`, `cpp-testing`.
  Archivos: `domain/calibration.*`, `repositories/settings_repository.*`,
  `ui/main_window.cpp`.

- [ ] **D2 — Calibración avanzada con tablero de ajedrez (distorsión de
  lente)**. Hoy se asume proyección *pinhole* sin distorsión (válido cerca
  del centro de la imagen, impreciso en los bordes con lentes gran angular).
  MVTec Halcon resuelve esto con `calibrate_cameras` + un tablero de
  calibración fotografiado en varias posiciones. Añadir un modo "avanzado"
  con `cv::findChessboardCorners` + `cv::calibrateCamera` que guarde
  coeficientes de distorsión y aplique `cv::undistort` antes de analizar cada
  frame. **Opcional/avanzado** — no bloquea el resto del backlog.
  Skills: `computer-vision-opencv`, `cpp-testing`.
  Archivos: `vision/lens_calibration.*` (nuevo), `vision/pipeline.*`
  (`cv::undistort` opcional antes de `segmentPiece`).

- [ ] **D3 — Tablero/multi-marcador ArUco (`cv::aruco::GridBoard`)**. La
  escala en vivo depende de **un solo marcador**; si la pieza o una mano lo
  tapan, se pierde. Un `GridBoard` de 4-6 marcadores sigue dando escala
  mientras se vea al menos uno.
  Skills: `computer-vision-opencv`, `cpp-testing`.
  Archivos: `vision/plane_scale.*`.

- [ ] **D4 — Conectar la homografía por-punto a las herramientas**.
  `planeDistanceMm` (homografía completa del plano) ya existe en
  `plane_scale.h` pero las herramientas solo usan la **escala local**
  (`mmPerPixel` constante). Con la cámara no perfectamente perpendicular al
  plano, la escala local pierde precisión lejos del marcador. Cablear
  `imageToMm` opcionalmente a `runTool`/`runTools` como alternativa a
  `mmPerPixel` cuando hay marcador ArUco activo.
  Skills: `computer-vision-opencv`, `cpp-testing`.
  Archivos: `inspection_editor/execution/tool_executor.*`,
  `ui/main_window.cpp`.

- [ ] **D5 — Indicador de calidad de calibración**. Mostrar el error de
  reproyección (método D2) o la dispersión de la escala medida en varios
  puntos del marcador (método A/ArUco) para que el operador sepa si calibró
  bien — hoy se confía ciegamente en el resultado.
  Skills: `computer-vision-opencv`.
  Archivos: `domain/calibration.*`, `ui/calibration_dialog.cpp`.

### E. Otras mejoras (cierre de huecos de rondas anteriores)

- [ ] **G1 — Registro "solo herramientas" sin modelo ONNX**. Hoy
  `onRegisterLiveClicked` exige `repos_.embedFn`; un operador que solo quiere
  medir (sin comparación de apariencia) no puede registrar una pieza. Permitir
  registrar sin embeddings cuando el modelo no esté disponible (guardar
  referencia vacía, el motor ya degrada a "solo herramientas" en
  `InspectionEngine::inspect`).
  Skills: `cpp-testing`.
  Archivos: `engine/registration_session.*`, `ui/main_window.cpp`.

- [x] **G2 — Nombres reales de cámara**. HECHO (2026-07-21), resuelto junto con
  el blindaje anti-crash de cámara que pidió el usuario. En vez de Media
  Foundation se usó **DirectShow COM** (`camera/native_cameras.cpp`), que además
  de dar el nombre real ("Integrated Camera", "DroidCam Source 3") enumera **sin
  abrir el dispositivo** — clave para no disparar el crash de drivers como
  `kswdmcap.ax`. Ver `core/crash_guard.*`, `tests/test_crash_guard.cpp` y la
  sección de limitaciones del README.

- [ ] **G3 — Empaquetado con `windeployqt6`**. Generar un `.zip`/instalador
  liviano que no dependa de tener MSYS2 en la PC de producción — el
  `CMakeLists.txt` ya tiene la lógica de copiar `onnxruntime.dll`, falta
  extender el mismo patrón a las DLL de Qt/OpenCV vía `windeployqt6` como
  paso de post-build opcional.
  Skills: `cmake`.
  Archivos: `CMakeLists.txt`, `run.ps1` (target `-Package`).

- [ ] **G4 — Auditoría con `/code-review`**. Antes de seguir sumando
  features, correr `/code-review` (o el skill `qt-cpp-review`) sobre el diff
  acumulado de todas las rondas anteriores. Puramente de revisión — no
  implementa nada nuevo, solo confirma que no se acumuló deuda técnica.
  Skills: `code-review`, `qt-cpp-review`.

---

## Skills disponibles y cuándo usarlas

Ya instaladas en el proyecto (`.agents/skills/`):

| Skill | Úsala para |
|---|---|
| `cpp-coding-standards` | Cualquier código C++ nuevo (guía de estilo del proyecto). |
| `cpp-testing` | Escribir/ajustar tests de GoogleTest antes de dar un ítem por hecho. |
| `test-driven-development` | Ítems de lógica pura nueva (geometría, cálculo): test primero. |
| `cmake` | G3 y cualquier cambio de `CMakeLists.txt`. |
| `qt-cpp-review` | Pasada de revisión sobre cualquier diálogo/widget nuevo antes de cerrar el ítem. |
| `qt-cpp-docs` | Si un módulo nuevo crece lo suficiente como para necesitar referencia aparte. |
| `sqlite-database-expert` | O1, O3, D1 (nuevas claves/tablas en `Settings`/esquema). |
| `systematic-debugging` | O2, G2 (APIs de hardware que fallan de formas no obvias). |

Disponibles en la sesión pero no instaladas en el proyecto — instalar con
`npx skills add <paquete>` si un ítem las necesita seguido:

| Skill | Úsala para |
|---|---|
| `computer-vision-opencv` | D2, D3, D4, O2 — todo lo que toque OpenCV/ArUco/calibración de cámara. |
| `run` | Levantar la app y probar cada ítem en la ventana real antes de cerrarlo. |
| `verify` | Confirmar que un ítem "arreglado" de verdad se comporta distinto en la app corriendo. |
| `simplify` | Pasada de limpieza tras varios ítems seguidos, antes de G4. |
| `code-review` | G4, o tras cualquier ítem grande (T1, D2) antes de commitear. |

---

## Plan de ejecución con múltiples agentes

El archivo con más tráfico de todo el proyecto es `src/ui/main_window.cpp`
(~1900 líneas, crece con cada feature) — casi todo lo nuevo termina cableado
ahí. Eso fija cómo hay que repartir el trabajo: **los agentes en paralelo
solo deben tocar archivos nuevos o aislados; el cableado final en
`main_window.*` se hace en serie**, para no generar conflictos de merge.

### Bloques paralelizables (archivos disjuntos)

| Agente | Ítems | Archivos que toca (nuevos o aislados) |
|---|---|---|
| A | T1, T2 | `inspection_editor/tools/*`, `execution/tool_executor.*` |
| B | S1, S2 | `ui/history_dialog.*` (nuevo) |
| C | D1, D5 | `domain/calibration.*`, `repositories/settings_repository.*` |
| D | O2 | `camera/camera_controller.*` |

Cada agente:
1. Recibe **este documento completo** más el ítem/los ítems asignados
   (no el resto — evita que "arregle" cosas fuera de su bloque).
2. Implementa, compila (`cmake --build --preset mingw-release`), corre
   `ctest --preset mingw-release`, y deja el árbol de trabajo **sin
   commitear** — el commit lo hace la sesión principal tras revisar e
   integrar el cableado en `main_window`.
3. Si el ítem requiere tocar `main_window.*` (casi todos, aunque sea un
   botón), deja ese fragmento **marcado con un comentario `// TODO(wire): …`**
   en vez de editarlo directamente, para que la integración serial lo
   encuentre rápido.

Usar el tool `Agent` con `subagent_type: general-purpose` y, si el repo
tuviera ramas/PRs (hoy se pushea directo a `main`), `isolation: "worktree"`
para aislar de verdad los cambios de cada agente hasta integrarlos.

### Paso de integración (serial, sesión principal)

Tras cada bloque paralelo: leer los `TODO(wire)`, cablear en
`main_window.h/.cpp`, compilar todo junto, `ctest`, smoke test, actualizar
README/memoria, **un commit por ítem original** (no un commit gigante por
bloque) para mantener el historial legible como el resto del repo.

### Paso final

`G4` (auditoría `/code-review`) se corre **después** de integrar cada bloque,
no al final de todo — detecta problemas de coherencia entre bloques (p. ej.
dos agentes reinventando el mismo helper) mientras todavía es barato
arreglarlos.

---

## Uso dentro de `/loop`

Este documento está escrito para consumirse **ítem por ítem** en modo
autónomo:

- **Modo dinámico (recomendado, igual al usado en esta sesión):** `/loop`
  sin intervalo. En cada iteración: abrir este archivo, tomar el primer ítem
  sin marcar (orden sugerido: A → D → C → B → E, dejando D2/D3 — los
  "avanzados" — para el final), implementarlo completo (código + test +
  build + ctest + smoke test + README + memoria), **marcar su casilla `[x]`
  en este mismo archivo**, commit + push, y solo entonces pasar al
  siguiente. El propio archivo actúa de checklist persistente entre
  iteraciones — no hace falta releer todo el historial de la conversación
  para saber qué falta.
- **Modo con intervalo fijo:** `/loop 45m "Implementa el siguiente ítem sin
  marcar de PROMPT_MEJORAS_HERRAMIENTAS_CALIBRACION.md, con su test y
  build/ctest en verde antes de marcarlo"` si se prefiere cadencia regular
  en vez de auto-ritmo.
- **Multi-agente + loop combinados:** el loop puede, en una iteración,
  decidir lanzar el bloque paralelo de la sección anterior en vez de un ítem
  suelto, si hay varios ítems independientes pendientes a la vez.
- **Parar:** cuando el backlog quede sin casillas pendientes, o con
  `ScheduleWakeup stop:true` / deteniendo el `/loop` manualmente en
  cualquier momento — no hay obligación de agotar la lista en una sola
  sesión.

---

## Fuentes consultadas

- [Measurement Tools - Vision Tools | Cognex](https://www.cognex.com/en-id/products/machine-vision/vision-tools/rule-based-tools/measurement-tools) — catálogo de herramientas (caliper, blob, edge, pattern) usado para contrastar el set actual del proyecto.
- [VisionPro - Caliper and Geometry - Videos | Cognex](https://www.cognex.com/en-my/videos/vision-software/visionpro-caliper-and-geometry) — referencia de UX del editor de herramientas geométricas.
- [Calibration | HALCON Operator Reference](https://www.mvtec.com/doc/halcon/2311/en/toc_calibration.html) — flujo de calibración con tablero multi-punto y corrección de distorsión (ítem D2).
- [calibrate_cameras | HALCON Operator Reference](https://www.mvtec.com/doc/halcon/12/en/calibrate_cameras.html) — detalle del algoritmo de calibración multi-imagen.
