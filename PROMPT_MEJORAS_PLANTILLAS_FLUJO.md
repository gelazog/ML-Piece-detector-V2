# PROMPT — Ronda de mejoras: flujo de plantillas (tiempo real ↔ plantilla, editor con dos modos, gestor)

Documento de planificación para la siguiente ronda de trabajo sobre **PC
Inspector**, centrada en el **flujo de trabajo de las plantillas de inspección**:
sincronizar lo que se dibuja en tiempo real con la plantilla guardada, dar al
editor un modo en vivo y otro por imagen, y añadir un gestor de plantillas.

Es hermano del backlog de herramientas/calibración
([PROMPT_MEJORAS_HERRAMIENTAS_CALIBRACION.md](PROMPT_MEJORAS_HERRAMIENTAS_CALIBRACION.md))
y complementa dos ítems suyos (T6 duplicar herramientas, O4 export/import de
config). Está pensado para **pegarse tal cual como prompt**, repartirse entre
**varios agentes**, o consumirse **dentro de `/loop`** ítem por ítem.

---

## Diagnóstico del estado actual (verificado en código, 2026-07-21)

Punto de partida real, para que una sesión/agente nuevo no tenga que redescubrirlo:

1. **Las herramientas en vivo solo se guardan al registrar.** `liveTools_` se
   persiste a la plantilla únicamente en `onRegisterLiveClicked`
   (`main_window.cpp`, bucle `repos_.tools->save(pieceId, tool.config, tmpl)`).
   No existe una acción "guardar plantilla" independiente.
2. **En una pieza ya registrada, editar en vivo no persiste.** Dibujar, mover,
   editar tolerancias o borrar en vivo **no** toca la BD; al cambiar de
   pieza/plantilla o cerrar, `loadToolsForSelectedPiece()` **limpia `liveTools_`
   y recarga desde la BD**, perdiendo los cambios sin aviso.
3. **El editor no ve las herramientas en vivo sin guardar.** `EditorWindow`
   carga de la BD (`loadExistingTools` → `ToolRepository::listForPiece`). Sus
   ediciones sí se guardan y al cerrar la vista recarga de BD (editor→vivo
   funciona), pero lo dibujado en vivo y aún no guardado no llega al editor.
4. **El editor abre sobre `frameOrFile()`**: el frame actual o un archivo, pero
   siempre como **imagen fija**. No hay elección explícita "tiempo real vs
   imagen" ni edición continua contra el video (aunque `EditorCanvas` YA soporta
   modo vivo: `setFrame` + `setLivePiece`).
5. **Gestión de plantillas mínima**: hay un combo de plantilla + botón "+" para
   crear. `ToolRepository` expone `save`, `listForPiece`, `listTemplates`,
   `remove(toolId)` — pero **no** renombrar, eliminar ni duplicar plantillas.

---

## Principios (heredados, no negociables)

1. **No reestructurar la arquitectura por capas** (`core/ camera/ vision/ ml/
   database/ domain/ inspection_editor/ engine/ repositories/ ui/`). Todo se
   implementa *dentro* de esa estructura.
2. **Compilar y probar de verdad** antes de dar un ítem por terminado:
   `cmake --build --preset mingw-release` limpio (`-Werror` activo) +
   `ctest --preset mingw-release` en verde + smoke test (la app abre y no se
   cierra sola). OJO: matar `pc_inspector.exe`/`pci_tests.exe`/`ctest` colgados
   antes de recompilar (bloquean el `.exe` con "Permission denied" al enlazar).
3. **Un test por lógica nueva no trivial** (repositorio, serialización, sync).
   Los widgets de Qt puros no necesitan test unitario; su lógica de datos sí.
4. **Commits atómicos por ítem**, mensaje en español, **sin firma** (sin
   `Co-Authored-By`). Evitar comillas dobles en el mensaje al usar here-strings
   de PowerShell (parte el argumento).
5. **Actualizar README y memoria** al cerrar cada ítem.
6. Ítems marcados **(pide confirmación)** cambian el comportamiento visible o el
   flujo del operador: preguntar antes con `AskUserQuestion`, no asumir.

---

## Backlog

Formato: casilla, ID, tarea, por qué, skill(s), archivos principales.

### A. Persistencia y sincronización tiempo real ↔ plantilla

