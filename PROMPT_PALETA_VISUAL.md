# Paleta visual de herramientas — panel por familias con iconos

Objetivo: que elegir herramienta se parezca a elegir una forma en Paint, y que el
sitio donde se elige sea un **panel acoplado** como «Comparación registrada /
actual», no una fila apretada dentro de la barra.

## Por qué ahora

Hoy hay dos paletas distintas para lo mismo (`ToolPalette::Shape`):

- **`Compact`** — fila 3 de la ventana principal. Cinco botones de familia con
  menú desplegable. Para ver una herramienta hay que **abrir un menú**, y el menú
  tapa el vídeo justo cuando quieres mirar dónde vas a dibujar. Además la fila 3
  ya lleva, además de la paleta: `Borrar herramienta`, `Rasgo distintivo`,
  `Puntos:` + spin, `Fijar escala con esta medida…`, `Guardar plantilla` y
  `Atajos`. En una ventana que arranca a 1100 px eso va justo hoy y no va a caber
  mañana.
- **`Accordion`** — columna izquierda del editor, `QToolBox`, 190 px. Una familia
  abierta a la vez, botones con icono **y** texto en vertical. Funciona, pero
  gasta una fila entera de alto por herramienta: con las ~16 que quedan por
  añadir (F, G, M) una familia no cabrá en la columna sin barra de scroll.

Las dos comparten el problema de fondo: **las herramientas no se ven**. Hay
catorce, van a ser treinta, y en ningún momento tienes las de una familia
delante de los ojos a la vez.

El modelo de Paint resuelve exactamente eso: una franja de familias arriba, el
nombre de la familia activa, y debajo **todas** sus herramientas como iconos en
rejilla. Se ve el conjunto de un vistazo y se elige en un clic, no en dos.

**Coste de hacerlo ahora:** retrasa `X1` y el bloque GD&T de
`PROMPT_CONFIGURAR_Y_FAMILIAS.md` una sesión larga. Merece la pena hacerlo antes
y no después porque cada herramienta nueva de F/G/M solo tendrá que aportar su
icono y caerá sola en el sitio bueno; al revés habría que repasar treinta.

## Anatomía a la que vamos

```
┌────────────────────────────────────┐
│ Herramientas                    ⇱ ✕│  ← QDockWidget, como el de comparación
├────────────────────────────────────┤
│ ⊹ Mover/Elegir                     │  ← fijo arriba: no es una familia
├────────────────────────────────────┤
│  [◻]  [↔]  [◇]  [⟂]  [⚙]           │  ← franja de familias, exclusiva
│                                    │
│  Medición en línea                 │  ← título de la familia activa
│  ┌──┐ ┌──┐ ┌──┐ ┌──┐               │
│  │↔ │ │○ │ │∠ │ │⌐ │               │  ← rejilla de iconos, reflujo por ancho
│  └──┘ └──┘ └──┘ └──┘               │
│  ┌──┐ ┌──┐                         │
│  │⊥ │ │◠ │                         │
│  └──┘ └──┘                         │
├────────────────────────────────────┤
│ Calibre — distancia entre dos       │  ← línea de ayuda: nombre + qué hace
│ bordes opuestos.          Familia 2+1│    de la herramienta señalada
└────────────────────────────────────┘
```

Tres reglas de diseño que **no se discuten por ítem**, para que el resultado sea
coherente:

1. **La franja de familias es solo iconos; el nombre de la familia activa va
   debajo, siempre visible.** Es lo que hace Paint con «Formas en 2D» y es lo que
   evita el problema de los iconos sin texto: nunca hay duda de en qué familia
   estás. Cinco familias × ~34 px = ~170 px, que caben en un panel de 200.
2. **La rejilla es de iconos sin texto, y por eso la línea de ayuda es
   obligatoria.** Un icono mudo es una adivinanza; un icono con el nombre escrito
   debajo al pasar por encima es un catálogo. La línea de ayuda no es un extra
   estético: es la parte que hace legal quitar el texto de los botones.
3. **Nada de colores a pelo en el panel.** Los iconos ya se pintan con
   `QApplication::palette().color(QPalette::ButtonText)`; el panel se ciñe a lo
   mismo. Lo que se ve distinto entre familias es el **dibujo**, no el color: en
   escala de grises industrial y con el monitor mal calibrado del taller, el
   color no distingue nada.

## Lo que NO cambia

- La API pública de `ToolPalette` (`currentTool`, `showSelection`, `activate`,
  `activateCategory`, `activateInCurrentCategory`, `currentCategory`, señal
  `toolChosen`). Los atajos «familia + dígito» de `main_window.cpp:1140` y la
  sincronización desde el lienzo cuelgan de ella; si cambia, este trabajo deja de
  ser una reforma de la vista y pasa a ser una reforma del control.
