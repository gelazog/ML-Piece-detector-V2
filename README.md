# PC Inspector — Demo de inspección visual industrial (offline)

Aplicación de escritorio C++20 / Qt 6 Widgets / OpenCV para registrar piezas 2D
como referencia (embeddings) y detectar anomalías + mediciones geométricas,
100 % offline. Especificación completa en
[PROMPT_MAESTRO_PC_INSPECTOR.md](PROMPT_MAESTRO_PC_INSPECTOR.md).

## Estado de fases

| Fase | Contenido | Estado |
|---|---|---|
| 1 | Esqueleto + módulo de cámara (detección, selección, vista en vivo) | ✅ Completada |
| 2 | `vision/`: contorno, centroide, Position Fixture | ✅ Completada |
| 3 | `ml/`: embeddings ONNX (EfficientNet-Lite) | ✅ Completada |
| 4 | `database/`: esquema SQLite | ✅ Completada |
| 5 | `inspection_editor/`: canvas + herramientas de medición | ✅ Completada |
| 6 | Motor de inspección completo | ✅ Completada |

## Flujo de la demo

1. **Setup** (una sola vez): `.\run.ps1` (o doble clic en `run.bat`). El
   script verifica MSYS2 + 8 paquetes (los instala si faltan), descarga y
   prepara el modelo de embeddings, compila y lanza la app. Si algo no puede
   instalarse solo, **dice exactamente qué falta y cómo resolverlo a mano**.
   Variantes: `-NoRun` (solo preparar), `-Rebuild` (recompilar), `-Test`
   (correr los 87 tests).
   La ventana tiene una **barra de menú** (Cámara / Pieza / Inspección / Ver /
   Ayuda) para las acciones de configuración, y deja en la vista solo los
   controles de uso constante (combos de cámara/pieza/plantilla, Iniciar,
   Registrar y activar, Auto-inspección, Inspeccionar y la paleta de dibujo).

2. **Cámara**: elige una del combo (aparecen con su nombre real, listadas por
   la API nativa del SO sin abrirlas) y pulsa **Iniciar** para la vista en vivo.
   Con **Ver ▸ Mostrar
   contorno** activo (por defecto), el contorno de la pieza, su centro y su
   eje se dibujan sobre el video en tiempo real; **al ocultarlo, las
   herramientas se congelan en su sitio** (la pieza se inspecciona fija, sin
   que nada tiemble) y el análisis se pausa si no hay nada que medir. La
   unidad de medida se elige en **Ver ▸ Unidad**. La cámara elegida queda
   guardada. *Sin cámara, todos los pasos siguientes aceptan imágenes desde
   archivo* (`sample_images/pieza_demo.png` sirve para probar).

   **Orientación**: por defecto la pieza se muestra **vertical** (tal como la
   ve la cámara) — más estable y sin la inclinación arbitraria que daba el eje
   principal. Si tus piezas llegan giradas y quieres que las herramientas las
   sigan al rotar, activa **Ver ▸ Seguir rotación de la pieza** (ahí sí
   aplican el rasgo distintivo y la anisotropía).