- [x] **P1 — Guardar la plantilla desde la vista en vivo (sin re-registrar)**
  *(pide confirmación)*. Hoy solo el registro persiste las herramientas. Añadir
  una acción **"Guardar plantilla"** (botón junto a las herramientas + atajo,
  p. ej. `Ctrl+S`) que sincronice `liveTools_` con la plantilla activa de la
  pieza seleccionada: **insertar** las nuevas (`id < 0`), **actualizar** las
  cambiadas y **borrar** de la BD las que el operador quitó en vivo. Requiere una
  operación de repositorio que reemplace/sincronice el conjunto de una plantilla
  (o el borrado de las que ya no están). Sin pieza seleccionada, ofrecer crear
  una o remitir al registro.
  Skills: `sqlite-database-expert`, `cpp-testing`, `qt-cpp-review`.
  Archivos: `repositories/tool_repository.*` (nuevo `replaceTemplateTools` o
  `syncTemplate`), `ui/main_window.*`.

- [x] **P2 — Marca de cambios sin guardar + aviso al salir**. Rastrear un flag
  "plantilla sucia" que se activa al dibujar/mover/editar/borrar en vivo y se
  limpia al guardar (P1) o registrar. Al **cambiar de pieza/plantilla** o
  **cerrar la app** con cambios sin guardar, preguntar **Guardar / Descartar /
  Cancelar** en vez de perderlos en silencio (hoy `loadToolsForSelectedPiece`
  los descarta). Depende de P1 para el "Guardar".
  Skills: `qt-cpp-review`.
  Archivos: `ui/main_window.*` (flag, `closeEvent`, cambio de combo pieza/plantilla).

- [x] **P3 — Abrir el editor con las herramientas en vivo actuales (ida y
  vuelta)**. Hoy el editor carga de la BD y no ve lo dibujado en vivo sin
  guardar; al volver, la vista recarga de BD. Pasar `liveTools_` (incluidas las
  no guardadas) al `EditorWindow` como estado inicial y **devolver** sus
  ediciones a `liveTools_` al cerrar (en vez de recargar de BD), para que editor
  y vivo muestren siempre lo mismo. Si el editor guarda a BD, mantener la
  coherencia con el flag de P2.
  Skills: `qt-cpp-review`, `cpp-testing`.
  Archivos: `inspection_editor/editor_window.*` (constructor que acepta
  herramientas iniciales + getter del resultado), `ui/main_window.cpp`.

### B. Editor con dos modos: tiempo real o imagen

- [ ] **E1 — Elegir modo al abrir el editor: «Tiempo real» o «Desde imagen»**
  *(pide confirmación de flujo)*. Al abrir el editor, ofrecer dos modos:
  **(a) Tiempo real** — el canvas del editor muestra el **video en vivo**
  (`EditorCanvas` ya soporta modo vivo con `setFrame` + `setLivePiece`), el
  fixture se actualiza por frame y las herramientas se **miden en vivo** mientras
  las ajustas; **(b) Desde imagen** — snapshot actual o archivo (lo de hoy).
  Reto principal: **compartir el `CameraController`** entre la ventana principal
  y el editor sin abrir la cámara dos veces (pausar el consumo de la principal
  mientras el editor está en modo vivo, o reenviar los frames).
  Skills: `qt-cpp-review`, `systematic-debugging`.
  Archivos: `inspection_editor/editor_window.*` (aceptar un `CameraController*`
  opcional y un modo), `ui/main_window.cpp`.

- [ ] **E2 — En modo imagen, elegir explícitamente cámara-actual vs archivo**.
  Hoy `frameOrFile()` decide solo. Hacerlo explícito dentro del diálogo de E1:
  «frame actual» o «abrir archivo…». Pequeño, se apoya en E1.
  Skills: `qt-cpp-review`.
  Archivos: `inspection_editor/editor_window.*`, `ui/main_window.cpp`.

### C. Gestor de plantillas

- [ ] **M1 — Diálogo «Gestionar plantillas»**. Hoy solo hay combo + botón "+".
  Añadir un gestor para la pieza seleccionada: **listar** (`listTemplates`),
  **crear**, **renombrar**, **eliminar** (con todas sus herramientas — pide
  confirmación), **duplicar** (copiar el conjunto a un nombre nuevo) y marcar la
  **activa**. Nuevas ops de repositorio: `renameTemplate`, `deleteTemplate`,
  `duplicateTemplate` (todas parametrizadas por `pieceId` + nombre).
  Skills: `sqlite-database-expert` (seguir el patrón de migraciones/consultas ya
  usado), `qt-cpp-review`, `cpp-testing`.
  Archivos: `ui/template_manager_dialog.*` (nuevo),
  `repositories/tool_repository.*`, `ui/main_window.cpp` (botón «Gestionar…»).

