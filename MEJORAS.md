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

- [~] **C1 · Ningún menú enseñaba su atajo.** Eran 0 de 58 entradas. El operador no
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

  **[~] Hechas las cinco entradas que duplicaban un atajo**, que eran todas las
  que había: el resto de las 58 no tiene tecla y no puede enseñar ninguna.

  | entrada | tecla |
  |---|---|
  | Calibrar escala (mm)… | `C` |
  | Guardar plantilla | `Ctrl+S` |
  | Inspeccionar | `I` |
  | Editor de plantilla… | `P` |
  | Atajos de teclado… | `F1` |

  El arreglo entero fue **el orden**: `buildMenuBar()` corría en la línea 1477 y
  `buildShortcuts()` en la 1499, así que cuando el menú se construía no existía
  todavía ninguna acción con tecla y la entrada tenía que crearse sola.
  Invertidos, `shortcutAction(id, texto)` cuelga la que ya existe y le pone el
  título del menú — la guía sigue enseñando `ShortcutSpec::description`, que es
  otro texto para otro público.

  `tests/test_menus_show_shortcuts.cpp` fija las dos mitades. La segunda es la
  que impide el arreglo malo: si alguien «mejora» esto duplicando acciones, la
  primera seguiría pasando —el menú enseñaría la tecla— y la segunda cazaría
  las dos acciones reclamando la misma secuencia.

  Queda pendiente lo contrario: **entradas de menú útiles que no tienen atajo y
  podrían tenerlo**. Eso es añadir teclas, no arreglar un fallo, y toca decidir
  cuáles merecen una.
- [x] **C2 · Ningún botón tenía acelerador `Alt+letra`.** Cierto, y **contado en
  el sitio equivocado**: la nota los contaba sobre la ventana principal, y ahí
  la falta no es un defecto.

  Los botones que se usan a diario ya tienen **tecla suelta** —`I` inspecciona,
  `C` calibra, `P` abre el editor, `F1` la guía— y una tecla suelta es mejor que
  `Alt+letra`: se pulsa con una mano y no compite con la barra de menús, que ya
  se ha quedado con A, F, I, M, P, V e Y. Añadirlos allí sería una segunda forma
  de hacer lo mismo, peleándose por las letras que quedan.

  **Donde sí hacen falta es en los diálogos**, y por una razón concreta: allí hay
  campos de texto, así que una tecla suelta es imposible —se la come el campo— y
  `Alt+letra` es el único mecanismo que queda. Medido:

  | diálogo | botones | con Alt+letra | campos de texto |
  |---|---|---|---|
  | Modo de medición | 9 | 0 | 4 |
  | Plantillas | 8 | **6** | 0 |
  | Piezas | 4 | **4** | 1 |
  | Historial | 2 | 0 | 1 |
  | Calibración de lente | 4 | 0 | 3 |

  **Hechos los cinco**, 22 de 27 botones. Los cinco que quedan sin acelerador
  son los dos modos de medición —cuyo texto viene de `domain::modeLabel`, y meter
  marcas de interfaz en la capa de dominio sería peor que la falta— y tres del
  cuadro de botones que no salen en todos los diálogos.

  Los de «Aceptar», «Aplicar», «Cancelar»… van en `ui/dialog_buttons.cpp`, que es
  donde ya viven sus textos: seis diálogos los usan, y una letra elegida seis
  veces son seis ocasiones de que dos se pisen. «A&plicar» lleva la P porque la A
  es de «Aceptar» — las dos juntas son la duda clásica de una ventana de ajustes
  y darles la misma tecla la empeoraría.

  **La guardia cazó un choque en cuanto se puso**, que es exactamente para lo que
  está: en el calibrador de lente, Alt+C lo reclamaban «&Cancelar» —compartido— y
  «&Calibrar» —del diálogo—. Ninguno de los dos autores podía verlo, porque cada
  uno miraba su fichero. «Calibrar» pasó a la L.

  `tests/test_mnemonics.cpp` vigila lo que de verdad puede romperse: **dos
  mnemónicos iguales no dan un error, dan un ciclo**. Alt+E deja de activar y
  pasa a saltar entre los dos candidatos, que desde fuera se vive como «a veces
  hace otra cosa» — la misma familia que `ambiguousActivate` con los atajos, que
  este proyecto ya se comió con Ctrl+1 y Ctrl+2. Se comprueba dentro de cada
  diálogo y en la ventana principal por separado, porque un diálogo modal
  bloquea lo de detrás y sus letras solo compiten entre ellas.
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

  **Barridos otros seis** (`tests/test_enter_key_in_dialogs.cpp`), y aparecieron
  los dos peores que quedaban:

  | diálogo | se lleva el Enter | destructivo con `autoDefault` |
  |---|---|---|
  | Modo de medición | Aceptar | — |
  | Plantillas | Nueva… | **Eliminar** |
  | Piezas | Renombrar… | **Eliminar…** |
  | Historial | Exportar CSV… | — |
  | Calibración de lente | Guardar esta toma | — |
  | Señalar el fondo | Usar este fondo | — |

  Los dos «Eliminar» no se llevaban el Enter, pero conservaban `autoDefault`:
  basta **tabular hasta ellos** para que el Enter siguiente borre una plantilla
  con todas sus herramientas, o una pieza con TODAS sus referencias,
  herramientas e historial. Arreglados los dos.

  Anotado sin cambiar: en Plantillas el Enter abre «Nueva…» y en Piezas
  «Renombrar…». No destruyen, así que la prueba los da por buenos; que sea la
  respuesta más natural en un diálogo de gestión es otra discusión, y sin una
  medida que diga que estorba no se toca.

  Faltan tres diálogos, los que necesitan una inspección hecha o repositorios
  vivos para construirse: informe de pieza, resultado de inspección y el
  asistente de registro.
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
- [~] **C5 · Colores escritos a mano fuera de `ui/theme.h`.** El trinquete de
  `tests/test_palette_guard.cpp` impide que suban; van **56 -> 49 -> 41 -> 36**.

  Los cinco de esta vuelta salieron de dos grupos, no sueltos:

  - El hueco de «aquí todavía no hay imagen» estaba escrito **tres veces
    idéntico** —informe de inspección, ventana principal, gestor de piezas—.
    Ahora es `theme::placeholderStyle()`. De paso se arregló su borde: el `#444`
    sobre el fondo `#1a1a1a` da **1,8:1**, o sea que casi no se ve, cuando WCAG
    pide 3:1 para el contorno de un control.
  - Los tres estados del asistente de registro, en seis líneas seguidas: el
    «aceptada» ya usaba `theme::kGood` y los otros dos llevaban `#ff5555` y
    `#ff9944` a mano. Un estado con token y dos sin él es como se acaba teniendo
    tres rojos distintos para «mal».