3. **Dibujar sobre el video en vivo**: con la pieza detectada, elige una
   herramienta en la fila **Dibujar** (cada botón explica en su tooltip qué
   mide y cómo trazarla) y dibuja directamente sobre el video arrastrando el
   mouse — en tiempo real y anclado a la pieza: si la mueves o la giras, las
   herramientas la siguen. Al soltar, **la herramienta mide la pieza actual y
   se auto-sugiere sus tolerancias** (±10 % para distancias, conteo exacto
   para blobs). **Mover/Elegir** selecciona y arrastra; **Borrar
   herramienta** elimina la seleccionada.
   - *Caliper*: línea que cruce los dos bordes a medir → distancia entre ellos.
   - *Círculo*: arrastra del centro al borde → diámetro y redondez.
   - *Punto-Línea*: línea de referencia → distancia perpendicular del borde.
   - *Borde liso*: línea sobre un borde recto → desviación máxima (muescas).
   - *Blob*: rectángulo sobre una zona → conteo de manchas/agujeros.
   - *Blob poligonal*: región de forma libre (clic para marcar vértices, clic
     sobre el primero para cerrar) → mismo conteo que el Blob pero para zonas
     irregulares que un rectángulo no cubre bien.
   - *Regla*: distancia directa entre dos puntos fijos (no busca bordes) —
     con la escala calibrada mide en mm/cm al vuelo.
   - *Línea-Línea*: dos líneas de referencia (se dibujan en dos pasos) → mide el
     **ángulo** entre ellas en grados (0–90°), con la separación perpendicular en
     el detalle. La tolerancia se define en grados; no se calibra a mm.
   - *Ángulo*: una esquina (vértice + dos lados, en dos pasos) → mide el
     **ángulo interior** en grados (0–180°). Tolerancia en grados; ideal para
     chaflanes y esquinas.

   Los detalles de todas las herramientas incluyen **mm (y cm a partir de
   10 cm)** cuando hay calibración — también el círculo (diámetro, radio y
   redondez) y el área de los blobs (mm²). El Caliper empareja **bordes de
   polaridad opuesta** (mide anchos reales, no dos bordes del mismo lado), y
   la banda de muestreo del Caliper/Borde liso **se dibuja en pantalla** al
   cambiar el campo Puntos. Bajo "Pieza actual" hay botones **⟲/⟳ 90°** para
   girar cómo se ve la pieza (persiste con la pieza seleccionada).

   La **cantidad de puntos de muestreo** de cada herramienta es editable en
   **Plantilla…** (campo "Puntos"): banda del Caliper, rayos del Círculo,
   escaneos del Borde liso y área mínima del Blob.

   **Snap al borde al dibujar**: mientras trazas un Caliper, Regla o Borde liso,
   un marcador amarillo resalta el borde detectado más cercano al cursor (usa la
   detección de bordes subpíxel del proyecto sobre una banda alrededor del
   puntero); al soltar, el extremo se **pega** a ese borde para colocar la
   herramienta con precisión sin pulso fino.

   **Duplicar y copiar/pegar**: con una herramienta seleccionada, **Ctrl+D** la
   duplica con un pequeño desplazamiento. En el editor de plantilla, **Ctrl+C** /
   **Ctrl+V** copian y pegan; el portapapeles vive durante toda la sesión, así
   que copiar y reabrir el editor en **otra plantilla de la misma pieza** permite
   pegar allí la herramienta.

   **Edición fina con manijas**: al seleccionar una herramienta (modo
   Mover/Elegir) aparecen cuadraditos blancos en sus extremos editables.
   Arrástralos para ajustar un punto suelto sin borrar y volver a dibujar: los
   dos extremos de un Caliper/Regla/Borde liso, el centro y el radio del
   Círculo, las cuatro esquinas de línea/escaneo del Punto-Línea y la
   Línea-Línea, el vértice y los lados del Ángulo, o el centro y el tamaño del
   rectángulo del Blob. Arrastrar el cuerpo (no una manija) sigue moviendo la
   herramienta entera.

   **Las medidas salen en vivo**: cada herramienta dibujada muestra su valor
   junto al trazo, actualizándose con cada frame (en px, o en mm si
   calibraste), en verde si está dentro de tolerancia y en rojo si no. Los
   trazos tienen **estabilizador temporal**: quieto = clavados (banda muerta
   anti-ruido), movimiento suave = seguimiento con suavizado exponencial (sin
   vibración), movimiento rápido = respuesta inmediata, y **continuidad
   anti-giro de 180°** — en piezas casi simétricas sin rasgo distintivo el
   eje ya no da vueltas espontáneas: conserva el sentido del frame anterior.

   **Selección múltiple**: en modo Mover/Elegir, arrastra sobre un espacio
   vacío para dibujar un **marco de selección** — las herramientas dentro
   quedan seleccionadas y se mueven o borran (Supr) en grupo.

   **Escala por marcador ArUco en vivo** (**Cámara ▸ Escala por marcador
   ArUco**): imprime el marcador `sample_images/aruco_4x4_id0.png`, mide su
   lado real con una regla y escríbelo al activar la opción. Colócalo junto a
   la pieza (en el mismo plano): la escala px→mm se recalcula **en cada frame
   con la homografía del marcador** y se ajusta sola si acercas o alejas la
   cámara — no hay que recalibrar a mano. La barra de estado muestra la escala
   viva. (Puntos a distinta altura/profundidad respecto al plano necesitan una
   cámara de profundidad; con una sola cámara 2D no es recuperable.)

   **Calibración fácil desde una herramienta** (lo más rápido): traza una
   Regla (o Caliper/Círculo) sobre algo de tamaño conocido — una regla, una
   moneda —, selecciónala y pulsa **"Fijar escala con esta medida…"**;
   escribes cuánto mide de verdad en mm y la escala px→mm sale de ahí. Todas
   las cotas quedan en unidades reales al instante. (Sin calibrar hay que
   medir sobre algo conocido: no existe escala automática pura desde una sola
   cámara.)

   **Calibración a milímetros** (menú *Cámara ▸ Calibrar escala*): dos
   métodos —
   **A)** haz dos clics sobre una distancia real conocida (una regla, el
   diámetro de una moneda) y escribe los mm: la escala se calcula y además se
   **estima la distancia de la cámara a la superficie**; **B)** escribe la
   distancia cámara→superficie y el FOV horizontal de tu cámara (webcams:
   55–70°) y la escala sale del modelo pinhole. Con la escala calibrada,
   todas las medidas se muestran en mm además de px (al dibujar, en Probar y
   en los reportes de inspección). La escala queda guardada y vale mientras
   la cámara no cambie de altura; las tolerancias internas siguen en px.
   La calibración se **sella con la cámara y la resolución** en que se hizo: si
   cambias de cámara o de resolución, la barra de estado avisa **«⚠ Calibración
   obsoleta»** en lugar de mostrar milímetros silenciosamente equivocados —
   recalibra (tecla **C**) para la fuente nueva.

   **Gestión de piezas** (botón *Piezas…*): renombrar, **eliminar** (con sus
   referencias, herramientas e historial — pide confirmación), **miniatura**
   de la pieza registrada y el **ajuste de orientación**: gira el sistema de
   coordenadas de la pieza en grados (spin fino o botón +90°) para dejar el
   eje donde quieras; aplica al video en vivo, al registro y a la inspección.
   El campo **Puntos** de la fila de dibujo ajusta el muestreo de la
   herramienta seleccionada sin abrir el editor (banda/rayos/escaneos/área
   mínima), y el editor mide automáticamente al abrir: las medidas se ven
   siempre.

   **Varias plantillas por pieza**: el combo *Plantilla* + botón *+* permite
   tener distintos juegos de herramientas para una misma pieza (p. ej. una
   por cara); se inspecciona con la plantilla activa. **Unidad** a elección
   del operador (Auto / mm / cm / px) en la fila de cámara, persistente y
   aplicada en todas las medidas. El **rasgo distintivo se puede quitar** o
   reemplazar (botón *Rasgo distintivo* cuando ya existe). Las herramientas se
   borran con **clic derecho** sobre ellas (además de Supr y el botón). En
   **auto-inspección el dibujo queda bloqueado** — el operador solo lee
   piezas. El autodetector de orientación ahora mide la **anisotropía** de la
   pieza: si es casi redonda, congela el ángulo en vez de perseguir el ruido
   (las piezas redondas ya no giran solas).

   **Control de luces y sombras**: el botón **Detección…** ajusta el contorno
   automático — umbral manual (o Otsu automático), polaridad de la pieza
   (oscura/clara/automática), suavizado y limpieza morfológica. Y **Zona de
   detección** enfoca el análisis en un solo lugar: arrastra un rectángulo
   sobre el video (amarillo punteado) y el contorno solo se busca ahí —
   sombras y objetos fuera de la zona dejan de estorbar. Ambos ajustes
   persisten y aplican al video en vivo, al registro y a la inspección. Las
   herramientas de dibujo ahora usan **iconos** (el tooltip explica cada una).

   **Atajos de teclado** (botón *Atajos (F1)* o tecla `F1`): guía completa y
   **editable** — haz clic en el atajo y pulsa la combinación nueva; se
   guardan en la BD. Por defecto: `Ctrl+Z`/`Ctrl+Y` deshacer/rehacer las
   herramientas dibujadas (crear, mover, borrar — también dentro del editor),
   `Supr` borrar la seleccionada, `Esc` volver a Mover/Elegir, `1`–`5` elegir
   herramienta de dibujo, `V` iniciar/detener cámara, `R` registrar y
   activar, `A` auto-inspección, `I` inspeccionar, `P` plantilla, `C`
   calibrar, `D` rasgo distintivo.

   **Rasgo distintivo** (piezas simétricas o para robustez extra): con el
   botón *Rasgo distintivo*, haz clic sobre un punto visualmente único de la
   pieza (un agujero, una marca, una esquina oscura — rombo magenta). Ese
   rasgo fija la orientación del fixture: la pieza se detecta igual **en
   cualquier rotación, incluso girada 180°**, cosa que los momentos por sí
   solos no distinguen en piezas simétricas. Se guarda con la pieza y aplica
   al registro, al video en vivo y a la inspección.