- Las familias, su orden y sus descripciones (`tool_geometry.cpp`). Son datos, no
  presentación.
- Que la paleta viva en `pci_editor`, del que ya depende `pci_ui`. La dependencia
  sigue bajando.

---

## Ítems

### - [x] P1 — Iconos de familia

*(Los cinco dibujados con `QPainter`, sin ficheros. El barrido de «ningún par se
parece» dio 19,4 % de píxeles distintos en el par más cercano —Medición en
línea contra Construcciones—, así que el umbral quedó en 12 %: mi primera cifra
era del 6 % y habría pasado con dos iconos casi iguales, o sea que no habría
cazado nada.
Hay un tercer barrido que no estaba en el plan y hacía falta: que ningún icono
de familia se parezca a los de SUS herramientas. Si se parecieran, las dos filas
del panel se confundirían, que es justo lo que la franja viene a separar.)*

Cinco iconos nuevos en `canvas/tool_icons.{h,cpp}`:
`[[nodiscard]] QIcon categoryIcon(ToolCategory category);`, dibujados con
`QPainter` como los catorce que ya hay (nada de ficheros: el proyecto no tiene
sistema de recursos y no se lo va a inventar por esto).

Qué dibuja cada uno, para que el icono diga la familia y no sea decoración:

| Familia | Dibujo |
| --- | --- |
| Figuras básicas | Un contorno cerrado irregular con su área tramada. |
| Medición en línea | Una cota: flecha doble entre dos topes. |
| Construcciones | Dos rectas finas cruzándose con un punto marcado en la intersección. |
| GD&T | El marco de control de característica: recuadro dividido con símbolo + tolerancia + datum. Es el símbolo que un metrólogo reconoce sin leer. |
| Máx./mín. y torneadas | Una silueta escalonada con la cota del máximo. |

Verificación, con el patrón de «renderizar y contar píxeles» que ya se usa:
- Cada icono renderizado a 20, 24 y 32 px tiene tinta: fracción de píxeles no
  transparentes dentro de una banda razonable (ni vacío ni mancha).
- **Ningún par de iconos de familia se parece**: diferencia absoluta media entre
  cada par por encima de un umbral, medido primero y fijado después con margen.
  Cinco pastillas indistinguibles serían peor que cinco palabras.
- `categoryIcon` no tiene `default:` en el `switch`, igual que `toolIcon`: una
  familia nueva **rompe la compilación** hasta que alguien le dibuje un icono.
  Esa es la garantía de que el panel no se llena de huecos con el tiempo.

### - [x] P2 — `Shape::Panel`: la franja, el título y la rejilla

*(El reflujo por ancho tenía una pescadilla que se muerde la cola y costó
encontrarla: el mínimo de un `QGridLayout` es el de sus columnas, así que con
las ocho herramientas de una familia en una fila el panel pedía 324 px y Qt no
le dejaba estrecharse por debajo — y como no se estrechaba, el reflujo a cuatro
columnas no llegaba a ocurrir NUNCA. Medido: `resize(180)` devolvía un ancho
real de 324. Se arregla con `QSizePolicy::Ignored` en el contenedor de la
rejilla: acepta el ancho que le den y recoloca dentro. Ahora el mínimo del panel
es 176 px, que es el de la franja de familias — lo que de verdad no encoge.
Segundo detalle del mismo sitio: `QGridLayout` no encoge nunca su número de
columnas, así que al pasar de ocho a cuatro las cuatro vacías seguían contando.
El layout se REHACE al cambiar el número de columnas en vez de recolocarse.
Y una cosa que el test tuvo que aprender: Qt APLAZA el evento de redimensionado
en un widget que nunca se ha mostrado, así que sin `show()` el test medía un
panel que no existía.)*

Tercera forma de `ToolPalette`, la de la anatomía de arriba. Piezas:

- **Franja de familias**: `QToolButton` checkables y exclusivos
  (`QButtonGroup`), icono de P1 a 24 px, `autoRaise`, tooltip con
  `categoryDescription`. Pulsar una familia **la abre; no elige herramienta**
  (ojo: `activateCategory()` sí elige la primera, porque viene del atajo y quien
  pulsa un atajo quiere dibujar ya — son dos gestos distintos y tienen que
  seguir siéndolo).
- **Título** de la familia activa, en negrita discreta.
- **Rejilla**: los `toolsInCategory(activa)` como `QToolButton` checkables,
  icono 28 px, `ToolButtonIconOnly`, hit target ≥ 34 px. **Reflujo por ancho**:
  columnas = `max(1, ancho_disponible / paso)` recalculado en `resizeEvent`. Qt
  no trae layout de flujo, así que es un `QGridLayout` que se recoloca; el
  ejemplo canónico (`Flow Layout`) es más máquina de la que hace falta para ≤ 10
  botones por familia.
