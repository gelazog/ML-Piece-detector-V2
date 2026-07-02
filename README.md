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
| 3 | `ml/`: embeddings ONNX (EfficientNet-Lite0) | Pendiente |
| 4 | `database/`: esquema SQLite | Pendiente |
| 5 | `inspection_editor/`: canvas + herramientas de medición | Pendiente |
| 6 | Motor de inspección completo | Pendiente |

## Compilar (Windows, MSYS2/MinGW64)

Requisitos: MSYS2 en `C:\msys64` con `mingw-w64-x86_64-{gcc,cmake,ninja,qt6-base,opencv}`.

```powershell
$env:PATH = "C:\msys64\mingw64\bin;$env:PATH"
cmake --preset mingw-release
cmake --build --preset mingw-release
ctest --preset mingw-release
```

El ejecutable queda en `build/release/pc_inspector.exe` (necesita
`C:\msys64\mingw64\bin` en el PATH para las DLL de Qt/OpenCV, o empaquetar con
`windeployqt6` más adelante).

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