- [ ] **M2 — Exportar/Importar una plantilla a archivo (`.json`)**. Volcar una
  plantilla (sus `ToolConfig` + geometrías + tolerancias) a un `.json` y
  reimportarla en otra pieza o PC de la línea. Reutiliza la (de)serialización de
  geometrías ya existente (`toJson`/`geometryFromJson`). Complementa O4 del otro
  backlog, pero a nivel de plantilla.
  Skills: `cpp-coding-standards`, `cpp-testing`.
  Archivos: `repositories/tool_repository.*` o un serializador aparte,
  `ui/template_manager_dialog.*`.

---

## Skills disponibles y cuándo usarlas

Ya instaladas (`.agents/skills/`):

| Skill | Úsala para |
|---|---|
| `cpp-coding-standards` | Cualquier código C++ nuevo. |
| `cpp-testing` | Tests de GoogleTest antes de dar un ítem por hecho. |
| `test-driven-development` | Lógica pura nueva (sync de plantilla, serialización): test primero. |
| `qt-cpp-review` | Pasada de revisión sobre cada diálogo/widget nuevo antes de cerrar el ítem. |
| `sqlite-database-expert` | P1, M1, M2 (nuevas ops/consultas sobre `InspectionTools`). |
| `systematic-debugging` | E1 (compartir la cámara entre ventanas falla de formas no obvias). |
| `cmake` | Si algún ítem añade archivos/targets nuevos. |

Disponibles en la sesión (instalar con `npx skills add` si hacen falta seguido):
`run` y `verify` (probar cada ítem en la app real), `simplify` (limpieza tras
varios ítems), `code-review` (auditoría antes de commitear un ítem grande).

---

## Plan de ejecución con múltiples agentes

Como siempre, `src/ui/main_window.cpp` es el archivo con más tráfico: el
cableado final va **en serie** ahí; los agentes en paralelo solo tocan archivos
nuevos o aislados.

| Agente | Ítems | Archivos (nuevos o aislados) |
|---|---|---|
| A | P1, P2 | `repositories/tool_repository.*` (sync/borrado), lógica de flag |
| B | M1, M2 | `ui/template_manager_dialog.*` (nuevo), ops de plantilla en el repo |
| C | E1, E2 | `inspection_editor/editor_window.*` |

`P3` depende de decisiones de `EditorWindow` (agente C) y del estado de
`liveTools_` (main_window): integrarlo en serie después de A y C. Cada agente
compila + `ctest` y deja el árbol **sin commitear**; la sesión principal cablea
`main_window.*`, prueba, actualiza README/memoria y hace **un commit por ítem**.

---

## Uso dentro de `/loop`

Igual que el otro backlog: **modo dinámico** (`/loop` sin intervalo). En cada
iteración: abrir este archivo, tomar el primer ítem sin marcar (orden sugerido
**A → C → B**: primero cerrar el hueco de persistencia que motivó esta ronda,
luego el gestor, luego los modos del editor), implementarlo completo (código +
test + build + ctest + smoke test + README + memoria), **marcar su casilla
`[x]`**, commit + push **sin firma**, y solo entonces pasar al siguiente. Parar
cuando no queden casillas o con `ScheduleWakeup stop:true`.

Este documento y el de herramientas/calibración son independientes: apunta el
loop al que quieras trabajar (`/loop @PROMPT_MEJORAS_PLANTILLAS_FLUJO.md` o
`/loop @PROMPT_MEJORAS_HERRAMIENTAS_CALIBRACION.md`).

---

## Fuentes / referencias de coherencia

- Patrón de **"recetas/jobs" y su gestión** (crear, clonar, activar) en software
  de visión industrial: Cognex In-Sight / VisionPro y MVTec MERLIC organizan la
  inspección en trabajos guardables y clonables — base conceptual de M1/M2.
- El resto sale del **diagnóstico del código de este repo** (ver arriba), no de
  alcance inventado.