- **`Mover/Elegir`** arriba del todo, fuera de la franja, con texto: no es una
  familia y confundirlo con una lo empeora.
- `setFocusPolicy(Qt::NoFocus)` en todos los botones, como ya se hace en la barra
  de zoom: el foco es del lienzo o los atajos dejan de funcionar tras el primer
  clic.

Verificación:
- El test que ya existe, `ToolPaletteTest.EveryToolIsReachableInBothShapes`,
  pasa a recorrer **las tres** formas: agrupar no puede esconder ninguna
  herramienta. Con la rejilla hay que recorrer las cinco familias abriéndolas,
  porque solo se instancia la activa.
- El panel no pide scroll horizontal entre 180 y 400 px de ancho:
  `sizeHint().width()` y el ancho mínimo de la rejilla medidos a esos anchos.
- La familia con más herramientas cabe en 520 px de alto sin scroll vertical
  **contando las que quedan por añadir**: el test las simula pidiendo la rejilla
  para una lista inflada, no espera a que existan.

### - [x] P3 — La línea de ayuda

*(Lo que repone el texto que P2 le quita a los botones. El atajo que enseña se
comprueba EJECUTÁNDOLO: un atajo mal anunciado es peor que ninguno, porque el
operador lo prueba, no pasa nada y deja de fiarse también de los que sí
funcionan.
El test del ratón destapó un fallo de P2 que no se veía: al cambiar de familia,
los botones retirados con `deleteLater` **siguen siendo hijos del panel** hasta
que corre el bucle de eventos, así que `findChildren` devolvía botones de la
familia anterior que ya no están en pantalla. Ahora se desvinculan antes de
programar el borrado.
El alto de la línea es fijo, de dos renglones: si creciera y menguara al pasar
el ratón, la rejilla botaría bajo el cursor y elegir sería un juego de
puntería. Hay un test que lo mide recorriendo una familia entera.)*

Debajo de la rejilla, dos renglones con el **nombre** y la **descripción** de la
herramienta señalada con el ratón; sin ratón encima, los de la seleccionada; sin
ninguna seleccionada, una frase de arranque («Elige una familia arriba»). A la
derecha, el atajo que la activa («Familia 2 + 1»).

Es lo que sustituye al texto que se quita de los botones. Sin esto, P2 es una
regresión con mejor aspecto.

Verificación:
- Con un `QEvent::Enter` sintético sobre el botón de una herramienta, la etiqueta
  lleva su nombre; con `QEvent::Leave`, vuelve a la seleccionada. Sin ratón y sin
  selección, no queda vacía.
- El texto de la descripción **es** `toolTypeDescription`, no una copia: escrito
  dos veces acabaría divergiendo, que es la razón por la que la paleta se
  compartió en su día.
- La etiqueta tiene alto fijo (reserva de dos renglones): si creciera y menguara
  al pasar el ratón, la rejilla botaría.

### - [ ] P4 — El editor estrena el panel

`editor_window.cpp:68` pasa de `Shape::Accordion` a `Shape::Panel`. El ancho
mínimo baja de 190 a lo que pida P2 y la columna deja de gastar una fila por
herramienta.

Verificación: abrir el editor de una plantilla real con herramientas guardadas,
elegir una de cada familia, dibujarla y comprobar que se guarda. Humo de verdad,
no solo compilar.

### - [ ] P5 — Dock «Herramientas» en la ventana principal

La fila 3 se vacía: el panel se va a un `QDockWidget` a la derecha, hermano del
de comparación, y se lleva con él lo que **actúa sobre la herramienta
seleccionada** y hoy está suelto en la barra:

- `Borrar herramienta` → al dock, bajo la línea de ayuda.
- `Puntos:` + spin → al dock, bajo la línea de ayuda (es el parámetro de la
  herramienta seleccionada; su sitio natural es junto a ella).
- `Rasgo distintivo`, `Fijar escala con esta medida…`, `Guardar plantilla`,
  `Atajos` → **se quedan en la barra**. No son herramientas de dibujo: son
  acciones sobre la pieza y la plantilla. Meterlas en el dock por hacer sitio
  sería ordenar por tamaño en vez de por significado.

Detalles que hay que atender o el dock sale mal:
- `objectName` estable (`toolsDock`), porque `saveState`/`restoreState`
  (`main_window.cpp:684` y `:2843`) guardan la disposición por nombre.
