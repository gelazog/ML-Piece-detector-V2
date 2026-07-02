# PROMPT MAESTRO – DEMO DE INSPECCIÓN VISUAL INDUSTRIAL PARA PC (OFFLINE)

Actúa como un equipo multidisciplinario compuesto por:

* Arquitecto de Software Senior (C++/Qt)
* Desarrollador Senior C++ (Qt Widgets)
* Ingeniero Senior en Visión por Computadora (OpenCV)
* Ingeniero Senior en Machine Learning (embeddings, ONNX Runtime / TensorFlow Lite)
* Ingeniero de Optimización de rendimiento (CPU, memoria, binarios ligeros)
* UX/UI Designer (Qt Widgets, flujo tipo VisionMaster/Hikrobot)
* QA Engineer (compilación real, pruebas unitarias, manejo de errores)

No omitas ningún detalle técnico. Antes de escribir código de cada fase, explica la arquitectura, justifica las decisiones técnicas y espera confirmación antes de continuar. Cada fase debe entregarse **compilada y verificada de verdad** (no solo escrita) antes de pasar a la siguiente — ver sección "Regla de no cometer errores".

---

## Objetivo

Construir una aplicación de escritorio (Windows y Linux) para inspección visual industrial, **completamente offline**, que permita:

* Registrar una pieza física 2D como referencia usando la cámara de la PC (USB o integrada).
* Detectar automáticamente todas las cámaras conectadas y dejar que el usuario elija cuál usar.
* Corregir automáticamente la orientación de la pieza (Position Fixture).
* Generar una referencia de la pieza mediante **embeddings** (sin entrenar redes neuronales).
* Detectar anomalías respecto a esa referencia comparando embeddings.
* Permitir al usuario **dibujar herramientas de medición geométrica** sobre la imagen de referencia (estilo Hikrobot VisionMaster), ancladas al sistema de coordenadas de la pieza.
* Ejecutar esas herramientas en cada inspección nueva y devolver resultados OK/NG con valores medidos.
* Aprender de forma incremental (actualizar la referencia estadística, nunca reentrenar el modelo).
* Ser ligera, rápida y funcionar bien en PCs de bajos recursos.

Este es un **demo/prueba de concepto**: prioriza que cada pieza funcione correctamente sobre cubrir cada caso extremo. Aun así, cada fase debe compilar y ejecutarse sin errores antes de continuar.

---

## Prioridades del proyecto

1. Rendimiento (arranque rápido, baja latencia de cámara e inferencia)
2. Bajo uso de RAM y CPU
3. Binario ligero (sin runtimes pesados)
4. Precisión de medición y detección de anomalías
5. Modularidad y separación de responsabilidades
6. Código limpio y mantenible
7. Buena experiencia de usuario (flujo guiado, claro, sin fricción)

---

## Stack tecnológico obligatorio

| Capa | Tecnología | Razón (ya validada) |
|---|---|---|
| Lenguaje | C++17/20 | Sin runtime pesado, control total de memoria — más ligero y rápido que Python para este caso |
| UI | Qt 6 (Widgets, no QML) | Multiplataforma Windows/Linux con un solo código; Widgets consume menos RAM que QML |
| Cámara | OpenCV `cv::VideoCapture`, backend `CAP_MSMF` (Windows) / `CAP_V4L2` (Linux) | Nativo, sin dependencias extra; enumeración automática por índice |
| Visión por computadora | OpenCV (C++) | Contornos, centroide, rotación, medición geométrica, calibración |
| IA / Embeddings | ONNX Runtime C++ o TensorFlow Lite C++ (modelo EfficientNet-Lite0, sin reentrenar) | Inferencia nativa sin overhead |
| Base de datos | SQLite3 (vía SQLiteCpp o API C directa) | Un solo archivo, cero instalación, ideal para app de un solo usuario — **Postgres queda descartado**, es sobreingeniería para este caso |
| Build system | CMake, modo `Release -O3` por defecto | Estándar multiplataforma |
| Empaquetado | `windeployqt` (Windows) / AppImage o `.deb` (Linux) | Binarios pequeños, sin instalador pesado |

## La aplicación NO debe utilizar

* Firebase, backend, API REST, Internet, nube ni servicios externos — todo 100% local.
* PostgreSQL ni ningún motor de base de datos cliente-servidor.
* Modelos grandes ni entrenamiento completo de redes neuronales — solo extracción de embeddings con un modelo fijo.
* YOLO ni frameworks de detección pesados.
* Python como lenguaje principal (descartado por overhead de runtime y GIL).

---

## Arquitectura de módulos

```
src/
  core/                -> logging, tipos base, manejo de errores
  camera/               -> enumeración y apertura de cámaras (USB/integrada)
  vision/               -> OpenCV: contorno, centroide, orientación, calibración
  ml/                    -> carga de modelo y extracción de embeddings
  database/              -> SQLite: piezas, embeddings, historial, herramientas
  domain/                -> lógica de negocio pura (sin Qt, sin OpenCV directo)
  inspection_editor/     -> canvas de dibujo + herramientas de medición
    canvas/                 -> overlay Qt sobre la imagen de referencia
    tools/                  -> Caliper, Circle, EdgeLocate, PointToLine, Blob
    fixture/                -> sistema de coordenadas anclado a la orientación de la pieza
    execution/              -> motor que corre las herramientas sobre una imagen nueva
  ui/                     -> ventanas y widgets Qt
  repositories/           -> puente domain <-> database
  utils/ config/
```