4. **Registrar y activar**: un solo botón — pide el nombre **validando
   duplicados al instante** (si la pieza ya existe ofrece guardar como nueva
   versión de su referencia o renombrar), captura automáticamente 30
   referencias válidas del video (cada frame se valida: nitidez, exposición,
   pieza completa; los rechazos muestran el motivo en el progreso), guarda la
   referencia de embeddings, la miniatura **y las herramientas dibujadas**, y
   arranca la auto-inspección. El panel derecho muestra desde entonces la
   **comparación en vivo: pieza registrada vs pieza actual** (recortes
   normalizados) con la similitud y su umbral durante la auto-inspección.
5. **Auto-inspección**: el botón queda activo y la app inspecciona el video
   continuamente (~1/s): banner **OK/NG** en vivo, resultados por herramienta
   dibujados sobre el video y estadísticas del día en la barra de estado.
   Todo queda en el historial. Se puede prender/apagar cuando quieras con la
   pieza seleccionada.
6. **Afinar y aprender**: **Plantilla…** abre el editor sobre imagen fija
   para ajustar tolerancias con "Probar sobre esta imagen" (los valores
   medidos te dicen qué rangos poner). **Inspeccionar** hace una inspección
   única con reporte detallado y, si fue OK, **Actualizar referencia**
   incorpora ese embedding como versión nueva (las anteriores nunca se
   borran) — el aprendizaje incremental. **Registrar (asistente)…** sigue
   disponible para registrar desde imágenes de archivo sin cámara.

