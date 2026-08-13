# Fuentes de imagen y banco de pruebas sin cámara

Dos frentes que comparten una idea: **la aplicación no debería saber de dónde
viene el frame**.

---

## La observación que lo ordena todo

`MainWindow` cuelga entero de una señal, `frameReady(QImage)`. Todo lo que hay
detrás —segmentar, contorno, fixture, herramientas, zona de trabajo, recuento,
medición automática, inspección— ya funciona sobre un `QImage` y **no pregunta
de dónde salió**. Lo único atado a la cámara es quién produce ese frame.

Así que «que lo del vídeo funcione en una imagen» no es un modo nuevo ni una
pantalla nueva: es **una fuente más**. Y si se hace como un modo aparte, lo que
se consigue es lo de siempre —dos caminos que divergen— y este proyecto ya pagó
eso una vez con los botones y otra con las tres paletas.

---

## F1 · Una imagen es una fuente, como la cámara

`camera::FrameSource` como interfaz: `start()`, `stop()`, `frameReady`,
`statsUpdated`, `sourceError`, `stopped`. Tres implementaciones:

- `CameraSource` — lo que hoy es `CameraController`, sin tocar su
  comportamiento.
- `StillImageSource` — una imagen de archivo. Emite el mismo frame a un ritmo
  bajo (basta refrescar para que el análisis vuelva a correr al cambiar un
  ajuste), no a 30 fps: repetir el mismo análisis treinta veces por segundo
  sobre una imagen que no cambia es quemar CPU para nada.
- `VideoFileSource` — un `.mp4`/`.avi` con `cv::VideoCapture`, a los fps del
  fichero, en bucle.

El desplegable de cámaras pasa a ser el de **Fuente**: las cámaras detectadas,
más «Abrir imagen…» y «Abrir vídeo…». El resto de la ventana no se entera.

**Lo que hay que vigilar**: `CameraController` tiene señales que solo tienen
sentido con una cámara (`controlsProbed`, `resolutionsProbed`, `exposureChosen`,
`profileRejected`). No se meten en la interfaz común: quien las tenga, las
tiene, y la ventana pregunta si la fuente es una cámara antes de conectarlas.

## F2 · Lo que no aplica se deshabilita CON MOTIVO

Con una imagen no hay exposición, ni enfoque, ni resolución que elegir, ni
perfil de medición que sondear. La regla ya existe en esta capa y solo hay que
extenderla: *si la fuente no deja tocar un control, no es culpa del operador y
no se pinta como si lo fuera*. Un control muerto sin explicación es peor que
uno ausente.

- Pestaña **Cámara e imagen**: deshabilitada, diciendo que la fuente es un
  fichero.
- **Asistente de enfoque**: no aplica; con una imagen fija la nitidez es la que
  es y no se puede mejorar girando nada.
- **Perfil de medición** y barrido de exposición: no se lanzan.
- **fps de captura**: con una imagen no significan nada. La barra de estado
  tiene que decir qué fuente es, no inventar un número.

## F3 · La calibración con una imagen

`ScaleCalibration` se ata a la resolución (`matchesResolution`) y la ventana
además guarda de qué cámara vino. Con un fichero:

- La resolución de la imagen sirve igual, así que el aviso de «cambiaste de
  resolución» sigue funcionando.
- **Pero la escala depende de la óptica y de la distancia al plano**, y un
  fichero no garantiza ninguna de las dos. Calibrar desde una imagen tiene que
  ser posible —es justo lo que hace falta para probar— y a la vez tiene que
  quedar claro que esa escala pertenece a esa imagen, no a la estación.
- Regla: al pasar de una fuente de fichero a una cámara (o al revés), si hay
  calibración, se avisa. No se borra: avisar y dejar decidir.

## F4 · La interfaz

Aquí es donde toca **usar la skill `qt-ui-design`** y auditar antes de dibujar.
Lo que ya se sabe que hay que resolver:

- El desplegable dice «cámara» y va a dejar de ser solo eso.
- «Actualizar cámaras» en el menú Cámara: el menú entero pasa a ser **Fuente**.
- Al abrir una imagen hay que ver **cuál** está abierta, no solo el vídeo.
- Los tres pasos del primer arranque (enfocar, calibrar, registrar) asumen
  cámara: con una imagen, «enfocar» no se puede hacer y el asistente tiene que
  saltárselo en vez de pedir lo imposible.

## F5 · El banco desde mi lado

Un ejecutable de consola, `pci_probe`, que corra el pipeline entero sobre un
fichero y escupa lo que midió. **Es lo que me permite comprobar sin cámara**, y
de paso sirve para CI y para que el operador mande un caso que falla.

```
pci_probe <imagen|vídeo> [--json] [--calibrar-largo PX=MM] [--medir] [--zona x,y,w,h]
```

Lo que tiene que imprimir, porque es lo que hay que poder comprobar:

- qué se segmentó y con qué área,
- el fixture (origen, ángulo),
- **la figura** que se reconoció y con qué desviación,
- las **propuestas de medición** con su valor y su motivo,
- la escala en mm/px si se pidió calibrar, y las medidas en mm,
- el reparto de tiempos por etapa.

Sin Qt Widgets: solo OpenCV y las librerías de dominio, para que arranque
rápido y corra en cualquier sitio.

## F6 · Pruebas de calibración desde imágenes

Con `pci_probe` y con tests:

- **Ida y vuelta de escala**: dibujar una figura de tamaño conocido en mm a una
  escala conocida, medirla, y exigir que vuelva el mismo número. Es la prueba
  que hoy no existe y la que de verdad dice si la calibración sirve.
- **Los dos métodos** (largo conocido y distancia+FOV) tienen que coincidir
  cuando describen la misma geometría; si no coinciden, uno de los dos miente.
- **El aviso de resolución**: calibrar a un tamaño y medir a otro tiene que
  avisar, no dar milímetros equivocados en silencio.
- Sobre `sample_images/aruco_4x4_id0.png`, que ya está en el repo.

---

## Orden

F1 y F5 primero, porque son la base: la fuente y el banco. F2/F3 salen solos en
cuanto la fuente existe. F4 al final, cuando ya se sabe qué opciones sobran de
verdad y no por adivinar. F6 en cuanto haya `pci_probe`.

**Regla de cierre**: cada punto se cierra con su test, su documentación
(README + ARQUITECTURA) y su commit atómico. Cuando estén todos, este fichero se
borra.
