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
2. **Cámara**: elige una del combo (se detectan solas, probando MSMF y
   DirectShow) y pulsa **Iniciar** para la vista en vivo. Con **"Detectar
   pieza (contorno)"** activo (por defecto), el contorno de la pieza, su
   centro y su eje se dibujan sobre el video en tiempo real. La cámara
   elegida queda guardada para la próxima vez. *Sin cámara, todos los pasos
   siguientes aceptan imágenes desde archivo* (`sample_images/pieza_demo.png`
   sirve para probar).
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

   La **cantidad de puntos de muestreo** de cada herramienta es editable en
   **Plantilla…** (campo "Puntos"): banda del Caliper, rayos del Círculo,
   escaneos del Borde liso y área mínima del Blob.

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

- **Nombres de cámara genéricos**: OpenCV no expone el nombre real del
  dispositivo, se muestra "Cámara 0 (DirectShow)/…". Resolver requeriría
  llamar a Media Foundation directamente (posible mejora futura).
- **Sondeo multi-backend**: cada índice se prueba con MSMF y después con
  DirectShow (hay drivers, como muchas cámaras integradas, que MSMF no abre).
  El backend que funcionó queda guardado y es el que usa la captura. El
  sondeo corre en un hilo de trabajo y se corta tras 2 índices vacíos.
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