## Compilar y ejecutar (Windows)

Lo más simple: `.\run.ps1` (o doble clic en `run.bat`) — verifica MSYS2 y los
paquetes (los instala si faltan), descarga y prepara el modelo de embeddings,
compila si hace falta y lanza la app.

A mano (entorno **UCRT64** de MSYS2 en `C:\msys64` con
`mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,qt6-base,opencv,onnxruntime,protobuf}`):

```powershell
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
cmake --preset mingw-release
cmake --build --preset mingw-release
ctest --preset mingw-release
```

El ejecutable queda en `build/release/pc_inspector.exe` (necesita
`C:\msys64\ucrt64\bin` en el PATH para las DLL de Qt/OpenCV; `onnxruntime.dll`
se copia junto al binario automáticamente porque System32 trae otra
incompatible de Windows ML y gana al PATH).

## Fase 1 — Limitaciones conocidas

- **Enumeración sin abrir el dispositivo (segura ante drivers rotos)**: la lista
  de cámaras se pide al sistema operativo por su **API nativa** —DirectShow COM
  en Windows (`native_cameras.cpp`), V4L2 en Linux— que devuelve el **nombre
  amigable real** ("Integrated Camera", "DroidCam Source 3") **sin abrir el
  dispositivo ni negociar formato**. Antes se hacía `capture.open()` sobre cada
  índice solo para leer nombre/resolución, y abrir una cámara virtual no lista
  (p. ej. AndroidCam antes de conectar el celular) hacía que su driver
  (`kswdmcap.ax`) dividiera por cero y **tumbara todo el proceso** con una
  excepción estructurada que ningún `try/catch` de C++ atrapa. La resolución
  ya no se conoce hasta conectar (se muestra solo el nombre).