- [~] **C8 · 36 pruebas localizan un control por su TEXTO.** Trinquete puesto en
  `tests/test_lookups_by_name.cpp`; hay que ir bajándolo.

  Ha costado **cuatro** roturas, siempre igual: el taller pide que un rótulo diga
  mejor lo que hace, se reescribe, y se caen pruebas que no tienen nada que ver
  con ese rótulo — solo lo usaban para encontrar el control.

      «¿Está el umbral cortando la pieza?»  ->  «Comprobar el corte»
      «Separar por el color del fondo»      ->  «Separar por color»
      «Usar lo que se ve ahora»             ->  «Usar el recuento detectado»
      «Calibrar»                            ->  «Ca&librar»

  Ninguna señalaba un fallo. Todas costaban un rato y, peor, enseñaban a no tocar
  los rótulos — lo contrario de lo que se pedía.

  Lo que se cuenta es **localizar**, no **afirmar**: un bucle sobre
  `findChildren` que compara el texto Y ADEMÁS se lleva el widget fuera. Sin esa
  segunda condición entrarían las comprobaciones legítimas del tipo «alguna
  etiqueta dice el nombre de esta familia», que recorren igual pero no capturan
  nada. Se midió con y sin: 52 contra 36, o sea que la mitad de la diferencia
  eran falsos positivos, y un trinquete con ruido se aprende a ignorar.

  | fichero | sitios |
  |---|---|
  | test_canvas_gestures.cpp | 25 |
  | test_configure_roundtrip.cpp | 3 |
  | test_report_tabs.cpp | 2 |
  | test_two_pieces_ui.cpp | 2 |
  | otros cuatro | 1 cada uno |

  Los tres de un solo sitio son funciones auxiliares —`findAction(texto)`,
  `labelStartingWith(prefijo)`— usadas desde muchas llamadas con textos
  distintos, así que vaciarlas es más trabajo del que parece y se hará cuando la
  etiqueta correspondiente se toque, que es cuando duele.

  El trinquete además **obliga a apretarlo**: si el recuento baja y el tope no,
  falla pidiendo que se baje. Un trinquete que no se aprieta deja de serlo.