- **Un dock nuevo sobre un estado guardado viejo**: `restoreState` no sabe nada
  de él. Hay que **probarlo con los ajustes reales existentes**, no con un perfil
  limpio, y confirmar que aparece visible y en su sitio; si no, colocarlo
  explícitamente después de restaurar. Mismo rigor que se usó con las migraciones
  de esquema: se prueba contra el fichero de verdad.
- El dock arranca visible y se puede cerrar; y hay una entrada en el menú de
  vista para volver a abrirlo. Un panel que se cierra sin forma de recuperarlo es
  una herramienta perdida.

Verificación: medir el ancho mínimo de la fila 3 **antes y después** e imprimirlo
en el test, como se hizo con la paleta compacta. La ventana tiene que caber de
verdad a 1100 px.

### - [ ] P6 — Retirar `Compact` y `Accordion`

Solo después de P4 y P5, y solo si no queda ningún uso. `Shape` se queda sin
enumeración o con una sola entrada; si es una sola, se elimina el parámetro del
constructor. Los tests que iteraban formas se simplifican.

Tres paletas mantenidas a la vez divergen; ya pasó con los botones antes de R2.

### - [ ] P7 — Que se vea bien de verdad

Lo «bonito» aquí es medible, así que se mide:

- **La herramienta activa se identifica sin pasar el ratón**: botón `checked`
  con fondo del `QPalette::Highlight`, y su familia también marcada en la franja.
  Verificación: renderizar el panel con y sin selección y comprobar que difieren
  en la zona del botón esperado.
- **Rejilla uniforme**: mismo paso entre celdas en las cinco familias; la última
  fila incompleta se alinea a la izquierda, no se estira.
- **Los atajos siguen mandando**: `activateCategory` cambia la franja **y** el
  título; `activateInCurrentCategory` marca el botón de la rejilla. Si el atajo
  elige algo que la vista no refleja, el operador deja de fiarse de los dos.
- **Márgenes y espaciado** con una sola constante, no números sueltos por el
  fichero.

### - [ ] P8 — Documentación y cierre

- `README.md`: cómo se eligen herramientas ahora (panel, familias, la línea de
  ayuda, el atajo familia + dígito) y que el dock se puede cerrar y recuperar.
- `ARQUITECTURA.md`: `ToolPalette` como panel único; `categoryIcon` junto a
  `toolIcon`; el dock de la ventana principal y su estado persistido.
- `PROMPT_CONFIGURAR_Y_FAMILIAS.md`: nota en el bloque de familias diciendo que
  cada herramienta nueva aporta su icono y aparece sola en el panel.
- Cuando P1–P8 estén marcados, **borrar este fichero**.

---

## Procedimiento por ítem (el de siempre)

1. Implementar.
2. `cmake --build --preset mingw-release` limpio bajo `-Werror`.
3. `ctest --preset mingw-release` verde.
4. Humo real de la app: abrir, elegir herramientas de cada familia en la ventana
   principal y en el editor, dibujar y guardar.
5. Marcar la casilla con una nota de una línea de lo que se aprendió si hubo algo
   que medir.
6. Actualizar `README.md` / `ARQUITECTURA.md` si el ítem lo toca.
7. Commit atómico **sin firma** y push a `main`.

Tests nuevos en `tests/test_canvas_gestures.cpp`, donde ya vive `ToolPaletteTest`
(binario `pci_gui_tests`, `QT_QPA_PLATFORM=offscreen`).

## Orden recomendado

`P1 → P2 → P3 → P4 → P5 → P6 → P7 → P8`

P1 antes que P2 porque la franja no se puede montar sin iconos. P3 pegado a P2
porque P2 sin P3 es peor que lo que hay. P4 antes que P5 porque el editor es el
sitio donde un fallo cuesta menos: si el panel está mal, se descubre ahí y no en
la pantalla que mira el operador todo el día. P6 después de los dos consumidores,
nunca antes.

## Referencias consultadas

- [VisionPro — Adding Vision Tools to ToolBlocks (Cognex)](https://docs.cognex.com/vpromx_1000/web/EN/visionpro/Content/Topics/users-guide/quickbuild/adding-vision-tools.htm)
  — el *Toolbox* de VisionPro agrupa las herramientas por función y se añaden con
  doble clic desde una paleta; confirma que agrupar por familia es el patrón del
  sector, no un invento.
- [QToolButton — Qt 6](https://doc.qt.io/qt-6/qtoolbutton.html) — por defecto
  muestra solo icono; es el widget previsto para esto.
- [Ribbon-like QToolButton group — Qt Forum](https://forum.qt.io/topic/110755/ribbon-like-qtoolbutton-group)
  — Qt no trae widget de paleta por categorías: se construye con `QToolButton` +
  `QGridLayout` dentro de un contenedor, que es justo lo que hace P2.