- **Apertura blindada a nivel del SO**: abrir la cámara (el punto donde un driver
  defectuoso puede fallar) va envuelto en `core::runProtected` (`crash_guard.*`),
  que usa un *Vectored Exception Handler* + `setjmp/longjmp` (GCC/MinGW no
  soporta `__try/__except`) para **sobrevivir a divisiones por cero y accesos
  inválidos del driver** y convertirlos en un simple "no se pudo abrir". Además,
  `installCrashHandler` deja un manejador de último recurso que, si el proceso
  muere igualmente a nivel del SO, escribe el código de excepción y la última
  operación en curso (breadcrumb) a `pc_inspector_crash.log`, para que un cierre
  que antes era silencioso quede diagnosticado. Portable: en plataformas sin SEH
  todo degrada a ejecución directa.
- **Sin control de backpressure explícito**: si la UI fuera más lenta que la
  cámara los frames encolados crecerían; en la práctica el repintado
  coalescido de Qt lo evita a resoluciones de webcam.
- **Cámara física requerida para la vista en vivo**: los tests unitarios cubren
  la lógica sin hardware (conversión de frames, FPS, `Result`); la captura real
  se verifica manualmente.

## Fase 2 — Módulo `vision/`

Segmentación por Otsu con polaridad automática, contorno mayor con centroide
por momentos, orientación por momentos centrales (ambigüedad de 180° resuelta
con el momento de tercer orden) y Position Fixture con `normalizePiece()` →
recorte canónico 256×256 sin fondo, listo para embeddings. En la UI, el
checkbox "Mostrar análisis" superpone contorno/centroide/eje en vivo (máximo
un análisis en vuelo; nunca bloquea la UI).

Limitaciones conocidas:

- **Requiere fondo contrastante y uniforme** (Otsu global); iluminación muy
  irregular degradará la segmentación.
- **Orientación inestable en piezas casi circulares o con simetría de
  rotación** — inherente al método de momentos; irrelevante para la
  comparación por embeddings de la fase 3.
- **La pieza debe estar completa dentro del encuadre**; si toca el borde el
  recorte normalizado puede recortarse.
- El overlay se verificó con imágenes sintéticas (31 tests); con cámara real
  queda pendiente de prueba manual del usuario.

## Fase 3 — Módulo `ml/` (embeddings)

`EmbeddingExtractor` envuelve ONNX Runtime C++ (sesión única, entrada NHWC/NCHW
autodetectada, normalización EfficientNet-Lite `(x-127)/128`, salida
L2-normalizada). `ReferenceBuilder` implementa Welford: media/desviación por
dimensión y estadística de similitud en O(dim) por muestra — la misma pieza
sirve para el registro inicial y para el aprendizaje incremental de la fase 6.

**Modelo**: el prompt pedía EfficientNet-Lite0, pero no existe un ONNX
confiable publicado de Lite0; se usa **EfficientNet-Lite4 del zoo oficial de
ONNX** (49 MB, misma familia y preprocesado). `run.ps1` lo descarga y
`prepare_model` (herramienta C++ compilada con el proyecto) le recorta el
clasificador para exponer los features del GAP: **embedding de 1280 dims**
en lugar del softmax de 1000 clases. Extracción medida: <220 ms con carga de
sesión incluida; la inferencia pura queda muy por debajo.

Limitaciones conocidas:

- **Toolchain migrado a UCRT64** (mingw64 no tiene onnxruntime precompilado);
  el entorno mingw64 anterior puede desinstalarse a mano si se desea liberar
  ~2 GB.
- Python aparece en `C:\msys64` solo como dependencia interna del paquete
  onnxruntime de MSYS2 — la aplicación no lo usa ni lo necesita en ejecución.
- El test de integración del extractor se salta (`GTEST_SKIP`) si el modelo no
  está descargado; `run.ps1` lo descarga automáticamente.

## Fase 4 — Módulo `database/` + `repositories/`