- [x] **C6 · «10 entradas antes del único separador».** Medido hoy, y **ya no es
  cierto**. Los números de línea que citaba —692-779— apuntan ahora al combo de
  pieza y al indicador de modo, o sea que el fichero se reorganizó y la nota se
  quedó describiendo algo que no existe.

  La racha más larga de entradas seguidas sin separador es **5**, y las tres que
  llegan a esa cifra están bien así:

  | menú | entradas | separadores | racha |
  |---|---|---|---|
  | Archivo | 3 | 1 | 2 |
  | Fuente | 2 | 1 | 1 |
  | Medida | 7 | 3 | 2 |
  | Medida ▸ Unidad de medida | 5 | 0 | **5** |
  | Pieza | 5 | 1 | 3 |
  | Inspección | 4 | 1 | 2 |
  | Ver | 9 | 1 | **5** |
  | Ver ▸ Origen del tablero | 5 | 0 | **5** |
  | Ayuda | 1 | 0 | 1 |

  Las dos de los submenús son grupos de opciones **excluyentes** —cinco
  unidades, cinco orígenes de tablero— donde un separador partiría en dos algo
  que es una sola pregunta. Y la de *Ver* son cinco cosas del mismo tipo: qué
  dibujar encima del vídeo.

  Se deja `tests/test_menu_grouping.cpp` como **trinquete**, igual que el de los
  colores escritos a mano: fija el tope en 8 —lo más largo que se aceptó en la
  pestaña Detección, que es el mismo problema resuelto antes— y no deja que
  suba. Hay hueco para una sexta unidad sin que salte, y no lo hay para volver a
  diez. Bajar una racha es trabajo, subirla es descuido, y sin trinquete solo
  pasa lo segundo.
- [x] **C7 · `inspectButton_->setDefault(true)` sobre un `QMainWindow`.** Era
  cierto que promete un Enter que no puede cumplir —Qt: «the default button
  behavior is provided only in dialogs»— y la conclusión que parecía seguir
  —quitarlo— resultó **falsa al medirla**.

  Pintando el mismo botón con y sin la propiedad: **1645 de 1680 píxeles
  distintos, el 97,9 %**. O sea que es exactamente lo que da el realce, y
  quitarla dejaría el botón que se pulsa cien veces al día igual que los otros
  doce.

  Así que se queda, con el comentario corregido para que nadie la borre
  creyéndola muerta ni espere un Enter de ella. Para inspeccionar con el teclado
  está `I`, que ahora el menú enseña (C1).

  `tests/test_default_on_main_window.cpp` deja el número escrito —no exige un
  resultado, lo registra— y vigila lo que sí importa: que el botón principal
  siga siendo **el único** en negrita. Hoy 1 de 22.

  De paso cazó un fallo en la propia prueba: el recuento filtraba por
  `isVisible()` y, con la ventana sin mostrar, salía **cero** y la comprobación
  pasaba sin mirar nada. El mismo fallo que tenía `--smoke`, dentro de la prueba
  recién escrita.

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
- [ ] **E8 · Cien tuercas iguales, ocho formas distintas.** Verificado midiendo
  `producto-tuercas-prueba.jpg` pieza por pieza. Son cien tuercas hexagonales
  del mismo lote y **el área varía solo un 0,9 %** entre la mayor y la menor, así
  que la detección es repetible. El clasificador no:

  | lados que ve | 3 | 4 | 6 | 7 | 8 | 9 | 10 | 11 |
  |---|---|---|---|---|---|---|---|---|
  | tuercas | 4 | 2 | **11** | 9 | 12 | 2 | 9 | **51** |

  Once de cien aciertan, y la respuesta más frecuente —once lados— es falsa. Es
  un fallo de REPETIBILIDAD: la misma pieza, dos veces, dos formas. Y como «qué
  medir depende de la forma», cada tuerca de la bandeja recibe un juego de cotas
  distinto.

  **Dos hipótesis mías, las dos falsadas midiendo.** Van escritas para que nadie
  las vuelva a gastar:

  1. *«Es un punto atípico, una rebaba»* — NO. El ajuste de seis lados se separa
     p50 1,0 px, p90 5,4 px, p95 5,8 px, máx 6,2 px: la cola está poblada, entre
     2 y 10 puntos por encima del tope. Es el chaflán de la tuerca repartido por
     sus seis esquinas, no un pico. Un percentil robusto en vez del máximo movería
     la moneda al aire de sitio, no la quitaría.
  2. *«Subir `maxDeviationPx`»* — NO. La meseta de seis lados sale en 17 de 30
     epsilon barridos, contra 1-3 de todas las demás, y se descarta **por 0,24 px**
     (6,24 contra el tope de 6,00). Pero el hueco que ese 6,00 protege está medido
     y es real: un rectángulo con redondeos de 40 px se separa 16,6 px. Subirlo lo
     suficiente para la tuerca rompería esa distinción.

  **Dónde está de verdad, y esto sí está medido:** la tuerca debería salir por la
  rama de «polígono redondeado», que es exactamente lo que es —un hexágono con
  las esquinas achaflanadas— y esa rama pide `straight >= 3`. Pero
  `decomposeContour` ve en cada tuerca **6 arcos y 0-1 rectas, con el 85-91 % del
  contorno en curva**. Los seis planos de una tuerca hexagonal se están
  descomponiendo como arcos. Ahí está el fallo, y no en ningún umbral del
  clasificador: sobre una pieza de lados rectos, la descomposición dice que
  nueve décimas partes son curva.

  Siguiente paso: mirar `decomposeContour` sobre un plano corto y ligeramente
  arqueado —50 px de lado en esta foto— y ver por qué prefiere un arco de radio
  enorme a una recta. **Hecho: ver E9.**