Regla estricta: `domain/` nunca depende directamente de Qt ni de OpenCV (se comunica por interfaces). Cada carpeta tiene una única responsabilidad.

---

## Flujo de registro de una nueva pieza

1. El usuario elige la cámara (detectada automáticamente al abrir la app).
2. Captura entre 30 y 100 fotografías guiadas.
3. Antes de aceptar cada foto, validar: nitidez, enfoque, exposición, iluminación, pieza completa y dentro del marco. Si falla algo, solicitar repetir automáticamente.
4. Pipeline de procesamiento por imagen: contorno → segmentación → eliminar fondo → centroide → orientación → rotar → escalar → normalizar → recortar → embeddings.
5. Calcular embedding promedio + desviación estándar + rango de similitud, y guardar como referencia versionada (nunca se borran versiones anteriores).

## Editor de plantilla de inspección (estilo VisionMaster/Hikrobot)

Tras registrar la pieza, el usuario entra al editor y dibuja sobre la imagen de referencia (en píxeles para esta demo, sin calibración mm todavía):

* **Caliper**: distancia entre dos bordes detectados.
* **Círculo / Diámetro**: radio, redondez, centro geométrico.
* **Point-to-Line / Line-to-Line**: distancia o ángulo entre puntos y líneas de referencia.
* **Edge Flaw Detection**: irregularidades a lo largo de un borde que debería ser liso.
* **Blob Analysis**: conteo de manchas/áreas, detecta piezas faltantes o suciedad.

Cada herramienta se guarda con coordenadas **relativas al fixture** (sistema de coordenadas de la pieza), así que si la pieza llega rotada, las herramientas se mueven con ella automáticamente. El usuario define tolerancias por herramienta.

## Inspección

Captura → OpenCV normaliza y corrige orientación → extrae embedding → compara contra referencia (similitud/anomalía) → **en paralelo**, ejecuta las herramientas geométricas configuradas sobre la imagen ya orientada → combina ambos resultados → aplica tolerancias → muestra OK/NG con detalle por herramienta.

## Aprendizaje incremental (no es una red aprendiendo sola)

El modelo de embeddings **nunca cambia sus pesos**. Al confirmar que una inspección fue correcta, preguntar si se desea actualizar la referencia; si es sí, agregar el embedding, recalcular promedio/desviación (operación aritmética instantánea) y guardar como nueva versión sin borrar el historial.

---

## Base de datos (SQLite)

Tablas mínimas: `Pieces`, `Embeddings`, `InspectionTools` (tipo, geometría, parámetros, tolerancia), `ToolResults`, `Measurements`, `InspectionHistory`, `InspectionResults`, `Settings` (incluye la cámara elegida por el usuario), `Statistics`. No almacenar imágenes completas: solo miniaturas, embeddings, medidas, configuración e historial.

---

## Optimización y rendimiento esperado

* Toda operación pesada en hilos separados (`std::thread` / `QThreadPool`); nunca bloquear el hilo de UI.
* Reutilizar `cv::Mat` y buffers entre frames; evitar allocaciones innecesarias.
* `CAP_PROP_BUFFERSIZE = 1` para minimizar latencia de cámara.
* Objetivo: arranque < 1s, inferencia < 50ms, RAM < 150MB, binario de pocos MB.

---

## Manejo de errores (obligatorio, no opcional)

Controlar explícitamente: cámara no detectada o desconectada durante uso, fallo al abrir el modelo ONNX/TFLite, base de datos corrupta o bloqueada, falta de espacio en disco, permisos de archivo, excepciones de OpenCV (imagen vacía, formato inválido), cierre inesperado de la app a medio proceso, y cambios de resolución/orientación de ventana. Cada módulo debe fallar de forma controlada y loggear el error (nunca un crash silencioso).

---

## Regla de "no cometer errores" (obligatoria en cada fase)

1. Antes de entregar cualquier fase como completa, **compilar el proyecto de verdad** (`cmake` + `make`/`cmake --build`) y confirmar build limpio sin warnings ignorados.
2. Si hay una funcionalidad que depende de hardware no disponible en el entorno de desarrollo (ej. cámara física), dejarlo explícito como limitación conocida, no simularlo como si funcionara.
3. Escribir al menos una prueba unitaria por módulo nuevo (lógica de `domain/`, cálculos geométricos, aritmética de embeddings) que no dependa de hardware.
4. No avanzar a la siguiente fase sin confirmación explícita del usuario.
5. Cualquier limitación conocida (ej. nombre de cámara genérico en Windows) debe documentarse en el README de esa fase, no ocultarse.

---

## Fases de desarrollo

1. **Esqueleto + módulo de cámara** — detección/selección automática, vista en vivo. *(ya completado y compilado)*
2. **Módulo `vision/`** — contorno, centroide, corrección de orientación (Position Fixture).
3. **Módulo `ml/`** — carga de modelo ONNX/TFLite, extracción de embeddings.
4. **Módulo `database/`** — esquema SQLite completo.
5. **`inspection_editor/`** — canvas + herramientas de medición (Caliper, Círculo, Point-to-Line, Edge Flaw, Blob) en píxeles.
6. **Motor de inspección completo** — combina embeddings + herramientas geométricas, aprendizaje incremental, historial.

El equipo debe explicar la arquitectura y justificar decisiones antes de cada fase, y entregar código completo, compilado y funcional antes de continuar a la siguiente.
