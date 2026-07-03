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
| 5 | `inspection_editor/`: canvas + herramientas de medición | Pendiente |
| 6 | Motor de inspección completo | Pendiente |

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
  dispositivo, se muestra "Cámara 0/1/…". Resolver requeriría llamar a Media
  Foundation directamente (posible mejora futura).
- **Sondeo de cámaras lento**: enumerar por índice con `CAP_MSMF` tarda
  cientos de ms por índice; se hace en un hilo de trabajo con la UI en estado
  "Buscando cámaras…" y se corta tras 2 índices vacíos consecutivos.
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