- [!] **E9 · Dos guardas de `makePrimitive` se comprueban antes del número que
  vigilan.** Encontrado persiguiendo E8. **Se intentó arreglar y se revirtió**,
  y esto último es lo que hay que leer antes de volver a intentarlo.

  El orden en `vision/geometry_features.cpp` es:

  1. `fitSegment` acepta el arco si `barrido >= minArcSweepDeg` (15°) y si
     `residuo <= maxResidual` (0,8), con el ajuste **voraz**.
  2. `makePrimitive` **reajusta** el círculo de forma robusta —para eso está: el
     voraz se pasa hacia el tramo tangente y dejaba el radio hasta un 40 %
     desviado— y **republica radio, barrido y residuo**.
  3. Nadie vuelve a comprobar ninguna de las dos condiciones.

  Lo que eso publica, medido sobre los **1288 arcos del banco de fotos entero**:

  | | valor publicado | lo que la opción admite |
  |---|---|---|
  | barrido mínimo | **0,4°** | 15° |
  | radio máximo / radio de la pieza | **31×** | — |
  | residuo de los arcos del dodecágono | **0,83-0,87 px** | 0,80 |

  Un arco de radio 31 veces su propia pieza no es una curva: es un lado recto con
  un número inventado encima.

  **Y sin embargo no se puede arreglar de una en una.** Se probó:

  - Reaplicando solo la guarda del **barrido**: el banco mejora mucho (barrido
    mínimo 15,1°, radio máximo 3,8×) y el rectángulo redondeado sigue dando 4
    rectas y 4 arcos. Pero **rompe** `ShapeClassProbe.Escala`: un dodecágono de
    radio 100 pasa a «redondeado de 3 lados». Antes acertaba porque sus lados
    salían como arcos de barrido ridículo y eso impedía que se disparase la rama
    de «redondeado» — acertaba por culpa del fallo.
  - Reaplicando **además** la del residuo: el dodecágono se arregla (9 rectas, 0
    arcos) y los rectángulos redondeados salen idénticos. Pero **rompe**
    `ShapeClassBasics.ManySidedContoursAreMeasuredAsRoundAndSayWhy`: un polígono
    de 16 lados, que debe medirse como círculo, sale «redondeado».

  **La conclusión, que es lo que vale de todo esto:** el punto frágil no son las
  guardas, es la rama de «polígono redondeado». Se pregunta la PRIMERA, y se
  dispara con una condición débil —`straight >= 3 && arcs >= 1 && curva >= 10 %`—
  así que cualquier cambio en la mezcla de rectas y arcos hace que se dispare o
  no en casos donde no toca. Mientras eso siga así, cada arreglo de las guardas
  cambia qué caso acierta, y el banco solo dice cuál se movió esta vez.

  **Y no hay atajo por el porcentaje de curva.** Se midió la familia entera
  buscando un techo:

  | pieza | rectas | arcos | en curva |
  |---|---|---|---|
  | 300x200 redondeo 20 | 4 | 4 | 12,7 % |
  | 300x200 redondeo 40 | 4 | 4 | 28,3 % |
  | 300x200 redondeo 60 | 4 | 4 | 44,9 % |
  | 300x200 redondeo 80 | 2 | 4 | **70,7 %** |
  | 12 lados radio 100 (mal leído) | 5 | 4 | **77,0 %** |

  Un rectángulo con redondeos grandes es legítimamente casi todo curva, así que
  no hay techo que separe. El recuento de rectas contra arcos tampoco: 4/4 en el
  bueno, 5/4 en el malo.

  Lo que haría falta antes de tocar nada: que la rama de «redondeado» se decida
  con una condición que signifique algo geométrico —los arcos son las ESQUINAS,
  o sea cortos y tantos como lados— en vez de con tres umbrales sueltos.

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