SQLite vía API C con wrapper RAII propio (`Db`/`Statement`, todo `Result<T>`,
sin excepciones cruzando la frontera). Esquema v1 completo con las 9 tablas
del prompt, migraciones por `PRAGMA user_version`, foreign keys, modo WAL y
`busy_timeout`. Las referencias de embeddings (`Embeddings`) se **versionan
por pieza y nunca se borran**: el aprendizaje incremental inserta una versión
nueva. `repositories/` es el puente domain↔database: `PieceRepository`
(roundtrip exacto de `ml::Reference` como BLOB float32) y
`SettingsRepository` (la cámara elegida se guarda y restaura al abrir).

Limitaciones conocidas:

- La BD vive junto al ejecutable (`pc_inspector.db`, demo portable); si no se
  puede abrir o migrar, la app sigue funcionando sin persistencia y lo deja en
  el log (nunca crash).
- BLOBs float32 en orden nativo little-endian: la BD no es portable a
  arquitecturas big-endian (irrelevante para x86/x64).
- Los repositorios de herramientas e inspecciones llegan con las fases 5/6
  que los consumen; el esquema ya los soporta.

## Fase 5 — `inspection_editor/` (editor de plantilla)

Editor estilo VisionMaster: botón **"Plantilla…"** en la ventana principal
(usa el último frame de la cámara o una imagen desde archivo —
`sample_images/pieza_demo.png` sirve para probar sin cámara). Las 5
herramientas se dibujan por arrastre, se seleccionan/mueven con el mouse, y
**"Probar sobre esta imagen"** las ejecuta al instante mostrando OK/NG y el
valor medido para ajustar tolerancias.

- **Caliper** (distancia entre 2 bordes), **Círculo** (diámetro + redondez por
  ajuste de mínimos cuadrados sobre 36 rayos), **Punto-Línea** (distancia
  perpendicular), **Borde liso / Edge Flaw** (desviación máxima respecto a la
  recta ajustada) y **Blob** (conteo por área mínima y polaridad).
- La geometría se guarda **en coordenadas del fixture** (tabla
  `InspectionTools`): si la pieza llega rotada, las herramientas se mueven con
  ella (verificado por test: misma medida ±1.5 px con la pieza a 20° y 125°).
- La detección de bordes usa perfil promediado + gradiente + refinamiento
  subpíxel parabólico (precisión típica ±1 px en sintético).

Limitaciones conocidas:

- Canvas demo: crear, seleccionar, mover y eliminar — sin handles de
  redimensionado ni undo (recrear la herramienta si se quiere otro tamaño).
- El editor trabaja sobre la pieza "demo" auto-creada; el registro completo de
  piezas con captura guiada llega en la fase 6.
- Medidas en píxeles (sin calibración mm, como define el prompt para el demo).
- La interacción del editor (mouse) se verificó compilando y abriendo la app;
  el flujo visual completo queda para prueba manual del usuario.

## Fase 6 — Motor de inspección completo

El círculo cerrado: **"Registrar pieza…"** abre el registro guiado (captura
manual/automática desde cámara o imágenes desde archivo; cada captura se
valida — nitidez, exposición, saturación, pieza completa y dentro del marco —
y se rechaza con su motivo). **"Inspeccionar"** corre en un hilo de trabajo:
similitud de embeddings contra la referencia + herramientas geométricas sobre
la imagen original → veredicto combinado OK/NG con banner, imagen anotada,
tabla por herramienta, y persistencia (historial + miniatura + estadísticas
del día en la barra de estado). Tras un OK, **"Actualizar referencia"**
ejecuta el aprendizaje incremental: nueva versión de la referencia (Welford,
O(dim)), las anteriores nunca se borran.

Arquitectura: `domain/` (veredicto y criterios de calidad, sin Qt ni OpenCV)
y `engine/` (orquestador). El extractor de embeddings se inyecta como función:
los tests end-to-end corren con embeddings sintéticos sin el modelo ONNX, y
sin modelo la app degrada a inspección solo geométrica (avisado, nunca crash).

Limitaciones conocidas:

- El umbral de anomalía es `simMean − max(3σ, 0.02)` sobre similitud coseno;
  con referencias de pocas muestras conviene registrar las 30 recomendadas.
- El registro necesita el modelo ONNX (la referencia SON embeddings); sin
  modelo el botón lo explica.
- Flujos de cámara en vivo verificados solo con imágenes sintéticas/archivo en
  esta PC (sin cámara detectable); pendiente de prueba manual con hardware.
