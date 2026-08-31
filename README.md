# PC Inspector — Demo de inspección visual industrial (offline)

Aplicación de escritorio C++20 / Qt 6 Widgets / OpenCV para registrar piezas 2D
como referencia (embeddings) y detectar anomalías + mediciones geométricas,
100 % offline. Especificación completa en
[PROMPT_MAESTRO_PC_INSPECTOR.md](PROMPT_MAESTRO_PC_INSPECTOR.md).

**¿Cómo funciona por dentro?** [ARQUITECTURA.md](ARQUITECTURA.md) explica cada
subsistema, qué modelo se usa y cómo se llama cada técnica, e incluye una lista
razonada de **cómo mejorarlo**. Este README es el manual de uso.

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
   (correr la batería de tests) y **`-Package`** (arma
   `build/package/PCInspector.zip`, un paquete portable con Qt, OpenCV,
   onnxruntime y el modelo: se descomprime y se ejecuta `pc_inspector.exe` en
   una PC **sin MSYS2**; ~178 MB, la mitad es el modelo).
   La ventana tiene una **barra de menú** (Cámara / Pieza / Inspección / Ver /
   Ayuda) para las acciones de configuración, y deja en la vista solo los
   controles de uso constante (combos de cámara/pieza/plantilla, Iniciar,
   Registrar y activar, Auto-inspección, Inspeccionar y la paleta de dibujo).
   La **barra de estado** muestra tres indicadores verde/rojo con tooltip —
   **Cám** (cámara transmitiendo), **BD** (base de datos conectada) y **ONNX**
   (modelo de embeddings cargado) — para ver de un vistazo qué está disponible.

2. **Fuente**: elige del combo y pulsa **Iniciar**. Hay tres tipos y **todos
   funcionan igual** de ahí en adelante:

   - una **cámara** (aparecen con su nombre real, listadas por la API nativa del
     SO sin abrirlas),
   - **Abrir imagen…** — una foto de la pieza,
   - **Abrir vídeo…** — un `.mp4`/`.avi`, que se reproduce en bucle.

   Con una imagen o un vídeo tienes **exactamente lo mismo** que con la cámara:
   detección de contorno, fixture, zona de trabajo, herramientas de medición,
   medición automática, recuento de piezas e inspección. Sirve para preparar una
   plantilla en el despacho, para reproducir un caso que falló en la línea (te
   mandan la foto y lo ves tú), y para trabajar cuando la cámara no está.

   **Sin ninguna cámara conectada la aplicación ya no se queda inservible**:
   abre una imagen y puedes hacerlo todo. `sample_images/pieza_demo.png` sirve
   para probar ahora mismo.

   **Capturar foto** congela el frame que estás viendo y trabajas sobre esa
   foto: con el vídeo en vivo la pieza tiembla y la detección late, así que
   dibujar una herramienta encima es puntería. Sobre una foto se traza, se
   calibra y se mide con calma. **La cámara no se cierra** — vuelves al vídeo con
   el mismo botón, al instante y sin que se te muevan los ajustes. Y la
   **calibración sigue valiendo**, porque la foto salió de esa misma cámara a esa
   misma distancia; eso no pasa al abrir un fichero, y por eso ahí sí se avisa.

   Mientras hay un fichero abierto, el desplegable **dice cuál es** y el
   indicador de la barra de estado cambia a **Img** o **Víd** en vez de *Cám* —
   con una imagen no estás mirando la cámara, y el indicador no debería decir
   que sí. Los ajustes que no se pueden tocar sobre un fichero (brillo,
   exposición, enfoque, resolución) **explican por qué** en su pestaña en vez de
   salir muertos, y recuerdan que todo lo demás funciona igual. El diálogo de
   abrir vuelve a la última carpeta que usaste.

   Con **Ver ▸ Mostrar
   contorno** activo (por defecto), el contorno de la pieza, su centro y su
   eje se dibujan sobre el video en tiempo real; **al ocultarlo, las
   herramientas se congelan en su sitio** (la pieza se inspecciona fija, sin
   que nada tiemble) y el análisis se pausa si no hay nada que medir. La
   unidad de medida se elige en **Medida ▸ Unidad de medida**: automática
   (mm o cm según el tamaño), milímetros, centímetros, **pulgadas** o píxeles.
   Las pulgadas se escriben con **tres decimales** —una pulgada son 25,4 mm, así
   que con dos el último dígito valdría un cuarto de milímetro—. La cámara
   elegida queda guardada.

   La **primera vez** que arrancas, sobre el vídeo aparece una línea con el
   siguiente paso que toca —calibrar y luego registrar una pieza—. Se quita con
   *Entendido* y no vuelve: a partir de ahí el estado lo llevan los indicadores
   de la barra.

   En la **barra de estado**, abajo, hay cuatro indicadores que dicen de un
   vistazo si estás midiendo en condiciones: **escala**, **enfoque**,
   **exposición** y **zona de trabajo**. Cada uno explica en su tooltip qué
   significa, y un clic te lleva a donde se arregla.

   Verde es que está bien; **ámbar es que afecta a la medida** y rojo que ya la
   está estropeando. Gris no es un aviso: quiere decir «no configurado, y en
   este caso no pasa nada» — medir en píxeles sin calibrar, o procesar la imagen
   entera, son formas legítimas de trabajar.

   **Configurar** (menú *Configurar ▸ Configurar…*) es **el único sitio** donde se
   ajusta cómo se ve y cómo se detecta la pieza: cámara e imagen, detección,
   escala, preferencias y atajos, en pestañas. **No bloquea la ventana**, y eso
   es a propósito: ajustar un umbral o un enfoque consiste en mover y mirar, y
   con un diálogo encima del vídeo no se ve el efecto de lo que tocas. Trae
   *Aplicar* además de *Aceptar*, y recuerda en qué pestaña te quedaste.
   Escala y Atajos abren su asistente desde su pestaña, porque calibrar es
   hacer clic en dos puntos de una foto y los atajos son una tabla: meterlos a
   la fuerza en un formulario los haría peores.

   **Cámara e imagen** (pestaña de *Configurar*, con la
   transmisión en marcha): brillo, contraste, ganancia, exposición y enfoque de
   la **propia cámara**, no del procesado. La página aplica al instante, así
   que mueves un deslizador y ves el efecto en el vídeo al instante. Al abrir la
   cámara se **sondea de verdad qué admite**: se intenta escribir cada control y
   se mide su rango real, así que solo quedan activos los que la cámara deja
   cambiar (con su recorrido correcto en el tooltip) y el resto sale
   deshabilitado con el motivo. Los controles manuales de exposición y enfoque
   se bloquean mientras su automático está encendido, porque ahí no harían nada.

   Ahí mismo está el selector de **resolución**. OpenCV no sabe listar las que
   admite una cámara, así que hay que preguntárselas una por una: el botón
   **Buscar…** lo hace, tarda unos segundos y **el vídeo se detiene mientras
   dura**, por eso el resultado se recuerda para esa cámara y no hay que
   repetirlo. Más resolución = más detalle y medidas más finas, a cambio de más
   CPU por frame. Al cambiarla, la **calibración en mm deja de ser válida** (la
   app te avisa) y **la zona de detección y el cero fijado del tablero se
   reajustan solos** a la nueva escala, para que sigan señalando el mismo sitio.
   La resolución elegida se guarda y se reaplica al iniciar la cámara.

   **Asistente de enfoque** (mismo panel): una barra que sube con la nitidez de
   la imagen, con marca del **mejor valor alcanzado** y un botón para
   reiniciarlo. Mueve el enfoque hasta que la barra llegue lo más arriba
   posible — antes había que adivinar si la imagen había mejorado. La barra es
   **relativa al máximo visto** porque la nitidez no tiene tope absoluto: lo
   único que significa algo es comparar. Y se mide **sobre la pieza**, no sobre
   el frame entero: con un fondo texturizado, la nitidez del encuadre puede
   subir mientras la de la pieza baja, y estarías enfocando la mesa. Sin pieza
   detectada mide el centro del encuadre y lo dice.

   Para una
   línea estable conviene **desactivar la exposición y el enfoque automáticos**
   y fijarlos: si no, la cámara cambia el brillo o "bombea" el foco entre
   frames y las medidas bailan. Los valores que toques se guardan y se
   reaplican la próxima vez que inicies la cámara.

   En una cámara que no has configurado nunca, la app lo intenta sola: al abrir
   **prueba varias exposiciones, mide cuál va rápido sin oscurecer la pieza** y
   la fija. Cuesta unos tres segundos y **puede salir que no compense** — es lo
   normal con poca luz, porque en automático la cámara sube también la ganancia
   y al fijar la exposición esa ayuda se pierde. Si pasa, vuelve al automático y
   te dice el motivo: pon una lámpara sobre la pieza y al siguiente arranque la
   respuesta cambia sola. También te avisa si la cámara **acepta los cambios y
   no reacciona a ninguno** — hay modelos que dicen que sí y no hacen nada, y
   ahí no se puede dar por buena una repetibilidad que no existe.

   **Dónde se va el tiempo** (pestaña *Rendimiento* de *Configurar*): marca
   *Medir el reparto por etapas* y verás en qué se va cada análisis —segmentar,
   contorno, fixture, normalizar y herramientas— en milisegundos y en
   porcentaje, promediado sobre los últimos 30. Va **apagado por defecto** y no
   por prudencia: esto corre en cada frame, y apagado no cuesta ni una llamada
   al reloj. Enciéndelo mientras miras y apágalo al terminar.

   Las herramientas se ejecutan **repartidas entre los núcleos** de la máquina,
   que es donde está el grueso del tiempo: con veinte herramientas grandes,
   9 ms en vez de 34. Dan exactamente las mismas cifras — hay pruebas que lo
   exigen medida a medida.

   La última línea, *sin atribuir*, es el tiempo que el desglose no reparte
   entre las etapas. Debería ser pequeña; si crece, es que hay trabajo real
   fuera de lo que se está midiendo y el reparto ya no dice dónde apretar.

   **Zona de trabajo** (pestaña *Rendimiento* de *Configurar*): dónde busca el
   programa la pieza en cada imagen. Si la pieza ocupa una esquina, mirar la
   imagen entera es tirar el resto del trabajo — medido sobre 1280×720 con una
   pieza de 180×140, recortar va **6 veces más rápido**. Cuatro opciones:
   **imagen entera** (nunca recorta), **zona automática** (un recorte que sigue
   a la pieza), **zona de detección fija** (el rectángulo que dibujaste a mano)
   y **zona libre** (el contorno que dibujaste a pulso). La zona activa se
   **dibuja sobre el vídeo**: tienes que poder ver por dónde está mirando el
   programa.

   **De fábrica viene en automática**, y también es a donde se vuelve al quitar
   una zona dibujada. El motivo es que la automática no puede cambiar ninguna
   respuesta: ante cualquier duda suelta el recorte y lo dice. Un ajuste que
   solo puede ir más rápido no tiene por qué esperar a que alguien lo descubra
   en una pestaña. Las dibujadas a mano sí cambian la respuesta —para eso las
   dibujas— y por eso esas se eligen y no se ponen solas.

   La zona automática **se rinde y vuelve a la imagen entera** en cuanto duda —
   si pierde la pieza, si la pieza toca el borde del recorte o si cambia de
   tamaño de golpe — y dice cuál de las tres cosas pasó. Un recorte que se
   equivoca es peor que no tenerlo: mediría con confianza dentro de un
   rectángulo donde ya no hay nada.

   También **se aparta sola cuando hay que contar piezas**: el recorte rodea a
   una sola pieza, así que contar dentro de él daría siempre 1 aunque haya seis
   en la mesa. Si tu pieza espera varias, o mientras miras la pestaña *Piezas*,
   se analiza la imagen entera y el recuento es el de verdad. La zona fija **sí**
   sigue limitando el recuento, y a propósito: si la dibujaste, dijiste «mira
   solo aquí».

   No hace falta pasar por esa pestaña para usar una zona fija: **con dibujarla
   basta**. El menú *Zona* de la barra (*Dibujar zona rectangular*) te deja arrastrar el
   recuadro sobre el vídeo y a partir de ahí se usa (el modo pasa solo a «zona
   fija»); con *Zona ▸ Quitar la zona* se borra y se vuelve a la imagen entera. La
   pestaña *Rendimiento* sirve para cambiar de modo o ver qué se está
   procesando ahora mismo.

   **Zona libre** (el botón de al lado): la misma idea sin la obligación de que
   sea un rectángulo. Es para lo que un rectángulo no puede separar — el borde
   del útil pegado a la pieza, la sombra de un lado, la pieza de al lado en
   diagonal: con dos piezas en diagonal **ningún** rectángulo contiene a una sin
   tocar a la otra, porque sus envolventes se solapan.

   Se dibuja de dos maneras, las dos con el mismo botón:

   - **Arrastrando**, rodeando la zona a pulso. Rápido.
   - **A clics**, marcando las esquinas y cerrando sobre la primera (o con doble
     clic). Exacto, para seguir un borde.

   El **botón derecho deshace** el último vértice; sin vértices, cancela. Lo que
   quede fuera de la zona **se oscurece sobre el vídeo**, para que veas qué está
   mirando el programa: en un rectángulo el dentro y el fuera se leen solos, en
   un contorno irregular no.

   No pierdes velocidad: por dentro se sigue recortando por la envolvente del
   polígono, y el contorno solo añade precisión encima. El trazo se guarda
   simplificado —cientos de puntos a pulso no aportan nada que la mano pueda
   sostener— con la garantía de que el borde no se mueve más de un píxel y pico:
   medido, 0,93 px sobre un círculo de 300 px de diámetro.

   **Separar las piezas que se tocan** (pestaña *Detección*). Cuando dos piezas
   se rozan, el contorno exterior las devuelve como **una** — y con una pieza no
   hay nada que recorrer con las flechas ni que enseñar en el mosaico. Con esto,
   cada mancha se mira por dentro: se busca el «corazón» de cada pieza (la zona
   más alejada del fondo) y se corta por el cuello que las une.

   **Nace apagada, y está medido por qué.** Sobre imágenes reales: dos engranajes
   engranados pasan de **1 a 2** piezas (lo arregla), tres tornillos en fila
   siguen en 3, una bandeja de cien tuercas sigue en 100 — pero **un tornillo
   largo solo pasa de 1 a 2**, porque su cabeza y su vástago se parecen bastante
   a dos piezas. Enciéndela si tus piezas se tocan; déjala apagada si son
   alargadas con cabeza. Cuesta entre 3 y 16 ms por análisis.

   Y tiene un alcance medido: con piezas de 260 px **aguanta un 13 % de solape**
   entre ellas y **se rinde con un 19 %**, devolviendo **una** pieza. Entre esos
   dos valores no se ha probado, así que no se sabe dónde está exactamente el
   límite — solo que está ahí en medio.

   Rendirse es lo correcto: con media pieza dentro de otra, el cuello es tan
   ancho como las propias piezas y no hay forma de saber dónde acaba una. Lo que
   importa es que devuelva **una** y no invente tres.

   En la pestaña *Detección* están además el **área mínima y máxima de pieza**
   (en % de la imagen): deciden qué se acepta como pieza y antes estaban fijas
   en el código. **Debajo del área mínima te dice qué significa ese porcentaje
   en la imagen que tienes delante** —«*en la imagen de ahora (631×477 px) son
   1505 px²: una mancha de unos 39×39 px no llega a pieza*»— **y cuántas piezas
   entran y se caen con ese valor**: «*con este valor pasan el mínimo 4 y se
   quedan fuera 12 por pequeñas*». Las dos frases cambian mientras mueves el
   número, así que bajarlo hasta que entren todas es un gesto de un segundo. Nadie mira una tuerca y piensa «esto es el 0,4 % del
   encuadre»; un cuadrado de 39 px sí se compara de un vistazo. Con piezas pequeñas, el 0,5 % por defecto es justo la frontera
   entre "no hay pieza" y "hay pieza" — bájalo si no se detectan, súbelo si se
   cuela ruido.

   **Y ahora te dice cuántas se han quedado fuera por ese mínimo, en el propio
   rótulo.** El recuento de piezas pone «**4 piezas +12 pequeñas**», sin tener
   que pasar el ratón por encima; la explicación de al lado dice el resto:
   «*además, 12 mancha(s) más se quedaron fuera por no llegar al área mínima; si
   son piezas tuyas, baja «Área mínima» en Configurar ▸ Detección*».

   El número corto va a la vista a propósito: un aviso que solo aparece al pasar
   el ratón no está, y mientras tanto la pantalla afirma «4 piezas» sobre una
   foto que tiene dieciséis.

   Hacía falta porque antes se descartaban en silencio. Sobre una foto de
   catálogo con dieciséis arandelas graduadas, el mínimo de fábrica deja **una**
   — y el operador veía «1 pieza» sin motivo y sin saber qué tocar. El mínimo
   está bien donde está: bajándolo lo suficiente para las dieciséis, entran
   también los rótulos y la regla de la foto. Lo que faltaba era decirlo.

   **Y se calla donde no hay nada que decir**: medido sobre el banco, cinco de
   nueve fotos no pierden ni una mancha, incluida la bandeja de cien tuercas.
   Un aviso que sale en todas las escenas se aprende a ignorar.

   **Modo automático o manual** (pestaña *Piezas* de *Configurar*). En
   **automático** el programa cuenta las que haya y **las mide todas**; el
   número no se juzga. En **manual** declaras cuántas tiene que haber, y si
   aparecen más o menos es NG. Declarar **una** apaga la enumeración a
   propósito, para que una sombra o un reflejo no cuenten como segunda pieza.
   El campo llega hasta **256**, que es el tope del detector.

   Y en esa misma página está **Ver todas las piezas en mosaico**, que abre el
   panel de la cuadrícula. Se guarda **con la pieza**: «bandeja de cien
   tuercas» es una propiedad del trabajo, y quien pasa de una bandeja a una
   pieza suelta en el mismo turno no tiene por qué acordarse de abrir y
   cerrar un panel cada vez.

   **Piezas esperadas** (pestaña *Piezas* de *Configurar*): cuántas piezas
   debería haber en la imagen. Hasta ahora la aplicación se quedaba con la
   **mayor** y borraba el resto en silencio, así que una bandeja con cinco
   tornillos y otra con seis daban el mismo resultado. Con el número declarado,
   **que falte una es NG por sí solo** —"se esperaban 6 piezas y se ven 5"—, sin
   necesidad de tener ninguna herramienta dibujada. El botón *Usar lo que se ve
   ahora* rellena el número con lo que la cámara está detectando, y la propia
   página avisa si lo que hay puesto no cuadra con lo que se ve, para que no te
   enteres cuando ya estás en producción. El número **se guarda con la pieza**,
   no con la máquina: "seis tornillos en bandeja" es una propiedad del trabajo.
   Con 0 se desactiva la comprobación.

   Y no solo se cuentan: **se miden todas con la misma plantilla**. Las
   herramientas viven en coordenadas de pieza, así que medir la segunda barra
   de la bandeja es aplicarle la misma plantilla desde su propio centro. El
   veredicto de la bandeja es **el de la peor pieza**, y el motivo del NG dice
   **en cuál mirar** ("ancho (pieza 3) fuera de tolerancia") — sin eso habría
   que ir pieza por pieza a mano. En el vídeo se dibujan las marcas de todas
   pero **los números de una sola**: treinta etiquetas encima de seis piezas no
   se leen.

   Y para **revisarlas una por una** hay dos caminos, según cuántas haya. Con
   dos o tres, las flechas `◀ ▶` de la barra inferior van saltando de pieza en
   pieza en **orden de lectura** —arriba-izquierda a abajo-derecha—, que es el
   mismo orden que usan los números del informe. Con una bandeja llena eso deja
   de servir: nadie pulsa la flecha cien veces. Ahí entra **Ver ▸ Piezas del
   encuadre (mosaico)**, que recorta cada pieza y las pone en cuadrícula, todas
   al mismo tamaño y con su número, para verlas de un vistazo y **pulsar la que
   desentona**. Se ofrece solo la primera vez que aparecen varias piezas; si lo
   cierras no se te vuelve a abrir, y lo recuperas desde ese menú.

   Elijas por donde elijas, la pieza elegida es la misma cosa: **la que miden
   las herramientas** y la que el vídeo **remarca más gruesa** mientras las
   demás bajan a un tono apagado. Ese engrosamiento solo aparece cuando la
   elección es tuya — si la pieza le ha tocado por ser la mayor, destacarla
   afirmaría una decisión que nadie tomó.

   **Y la elección vale para todo**, no solo para el vídeo. *Medir pieza*, la
   medición automática del editor y el propio editor miden **la pieza que has
   señalado**. Durante un tiempo no fue así: el navegador solo lo entendía el
   vídeo, y esos tres caminos analizaban siempre **la mayor del encuadre**. En
   una foto de diez arandelas surtidas donde la mayor es la octava, señalar la
   primera y pedir el informe devolvía «Arandela, 274 px» cuando la pieza
   señalada es un «Polígono redondeado de 3 lados» de 95 px — sin ningún aviso.


   **Orientación**: por defecto la pieza se muestra **vertical** (tal como la
   ve la cámara) — más estable y sin la inclinación arbitraria que daba el eje
   principal. Si tus piezas llegan giradas y quieres que las herramientas las
   sigan al rotar, activa **Ver ▸ Seguir rotación de la pieza** (ahí sí
   aplican el rasgo distintivo y la anisotropía).
   **Con un vídeo abierto** aparece debajo una barra de reproducción: **Pausa /
   Seguir**, un **paso a paso** (`▶|`) que avanza un solo frame y se queda ahí, y
   una **barra de tiempo** para buscar. Sirve para lo que se abre un vídeo:
   encontrar EL frame en el que la pieza se ve bien y trabajar sobre él —
   pausado, la pieza no tiembla y dibujar una herramienta deja de ser puntería.

   El paso a paso existe porque con la barra no se puede elegir el frame: en un
   vídeo largo, un píxel de barra son varios frames. Y si el fichero no dice
   cuántos frames tiene —pasa más de lo que parece— la barra se apaga y verás el
   número de frame: colocar el pulgar sin saber el total sería inventarse dónde
   va el vídeo.

   **La tira de capturas** (panel *Capturas*, a la izquierda): cada vez que
   pulsas **Capturar foto**, la foto se queda ahí en vez de sustituir a la
   anterior. Así puedes juntar varias de la misma pieza —o de piezas distintas—
   para compararlas, guardarlas como historial y reutilizarlas después.

   Clic en una miniatura para trabajar sobre ella (medir, dibujar, inspeccionar:
   todo funciona igual que sobre el vídeo). **Guardar todas…** las vuelca a una
   carpeta y **Vaciar** limpia la tira sin tocar lo ya guardado.

   Se guardan en **PNG** y con el nombre `pieza_AAAAMMDD-HHMMSS_nn.png`. Los dos
   detalles tienen motivo: PNG porque estas fotos son para volver a medir sobre
   ellas y el JPEG inventa bordes donde no los hay; y la fecha en ese orden para
   que **el orden alfabético de la carpeta sea el cronológico**. Nunca sobrescribe
   una foto anterior.

   No se guardan solas a cada disparo, y es a propósito: en una puesta a punto se
   disparan veinte fotos de las que interesan tres, y una carpeta con diecisiete
   descartes es peor que no tener carpeta.

   **Corregir el borde a mano** (botón *Corregir borde*): cuando la detección se
   equivoca —una sombra que se come un lado, un reflejo que parte la pieza—
   puedes **pintar** sobre la imagen para añadir a la pieza lo que le falta o
   quitarle lo que le sobra. Verde lo que añades, rojo lo que quitas, y el
   círculo del pincel sigue al cursor para que veas cuánto abarca antes de
   pintar. Al soltar se reanaliza y ves el borde nuevo.

   **Solo con una foto o una imagen abierta**, y el botón te lo dice cuando no:
   en vídeo en vivo el contorno se recalcula en cada frame, así que un borde
   corregido a mano dejaría de valer en cuanto la pieza se moviera. Captura una
   foto y corrígela ahí.

   **El trazo desaparece cuando ha hecho su trabajo.** En cuanto el contorno
   corregido aparece en pantalla, la mancha verde o roja se retira: lo que ves
   es el borde que detecta el programa, no lo que pintaste. La corrección
   **sigue puesta** — un aviso «Borde corregido» junto al modo de medición te
   lo dice, con cuántos píxeles lleva.

   **Rodear una pieza entera, en vez de pintarla** (*Corregir borde ▸ Marcar una
   pieza rodeándola…*): cuando en un lote hay una pieza que la detección **no
   ve** —mate, en sombra, de un tono que el umbral global se deja fuera—,
   pintarla con el pincel son decenas de pinceladas y lo que queda es una
   silueta dibujada a pulso. Rodéala de un trazo y el programa **busca el borde
   dentro**: vuelve a separar pieza y fondo mirando solo esa zona, que es donde
   sí hay contraste.

   Eso importa para lo que viene después: **el borde sale de la imagen, no de tu
   mano**, así que la pieza se puede medir. Sobre una pieza de 100×100 px
   rodeada con un trazo 22 px por fuera y ondulado, lo que sale mide 100×100.

   Y si ahí dentro no hay nada que detectar —fondo liso, o un trazo tan ceñido
   que no deja fondo con el que comparar—, **se dice**: la pieza se marca igual,
   porque vale para contarla y para no perderla, pero el mensaje avisa de que
   sus cotas serían las del trazo.

   **Descartar lo que no es una pieza** (*Corregir borde ▸ Descartar lo que no es
   una pieza…*): el mismo gesto al revés, para una sombra, un reflejo o un
   rótulo impreso en la mesa. Todo lo que quede dentro del trazo pasa a ser
   fondo. Las dos cosas entran en el **mismo Ctrl+Z** que las pinceladas.

   **Deshacer y rehacer**: con el pincel activo, **Ctrl+Z** deshace la última
   pincelada y **Ctrl+Y** la rehace. Con el pincel apagado, esos mismos atajos
   siguen siendo los de las herramientas dibujadas, como siempre: hay un solo
   deshacer, y hace lo que toca según tengas el pincel en la mano o no.
   También están en el menú *Corregir borde*. Quitar todas las correcciones
   también se deshace.

   La corrección vale para **esa imagen**: no cambia cómo se detectan las demás.
   Pero no hace falta que la ajustes a ojo si se repite — para eso está lo
   siguiente.

   **Afinar el borde a subpíxel** (*Configurar ▸ Configurar…*, pestaña **Detección**, apartado **Precisión**): el borde
   de una pieza no es un escalón — el brillo cambia a lo largo de varios
   píxeles, y sobre una foto real esa rampa medía **15 px**. Un umbral coloca el
   borde en cualquier punto de ella según la iluminación. Con esto, cada punto
   del contorno se pone donde el brillo cruza la mitad entre el nivel de dentro
   y el de fuera **en ese punto**. Medido sobre un borde de posición conocida,
   el error pasa de **0,417 px a 0,025 px**.

   **Nace apagado, y a propósito.** Cambia dónde está el borde, así que cambian
   el área, el perímetro y todas las cotas a la vez. Si ya tienes tolerancias
   ajustadas, **revísalas** al encenderlo: una pieza buena podría salir NG por el
   cambio de definición y no por un defecto. El programa te lo avisa al cambiarlo.

   Para verlo antes de decidir: `pci_probe tu_foto.jpg --subpixel` frente a la
   misma orden sin la bandera.

   **Afinar la detección con la corrección** (*Corregir borde ▸ Afinar la
   detección…*, se enciende en cuanto hay algo pintado): tu corrección es,
   literalmente, la respuesta correcta para un caso que el programa falló. Con
   ella delante, el programa **prueba ajustes** y busca cuál habría dado ese
   mismo borde solo.

   Te dice las dos cifras: cuánto coinciden los ajustes de ahora con lo que
   corregiste, y cuánto coincidiría el propuesto. Si no hay nada que ganar te lo
   dice también, con los números — «no hay nada que cambiar» sin cifras es
   indistinguible de «no lo he mirado». Si aceptas, el ajuste se aplica a
   **todas las piezas de aquí en adelante** y la corrección a mano se retira,
   para que lo que veas salga del ajuste y no de tu pincelada.

3. **Medir pieza** (botón de la barra): la respuesta a *¿cuánto mide esto?*, de
   una vez y sin rodeos. Mira el contorno de lo que tengas delante y te da:

   - **Qué figura es** — redonda, arandela, polígono de N lados, redondeado o de
     contorno libre — y por qué lo ha decidido, con su número.
   - **El contorno**: perímetro, área, largo y ancho reales (envolvente mínima
     girada, así que no cambia porque la pieza esté torcida), agujeros,
     circularidad y cuántos tramos rectos y arcos tiene.
   - **Las cotas que su forma tenga**: diámetro y redondez si es redonda, los dos
     diámetros si es una arandela, **cada lado y cada ángulo** si es un polígono,
     el radio de cada redondeo si las esquinas están matadas.

   No hace falta pieza registrada, ni plantilla, ni calibración: sin calibrar te
   da píxeles y te lo dice. Desde ahí puedes **copiarlo**, **exportarlo a CSV** o
   pulsar **Vigilar estas cotas** para convertirlas en herramientas de la pieza.
   Medir y vigilar son dos decisiones distintas, y por eso lo segundo no pasa
   solo cada vez que consultas.

   La ventana va en **dos pestañas**: *Medidas de la pieza*, que es lo de
   arriba y lo mide el programa solo, y **Mis herramientas**, que son las cotas
   que tú dibujaste — lo que dan ahora, si cumplen y **entre qué valores se
   admiten** cuando no. Ahí puedes desmarcar una para que deje de contar sin
   borrarla.

   Y cada herramienta **se abre en todo lo que su figura puede medir**: la misma
   región que dibujaste para el área te da también su perímetro, su solidez, su
   circularidad, su relación de aspecto y cuántos agujeros tiene, con el número
   ya calculado. Marca la que quieras y nace como cota nueva sobre esa misma
   figura, sin volver a dibujar nada. Nace **sin tolerancia**: decláresela tú,
   porque heredar la del área diría que un perímetro cumple una banda que no es
   la suya.

   El informe **no se corta**: una lista de propuestas que hay que revisar a mano
   sí se limita a doce, pero un informe cortado contesta a medias. Y nada sale
   marcado «OK», porque una cota recién medida está dentro de su propia
   tolerancia por construcción — todavía no la ha comprobado nadie.

   **Y una tercera pestaña: «Elegir qué se mide».** Las 32 herramientas, cada
   una con **su casilla**: marcadas son las que el programa usa al pulsar
   «Vigilar estas cotas». Antes se llevaba las doce propuestas y había que
   borrar después las que no querías.

   Al lado, **qué se puede hacer con ella sobre esta pieza** —*propuesta*, *ya la
   usas*, *no la ve aquí*, *a mano*— y **por qué**, en el tooltip de la propia
   celda. «No la ve aquí» no es que el programa falle: es que la pieza no tiene
   ese rasgo, y así lo dice.

   Las que no puede colocar no traen casilla —marcar algo que no haría nada es
   una promesa que no se cumple— sino un botón **Dibujarla**: cierra el informe
   y te deja esa herramienta elegida en la paleta. Colocar una Rectitud o un
   Chaflán exige señalar **qué tramo** se mide, y adivinarlo daría una cota
   sobre un sitio que nadie eligió.

4. **Medir automáticamente**: en el editor de plantilla, el botón
   **Medir automáticamente…** mira la pieza y propone las cotas que encuentra.
   No las inserta a lo loco: abre una lista con **la medida de cada una, su
   tolerancia sugerida y por qué se propone**, todas marcadas de entrada, y añade
   solo las que dejes marcadas — en un solo paso que Ctrl+Z deshace entero. Cada
   propuesta se ha medido de verdad antes de ofrecerse, así que lo que aparece
   en la lista ya funciona sobre esa pieza.

   **Primero mira qué FIGURA es tu pieza**, y de ahí sale qué te propone:

   Y esa lectura tiene que dar lo mismo en piezas iguales, o la bandeja entera se
   vuelve irrevisable. Sobre una foto de cien tuercas hexagonales del mismo lote,
   antes salían con 6, 7, 8, 9, 10 y 11 lados —once aciertos de cien— porque el
   borde que queda **en sombra** llega dentado dos o tres píxeles y eso basta,
   sobre una pieza pequeña, para que aparezcan vértices que no existen. Ahora
   salen **85 de 100** como hexágonos.


   | Si la pieza es… | Te propone |
   |---|---|
   | **Redonda** | el **diámetro** (con su perímetro en el porqué) y la **redondez** |
   | **Una arandela** | **Ø exterior**, **Ø interior** y la redondez |
   | **Una pieza redonda vista de refilón** | lo mismo: se reconoce como redonda aunque en la foto sea una elipse |
   | **Un polígono** | **cada lado** con su propia cota y **cada ángulo**, sacados de los mismos vértices con los que se reconoció la pieza |
   | **Un polígono redondeado** | cada tramo recto, el **radio de cada redondeo** y el largo/ancho |
   | Cualquier otra | **cada cara recta** que tenga, largo y ancho, un círculo por agujero, un espesor por cada par de caras paralelas y un ángulo por esquina |

   | **Una rueda dentada** | **cuántos dientes**, Ø de cabeza, Ø de raíz y la excentricidad |
   | **Una rosca de perfil** | el **paso**, el Ø exterior y el Ø de fondo |

   **Y te dice con cuánta seguridad reconoció la forma.** Cuando el recuento de
   lados no es firme, el propio nombre lo dice —«Polígono de 9 lados (recuento
   poco firme)»— y no solo la explicación de debajo, porque ese nombre es lo que
   sale al exportar y lo que te dice una receta cuando no va con la pieza. Debajo del título del
   informe va la frase que lo explica, y lleva dos números: cuánto se separa el
   contorno del polígono que se le ajustó, y **en cuántos barridos aguanta ese
   recuento de lados**. Un hexágono de verdad aguanta 17 de 30; unas arandelas
   que salieron «polígono de 9 lados» aguantan 2 de 30, que no es un recuento
   sino una casualidad que cupo en la tolerancia. Con un solo número los dos
   casos se leían igual.

   **Si la pieza se ve de refilón, te lo dice.** Una pieza redonda que no está
   justo debajo del objetivo se ve como una elipse, y entonces el diámetro que
   se publica sale corto —hasta un 13 % en las fotos de prueba— y la redondez
   mide la inclinación de la cámara en vez de la pieza. El informe lo avisa con
   la cifra: «*el contorno es una elipse de 1,25:1… el diámetro se queda un
   10 % por debajo del eje mayor*». El arreglo es físico: pon la pieza debajo
   del objetivo o calibra el plano con el tablero.

   **Si ninguna cota puede juzgar la pieza, te lo dice.** Hay dos clases de
   cota: las que vuelven a medir en cada inspección —un diámetro, la redondez,
   el paso de una rosca— y las que repiten el valor de hoy porque salen de la
   descomposición de *este* contorno. Si a una pieza solo le salen de las
   segundas, el informe lo avisa: guardarla como plantilla daría una pieza que
   no rechaza nada, y eso tienes que saberlo antes y no tres lotes después.

   **Una arandela conserva su agujero también al elegirla como pieza.** Si el
   taladro se come más de la mitad del disco —una arandela de pared fina, un
   anillo separador—, antes se perdía al seleccionarla: salía como disco macizo,
   sin Ø interior y con el doble de área. La misma pieza daba un número distinto
   según la miraras en el informe de la foto entera o de una en una.

   **Los agujeros que te propone son los de ESA pieza.** Si en el encuadre hay
   una bandeja entera —veinte tuercas, un puñado de arandelas—, cada informe
   habla solo de la pieza que estás midiendo: antes listaba también los agujeros
   de las vecinas, y en la foto de las tuercas eso eran ciento sesenta y nueve
   filas casi iguales para una pieza que tiene dos agujeros. Y si una cota de
   agujero sale midiendo tanto como la pieza entera, no se publica: un agujero
   está dentro, así que no puede medir más que ella.

   **Y a todas, el área y el perímetro de la silueta.** Son las dos únicas
   cotas que miran la pieza **entera**: todo lo demás mide un rasgo —este
   diámetro, aquel lado, esta esquina— y una pieza puede pasar las doce y estar
   mellada justo donde ninguna caía. Sobre una placa de 200×160, una mella de
   60×20 en mitad de un lado mueve el área un 3,8 % mientras el largo no se
   entera (0,0 %).

   Se proponen **las dos** porque no se estropean igual: un borde dentado alarga
   el perímetro un 82,5 % y apenas toca el área (4,6 %). Juntas cogen defectos
   que por separado se escapan.

   Y son **comprobación, no referencia**: se vuelven a medir en cada
   inspección, así que no llevan el aviso de «vale como cota de referencia».
   Antes había piezas —una varilla roscada, un cáncamo— a las que la lista
   entera salía con ese aviso: aceptabas las cinco propuestas y te quedabas con
   una plantilla que no podía rechazar nada.

   **Y puedes acotar qué te propone, con una RECETA.** Arriba del diálogo hay un
   desplegable —*Pieza redonda*, *Arandela*, *Cuadrada o rectangular*, *Tuerca
   hexagonal*, *Engranaje*— que deja fijado qué clases de cota entran, con una
   frase que dice qué trae cada una. Las casillas siguen debajo: la receta las
   pone, y tú puedes ajustarlas sin salir del diálogo.

   Sirve para lo que pediste el taller: quien mide engranajes no quiere que le
   ofrezcan lados —los «lados» de una rueda son sus dientes— y quien mide
   arandelas no quiere ángulos de esquinas que no existen. Con la receta puesta,
   el tope de doce propuestas se reparte **entre las cotas que sí quieres**.

   **Una receta acota, no fuerza.** Si eliges *Tuerca hexagonal* sobre una
   arandela, no te saca un entrecaras: te dice *«la receta Tuerca hexagonal es
   para una tuerca hexagonal, y esta pieza se ha reconocido como arandela»*. Un
   número que sale de la herramienta equivocada se acepta, se guarda con su
   tolerancia y luego no cuadra con el plano — y eso es peor que no tenerlo.

   **Y la receta se queda con la pieza.** La que elijas se guarda en la pieza
   registrada, así que la próxima vez el diálogo abre ya con ella y la lista
   propuesta según ella. Es lo que hace que sirva para un lote: con una bandeja
   de cien engranajes, elegir «Engranaje» cien veces es no elegirlo.

   Lo que se recuerda en la pieza es la **receta**, no las casillas sueltas: si
   marcas o desmarcas alguna sin guardar, ese ajuste dura lo que la sesión.
   Guardarlo dejaría el desplegable diciendo *Arandela* con otras clases
   marcadas, que se entiende peor que no recordarlo.

   **Y puedes guardar la tuya.** Elige la receta más parecida, ajusta las
   casillas y pulsa **Guardar como receta…**: le pones nombre —«bridas del
   proveedor B»— y aparece en la lista como una más, asignable a cualquier
   pieza. Hereda la familia de la que partiste, así que una receta hecha desde
   *Arandela* sigue negándose sobre una tuerca. No puedes ponerle el nombre de
   una de fábrica: la pieza recuerda su receta **por el nombre**, y con dos
   iguales no sabría a cuál te referías.

   La única excepción es el engranaje, y por una razón medida: para el
   clasificador una rueda dentada es *irregular* (le cuenta 111 lados rectos),
   así que ahí quien dice si es un engranaje es la herramienta consiguiendo
   medirlo. Sobre `engranaje-1.png` la receta saca **dientes, área, perímetro y
   el agujero**.

   **Rosca y engranaje son nuevos, y antes faltaban del todo.** La medición
   automática conocía siete de las treinta y dos herramientas, y ninguna de esas
   dos estaba: a un tornillo roscado te ofrecía nueve «Radio» y tres reglas, y a
   una rueda de veinte dientes, nueve «Lado» — que eran ocho de sus cuarenta
   flancos, elegidos por orden de lista. Ahora, cuando la pieza **se repite**
   —dientes alrededor del centro, filetes a lo largo del eje— te propone la
   herramienta que le toca y **deja de ofrecerte los tramos sueltos**, porque ya
   están medidos ahí dentro. En la varilla roscada de prueba, que lleva impreso
   «6 hilos por pulgada», el paso que mide sale con un 0,9 % de error.

   **Lo que cuesta, dicho claro:** en una pieza roscada se apagan *todas* las
   cotas de contorno, así que un tornillo de cabeza hexagonal se queda sin las
   cotas de su cabeza y hay que dibujarlas a mano. Se probó a apagar solo el
   tramo roscado y salió peor: ese tramo no delimita la rosca, y volvían nueve
   «Radio» que eran crestas de filete separadas exactamente un paso.

   **Una rueda con agujeros de aligeramiento** hay que medirla a mano: la
   propuesta automática busca los dientes en un anillo que en esas ruedas cae
   encima de los agujeros. La herramienta te lo dice y te pide ajustar los
   radios.

   **Dos ruedas que se solapan** (como en la foto de prueba con dos piñones) no
   se pueden contar: la detección las funde en una sola silueta, y separándolas
   el corte se lleva dientes por delante. La app se niega a dar un recuento, que
   es lo correcto — un diente de más o de menos ya es otra rueda.

   Una **tuerca hexagonal sigue recibiendo sus seis caras**: su radio se repite
   seis veces por vuelta igual que en una rueda, pero seis caras son *todas* las
   que tiene, mientras que ocho flancos de cuarenta son solo una muestra.

   Las caras rectas salen **para cualquier forma que no sea redonda**, no solo
   para los polígonos: una pieza de canto escalonado se quedaba antes sin una
   sola cota de sus caras. Una elipse sigue sin recibir lados, y eso también es
   correcto: no tiene ninguno.

   A una pieza redonda **ya no le propone el largo y el ancho**: en un disco esos
   dos números son el mismo que el diámetro, y una lista con la misma cota tres
   veces con tres nombres no se revisa. El **recuento de lados** y **cada lado**
   son cosas distintas y por eso van los dos: el recuento vigila que no aparezca
   ni falte una cara, y cada lado vigila su medida.

   Dos avisos honestos sobre los límites, que están medidos: la pieza tiene que
   **verse suficientemente grande** para contarle los lados —con 3 o 6 caras basta
   con que mida unos 70 px de ancho, pero con 10 o 12 hacen falta 200— y por
   debajo de eso la da por redonda y te mide el diámetro, que es lo útil a ese
   tamaño. Y aguanta bastante suciedad de imagen (ruido, desenfoque, poca luz,
   iluminación desigual), pero **poca luz *más* iluminación desigual** puede
   hacer que la detección de la pieza falle antes de llegar a medir: ahí lo que
   hay que arreglar es la luz.

   **Cuidado con dos de ellas, y la app te lo dice.** La *Regla* y el *Ángulo*
   miden entre puntos que tú fijas sobre la pieza, no buscan el borde. El número
   que ves al proponerlas es real —sale del contorno de esa pieza—, pero
   guardadas como herramienta **repiten ese mismo valor en cada inspección**: no
   pueden detectar que un lado cambió de largo. Por eso salen con «—» en la
   columna de estado en vez de un OK verde: dan una cota de referencia, no una
   comprobación. Lo que sí comprueba es todo lo que vuelve a mirar la imagen —
   diámetros, redondez, espesores, recuento de lados, blobs.

   La lista se corta en **doce**, porque cincuenta propuestas no se revisan. Lo
   que quede fuera **se te dice**: descartar cotas en silencio te dejaría
   creyendo que la pieza no tenía más. Y el corte se lleva lo más pequeño **de
   cada tipo**, no un tipo entero — antes, un hexágono perdía sus seis ángulos.

   **Medir pieza** (botón, *Medida ▸ Medir pieza*, o la tecla **M**) abre el
   informe de la pieza que tengas señalada: perímetro, área, agujeros,
   circularidad, los diámetros y todas tus cotas, con copiar y exportar. Tiene
   tecla desde que alguien pidió «una ventana que muestre el área y el
   perímetro» sin saber que ya estaba — que es la mejor prueba posible de que no
   se encontraba.

   **Sacar las medidas**: en el resultado de una inspección tienes **Copiar
   medidas** (texto alineado, para pegar en un correo o en un parte) y
   **Exportar CSV…** (columnas que una hoja de cálculo puede sumar y promediar).
   Cada fila lleva su valor, **su unidad**, los píxeles crudos por si luego
   recalibras, el estado y la tolerancia. Sin calibración da píxeles y lo dice.

   **Y te dice si esa medida depende de la luz.** El informe avisa cuando la
   cifra se movería al cambiar un poco la iluminación: «*moviendo el corte de
   gris 8 niveles a cada lado, esta pieza mide entre 160,9 y 176,3 px: oscila un
   9,2 %*». Ocho niveles es menos de lo que cambia una lámpara al calentarse.

   Hace falta porque el efecto es real y no se ve mirando. Medido sobre el banco
   de fotos, con el mismo barrido: `rosca-1` oscila **0,0 %**, `tornillo-1` un
   **0,1 %** — ahí el borde manda sobre la luz y la cifra es de fiar. Pero
   `tornillo-ojo-4` se mueve un **4,1 %** y `arandelas-1` un **9,2 %**: en esas
   escenas hay sombra pegada al borde o reflejo de frente, y la medida depende
   de la lámpara tanto como de la pieza.

   Ocho de once fotos se quedan por debajo del 0,5 % y no ven este aviso nunca —
   uno que saliera siempre se aprendería a ignorar. Y no es la incertidumbre
   expandida de la norma: le falta la escala, la repetibilidad del montaje y la
   del operador. Es una de sus componentes, la más barata de medir y justo la
   que se ve con los ojos.

   **El fichero sale en el formato que tu hoja de cálculo abre.** El separador
   de campos y el decimal salen de tu configuración regional de Windows, igual
   que hace Excel cuando guarda un CSV: en un equipo en español, `;` y coma
   decimal; en uno en inglés, `,` y punto. Y lleva marca de orden de bytes, sin
   la cual Excel se come todos los acentos y las unidades — «Perímetro» aparecía
   como «PerÃ­metro» y «mm²» como «mmÂ²».

   Los tres exportadores —medidas, contorno e informe del turno— usan el mismo
   criterio. Antes escribían coma y punto siempre, así que en un Windows español
   el fichero entero caía en la columna A, con los números leídos como fechas y
   los acentos rotos: tres motivos independientes, y con cualquiera de ellos el
   fichero ya no servía.

   **Ver contorno** (mismo panel) dibuja encima el contorno detectado con su
   descomposición: los tramos **rectos en azul**, los **arcos en naranja** con su
   radio escrito al lado y los **agujeros en magenta**, sobre el contorno crudo
   en blanco tenue para que se vea dónde el ajuste se despega del borde. Abajo a
   la izquierda, el resumen: perímetro, área, agujeros y envolvente. Es una capa
   de consulta: no estorba ni se puede arrastrar, y se apaga con el mismo botón.

   **Exportar contorno a CSV…** guarda los puntos del contorno y de cada agujero
   en un archivo para abrirlo en un CAD — en **mm si hay calibración**, en
   píxeles si no, y con la unidad escrita en la cabecera de cada columna para que
   no haya dudas tres días después.

5. **Dibujar sobre el video en vivo**: las herramientas viven en el panel
   **Herramientas**, a la derecha. Arriba, una franja con las cinco familias
   (Figuras básicas · Medición en línea · Construcciones · GD&T · Máx./mín. y
   torneadas); debajo, **todas** las de la familia abierta en rejilla, y al pie
   una línea que dice qué mide la que señalas con el ratón y con qué atajo se
   activa.

   Pulsar una familia **la abre para mirarla** y no cambia con qué estás
   dibujando; con el teclado, **Ctrl+1…5** elige familia y **1…9** la
   herramienta dentro — y ahí sí elige, porque quien pulsa un atajo quiere
   dibujar ya. La herramienta activa se ve marcada sin tener que pasar el ratón.

   El panel se puede **cerrar y mover** como el de comparación, y se recupera
   desde *Ver ▸ Panel de herramientas*. El editor de plantilla usa el mismo
   panel en su columna izquierda.

   Se dibuja arrastrando el mouse sobre el video — en tiempo real y anclado a
   la pieza: si la mueves o la giras, las herramientas la siguen. Al soltar, **la herramienta mide la pieza actual y
   se auto-sugiere sus tolerancias** (±10 % para distancias, conteo exacto
   para blobs). **Mover/Elegir** selecciona y arrastra; **Borrar
   herramienta** elimina la seleccionada.
   **Las cinco familias de un vistazo** (32 herramientas). El catálogo completo
   —qué mide cada una, cómo se traza y qué NO puede medir— está en
   [HERRAMIENTAS.md](HERRAMIENTAS.md).

   | Familia | Herramientas | Para qué |
   |---|---|---|
   | **Figuras básicas** (7) | Borde liso · Blob · Blob poligonal · Región · Simetría · Lados · Rebabas y mellas | La forma y lo que hay dentro de una zona |
   | **Medición en línea** (8) | Caliper · Círculo · Punto-Línea · Regla · Línea-Línea · Ángulo · Arco · Holgura | Las cotas de toda la vida: distancias, diámetros, radios y ángulos |
   | **Construcciones** (3) | Punto construido · Recta construida · Eje medio | No miden: **fabrican** referencias para que otras midan contra ellas |
   | **GD&T** (7) | Posición · Rectitud · Redondez · Orientación · Desviación de centros · Patrón de agujeros · Perfil de línea | Tolerancias geométricas contra un marco de referencia |
   | **Máx./mín. y torneadas** (7) | Eje / Diámetro · Rosca · Engranaje · Máx./mín. · Chaflán · Radio de acuerdo · Ranura | Piezas de torno y medidas que no dependen de acertar la dirección del trazo |

   Los detalles de todas las herramientas incluyen **mm (y cm a partir de
   10 cm)** cuando hay calibración — también el círculo (diámetro, radio y
   redondez) y el área de los blobs (mm²). El Caliper empareja **bordes de
   polaridad opuesta** (mide anchos reales, no dos bordes del mismo lado), y
   la banda de muestreo del Caliper/Borde liso **se dibuja en pantalla** al
   cambiar el campo Puntos. Bajo "Pieza actual" hay botones **⟲/⟳ 90°** para
   girar cómo se ve la pieza (persiste con la pieza seleccionada). El **panel de
   comparación** es un **dock reubicable**: arrástralo, déjalo flotante o
   ciérralo (se reabre desde **Ver ▸ Panel de comparación**); su disposición se
   guarda entre sesiones.

   La **cantidad de puntos de muestreo** de cada herramienta es editable en
   **Plantilla…** (campo "Puntos"): banda del Caliper, rayos del Círculo,
   escaneos del Borde liso y área mínima del Blob.

   **Gestor de plantillas** (botón **Gestionar…** junto al selector de
   plantilla): una pieza puede tener varias plantillas (p. ej. una por cara);
   desde aquí puedes **crear, renombrar, duplicar y eliminar** plantillas y
   elegir cuál activar. Duplicar copia todas las herramientas de la plantilla a
   un nombre nuevo — útil para partir de una parecida y ajustarla.
   También puedes **exportar** una plantilla a un archivo `.json` e **importarla**
   en otra pieza (o en otra PC de la línea): se copian todas las herramientas con
   sus tolerancias, sin tener que redibujarlas.

   **Zoom con la rueda**: gira la rueda sobre el vídeo o la imagen para acercarte
   hasta 20×; el zoom va **hacia el cursor**, así que el detalle que señalas se
   queda quieto mientras te acercas. Al volver al ajuste (1×) la vista se
   recentra sola. Sirve tanto en la vista en vivo como en el editor, y las
   herramientas siguen ancladas a la pieza con cualquier zoom. Para moverte por
   la imagen ampliada, arrastra con el **botón central** o con **Ctrl + botón
   izquierdo** (cursor de mano): el dibujo y el marco de selección siguen
   funcionando igual, y la imagen nunca se pierde de vista.

   **Barra de zoom** (barra inferior de la ventana principal y bajo el lienzo del
   editor), por si prefieres no usar la rueda: `⤢` mínimo (ajustar a la ventana),
   `−` alejar, el **porcentaje actual**, `+` acercar y `⛶` máximo. Los botones se
   apagan al llegar a cada tope. Atajos equivalentes: **Ctrl++** / **Ctrl+-**
   acercar/alejar, **Ctrl+0** ajustar a la ventana, **Ctrl+Alt+1** ver al 100 %
   (píxeles reales), **Ctrl+Alt+2** zoom máximo y **doble clic** para volver al
   ajuste. El porcentaje es la escala real de pantalla, así que "100 %" significa
   un píxel de imagen por píxel de pantalla. Al cambiar de pieza el encuadre
   vuelve al ajuste.

   **Tablero de referencia (centro = 0)**: en **Ver ▸ Tablero de referencia** se
   dibujan ejes y grilla con el **cero en el origen que elijas**, para medir la
   *posición* de la pieza (cuánto se desvía y cuánto gira) y no solo distancias
   sueltas. +X a la derecha y **+Y hacia arriba**, como en metrología. En **Ver ▸
   Origen del tablero** eliges cómo se centra:
   - **Automático — centro del contorno** (recomendado): el cero cae en el
     centro geométrico de la pieza, que es el que se ve centrado.
   - **Automático — centro de masa**: el centroide del contorno. En piezas
     asimétricas (una L, por ejemplo) queda visiblemente desplazado del centro
     que uno ve; se mantiene por si te interesa esa referencia.
   - **Automático — centro de la imagen**: el cero queda fijo en pantalla, útil
     para centrar la pieza en un soporte.
   - **Manual — punto fijado a mano**: lo marcas con un clic sobre la imagen.

   Además, el diálogo de modo de medición trae un **ajuste fino** en X/Y para
   correr el cero a mano sobre cualquiera de esas opciones, y una casilla para
   que los **ejes giren con la pieza** o queden alineados con la imagen.
   El paso de la grilla se adapta al zoom y las etiquetas salen en la unidad
   activa (mm/cm si hay escala calibrada, px si no). Todo queda guardado.
   **Regla graduada** (**Ver ▸ Regla graduada**): reglas en los bordes superior
   e izquierdo con marcas y números en la unidad activa, una **barra de escala**
   ("20 mm") abajo y la marca de la posición del cursor sobre ambas reglas. Con
   el tablero encendido las reglas miden desde su cero; con el tablero apagado,
   desde la esquina de la imagen. Sirve para leer un tamaño de un vistazo sin
   tener que dibujar una herramienta.

   Con el tablero encendido aparece además una **lectura continua** sobre el
   vídeo: `dx`, `dy`, radio y giro de la pieza respecto al cero. Si el cero está
   en la propia pieza, la desviación es cero por definición y solo se muestra el
   giro. Y al pasar el ratón por encima, un recuadro muestra las **coordenadas
   del punto bajo el cursor** en ese mismo sistema centrado.

   **Modos de medición (por pieza)**: cada pieza se mide en uno de dos modos,
   que eliges al **registrarla** y puedes cambiar luego en **Pieza ▸ Modo de
   medición…**:
   - **Posición real (personalizada)**: lo de siempre — mides donde quieras y
     cada herramienta se juzga con sus tolerancias.
   - **Especial (tablero centrado)**: además, la pieza se mide respecto al
     tablero (centro = 0). Al seleccionar una pieza en este modo, **su** tablero
     se aplica y se enciende solo; al volver a una pieza en modo real, se apaga.
     En este modo puedes fijar **reglas de posición** que entran en el veredicto
     OK/NG: **desviación máxima** del centro respecto al cero y **giro máximo**
     (0 = no vigilar). La lectura en vivo muestra esos límites y se pone en rojo
     cuando la pieza está fuera, así la colocas bien antes de inspeccionar. Si
     la pieza es casi simétrica su eje no es fiable, así que la regla de giro se
     salta sola y lo avisa, en vez de dar NG falsos.
   El modo y el tablero (origen, punto fijado y si los ejes giran) se guardan
   **con la pieza**, así que cada una recuerda cómo se mide. **Junto al combo de
   pieza hay una etiqueta con el modo activo** (gris = posición real, cian =
   especial), para no dudar nunca en cuál estás.

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

   **Escala por marcador ArUco en vivo** (**Configurar ▸ Escala por marcador ArUco (en vivo)**): imprime el marcador `sample_images/aruco_4x4_id0.png`, mide su
   lado real con una regla y escríbelo al activar la opción. Colócalo junto a
   la pieza (en el mismo plano): la escala px→mm se recalcula **en cada frame
   con la homografía del marcador** y se ajusta sola si acercas o alejas la
   cámara — no hay que recalibrar a mano. La barra de estado muestra la escala
   viva y un **indicador de calidad** (buena / regular / pobre, en %): como el
   marcador es un cuadrado conocido, la uniformidad de sus lados y diagonales en
   píxeles delata cuán perpendicular está la cámara al plano — si sale
   «regular/pobre», endereza la cámara para que la escala sea fiable lejos del
   marcador. Con el marcador activo, las herramientas de **longitud** (Caliper,
   Regla, Punto-Línea) calculan los mm **por homografía punto a punto** en vez de
   multiplicar píxeles por una escala constante: así corrigen la perspectiva y
   siguen siendo precisas lejos del marcador (el valor con el que se comparan las
   tolerancias sigue en px). (Puntos a distinta altura/profundidad respecto al
   plano necesitan una cámara de profundidad; con una sola cámara 2D no es
   recuperable.)

   **Calibración fácil desde una herramienta** (lo más rápido): traza una
   Regla (o Caliper/Círculo) sobre algo de tamaño conocido — una regla, una
   moneda —, selecciónala y pulsa **"Fijar escala con esta medida…"**;
   escribes cuánto mide de verdad en mm y la escala px→mm sale de ahí. Todas
   las cotas quedan en unidades reales al instante. (Sin calibrar hay que
   medir sobre algo conocido: no existe escala automática pura desde una sola
   cámara.)

   **Preferencias** (pestaña de *Configurar*): centraliza ajustes antes
   fijos en código — el **intervalo de auto-inspección** (ms) y la
   **sensibilidad de anomalía (kσ)** de apariencia. Se aplican al aceptar y
   quedan guardados.

   **Medir una vez por pieza que pasa** (misma pestaña): para vídeo y cámara en
   marcha. Con el temporizador solo, sobre una cinta pasan tres cosas y ninguna
   avisa: se mide **media pieza** mientras entra, se mide **la misma pieza doce
   veces** mientras cruza —y cada una cuenta como una inspección distinta en el
   historial—, y una pieza rápida puede pasar **entre dos disparos** sin medirse
   nunca.

   Con esto encendido se mide cuando la escena está **quieta** el tiempo que
   pongas (*asentamiento*) y **ninguna pieza toca el borde**, y no se vuelve a
   medir hasta que el encuadre se **vacía** el tiempo que pongas (*rearme*) —
   que es el «tiempo de espera entre que sale y entra una pieza».

   La cuenta de quietud **vuelve a empezar** con cada movimiento en vez de sumar
   trozos: sumando, una cinta que avanza a tirones dispararía entre dos tirones.
   Y si no dispara, la barra de estado dice **por qué** —la escena se mueve, hay
   una pieza en el borde, o ya se midió y falta que el encuadre se vacíe—,
   porque son causas que llevan a hacer cosas distintas.

   Viene **apagado**: encenderlo cambia cuándo se mide, y quien ya tenía su
   auto-inspección ajustada no tiene por qué encontrarse con otro
   comportamiento.

   **Calibrar la lente** (menú *Configurar ▸ Calibrar la lente…*): imprime un
   tablero de ajedrez, pégalo a algo rígido y enséñaselo a la cámara desde
   varios sitios. Toda lente curva las rectas, y con una lente corriente **la
   misma pieza mide hasta un 18,5 % menos en una esquina que en el centro** —sin
   que nada en pantalla diga cuál de las dos medidas es la buena.

   Lo único que hay que hacer bien es **llevar el tablero a las esquinas del
   encuadre**, no solo al centro. La ventana lleva una rejilla de nueve zonas que
   se va poniendo verde, y no deja calibrar hasta que las cuatro esquinas estén
   cubiertas. No es rigor de más: una calibración hecha solo por el centro parece
   perfecta —el programa la puntúa igual de bien— y deja el borde un 35 %
   desviado, que es peor que no corregir nada.

   Una vez calibrada, se enciende y se apaga en *Configurar ▸ Corregir la distorsión
   de la lente*. **Encenderla cambia todas las medidas**, que es justo lo que se
   pretende: si ya tienes piezas registradas, vuelve a mirar sus tolerancias.

   **Calibración a milímetros** (menú *Configurar ▸ Calibrar escala (mm)…*): dos
   métodos —
   **A)** haz dos clics sobre una distancia real conocida (una regla, el
   diámetro de una moneda) y escribe cuánto mide **en mm, cm, m o pulgadas**
   —la unidad se elige al lado del campo, para que nadie tenga que convertir
   de cabeza: un error de conversión ahí no sale como un error, sale como cotas
   bien formateadas y todas mal—. La escala se calcula y además se
   **estima la distancia de la cámara a la superficie**; **B)** escribe la
   distancia cámara→superficie y el FOV horizontal de tu cámara (webcams:
   55–70°) y la escala sale del modelo pinhole. Con la escala calibrada,
   todas las medidas se muestran en mm además de px (al dibujar, en Probar y
   en los reportes de inspección). La escala queda guardada y vale mientras
   la cámara no cambie de altura; las tolerancias internas siguen en px.
   **La longitud y su unidad se recuerdan** de una calibración a la siguiente:
   es lo único que hay que teclear cada vez, y era lo único que volvía al
   valor de fábrica mientras la distancia de cámara y el FOV sí se recuperaban.
   Si el puesto tiene una regla fija, se escribe una sola vez.

   Y la ventana de calibrar **habla en la unidad que tengas elegida**: si
   trabajas en centímetros, te enseña la escala en cm/px y la distancia de
   cámara en cm. Por dentro la escala sigue siendo mm/px —eso no cambia— pero
   enseñarla en milímetros a quien ha pedido centímetros le obliga a convertir
   de cabeza justo en la pantalla donde una conversión mal hecha estropea
   **todas** las cotas de la pieza.
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
   persisten y aplican al video en vivo, al registro y a la inspección.

   **En el vídeo, cada cota dice «OK» o «NG» con palabras**, no solo con el
   color de la letra. Antes el veredicto iba únicamente en verde o rojo: quien
   no distingue esos dos colores no lo veía, y en un parte impreso en blanco y
   negro desaparecía. Además la caja de fondo tapa más, así que la lectura se ve
   igual sobre una pieza clara que sobre una oscura.

   **Supr, con el ratón sobre la tira de fotos, quita esa foto** — y antes
   borraba una cota de la plantilla, que no es lo que decía la ayuda.

   **Clic derecho sobre el vídeo.** Antes borraba la cota al instante, sin
   preguntar. Ahora abre un menú con lo que aplica en ese punto: sobre una cota,
   *renombrar*, *duplicar*, *copiar lo que mide* y —abajo del todo y separado—
   *borrar*; sobre el vídeo vacío, *marcar aquí el rasgo distintivo*, *ajustar a
   la ventana* y *píxeles reales*. Lo del rasgo se ahorra el paso de antes:
   pulsabas un botón, el programa se quedaba esperando y tenías que acertar con
   el clic siguiente; ahora ya has señalado dónde lo quieres.

   **Con el pincel puesto, Ctrl+rueda acerca.** La rueda sola sigue cambiando
   el grosor del pincel, que es lo que se ajusta a cada momento mientras
   corriges; pero antes esa era la ÚNICA cosa que hacía la rueda, así que para
   acercarte a perfilar un borde tenías que apagar el pincel, hacer zoom y
   volver a encenderlo. Ctrl+rueda acerca sin apagar nada, y es el mismo gesto
   que en Krita, GIMP o Photoshop.

   **Recuperar lo que el brillo se lleva** (pestaña *Detección*). Si tus piezas
   son metálicas y salen **mordidas o partidas en trozos**, casi siempre es el
   reflejo: sube hasta el nivel del fondo y el corte de gris lo deja fuera. Con
   esta casilla el programa corta dos veces —lo seguro primero, y luego crece
   hacia lo dudoso solo donde toca lo seguro—, así que devuelve el brillo de la
   pieza sin dejar entrar la mesa. Medido sobre fotos reales: tres tornillos
   cincados pasan de salir como **5 manchas a salir como 3**, y un tornillo
   galvanizado de **2 a 1**; la bandeja de cien tuercas se queda en 100. Nace
   apagada porque cambia lo que se mide.

   **Clave de color de fondo** (pestaña *Detección*). Si tu mesa **tiene color**
   —cartón rojo, tapete azul, una bandeja verde— y ves que solo te detecta las
   piezas cromadas, es esto: hasta ahora el programa tiraba el color de la foto
   antes de separar nada, y trabajaba solo con lo claro y lo oscuro. Sobre un
   cartón rojo, una arandela de latón tiene casi la **misma claridad** que el
   fondo (ese rojo cae en gris 116, un gris medio) y lo único que las distingue
   es el tono.

   Con la clave encendida, cada píxel se mide por **lo distinto que es su color**
   del color del fondo. Medido sobre una foto de una veintena de arandelas de
   acero, latón, cobre, caucho, fibra y plástico sobre cartón rojo:

   | | piezas encontradas | pieza del cuadro |
   |---|---|---|
   | por claridad | 7 | 11 % |
   | por color | **20** | **23 %** |

   Las trece que aparecen son exactamente las que no son cromadas.

   **Y no hace falta que sepas que existe: la aplicación te lo dice.** Si mide
   que tu mesa tiene color, la pestaña *Detección* lo avisa con la cifra dentro
   —«tu mesa tiene color (#EE3F4D, saturación 0,74)»— y pone un botón que lo
   enciende. Sobre mesa blanca **se calla**, y con la clave ya encendida
   también: un aviso que sale en todas las escenas se aprende a ignorar, y
   entonces tampoco sirve donde hacía falta.

   El color del fondo puede **buscarlo solo** (toma la mediana del marco de la
   imagen, que es fondo casi siempre — aguanta aunque haya piezas tocando el
   borde) o puedes **señalarlo tú en la imagen**, que es lo sensato si el puesto
   siempre tiene la misma mesa.

   **Señalar el fondo.** Con «Color del fondo (manual)» el botón de debajo abre la
   imagen: arrastras un recuadro sobre un trozo de mesa **vacío** y de ahí sale
   el color. Antes esto abría la rueda de colores de Windows, y ahí hay que
   *adivinar* el rojo del propio cartón — que no se sabe de memoria. El color
   está delante, en la foto.

   Sobre la foto del cartón rojo, señalar la mesa da **12 piezas y el 23,9 %**
   del cuadro, frente a **11 y el 22,9 %** con la mediana del marco.

   La ventana enseña, con cada recuadro, **qué piezas saldrían** — corriendo la
   segmentación de verdad, no una aproximación bonita. Hace falta: si el
   recuadro cae encima de una arandela en vez de sobre la mesa, la escena sale
   **del revés** —cero piezas y el 87,8 % del cuadro marcado— y no da ningún
   error, solo detecciones peores durante meses. Por eso además avisa antes de
   aceptar, con la cifra: una mesa vacía **varía menos de 25** sobre sí misma; el
   cartón rojo real da 4, y un recuadro que ha cogido pieza da 75.

   Y hay escenas donde **no hay mesa que señalar**: en la bandeja de cien
   tuercas las piezas llegan a los cuatro bordes, y el recuadro más limpio de
   toda la imagen da 143. Ahí lo dice en vez de dejarte señalar tuercas creyendo
   que señalas mesa.

   Si no hay imagen —cámara aún sin llegar, puesto que se configura desde otro
   PC— el botón cae a la rueda de colores de siempre.

   **Sobre fondo blanco no cambia nada** y eso está comprobado: el engranaje, el
   cáncamo y la bandeja de cien tuercas dan las mismas piezas por los dos
   caminos. Nace apagada porque cambia lo que se mide.

   Un aviso honesto: una arandela de **plástico traslúcido** sigue sin
   detectarse bien, porque a través de ella se ve el fondo — ahí no hay color que
   la separe.

   **Piezas brillantes: reflejos y fondo.** Si tus piezas son metálicas y salen
   partidas en trozos o medidas cortas, el problema suele ser el corte de gris:
   pasa por dentro de la pieza y se deja fuera material. La pestaña *Detección*
   lo mira sola y, cuando pasa, te lo dice **con la cifra** —«aflojar el corte
   cambia la silueta un 36,8 %»— y te ofrece un botón para **separar por el
   canto** en vez de por el nivel de gris. El canto no mira el brillo, así que
   una pieza cincada sale entera.

   No sale siempre a propósito: **el canto no es mejor, es para otra escena**.
   Sobre una bandeja de cien tuercas el nivel las cuenta bien y el canto funde
   diez, así que ahí no te lo ofrece. Antes ese aviso no aparecía nunca sobre
   fondo blanco por un fallo de la comprobación.

   Probado sobre trece fotografías reales, tuyas y de un corpus de piezas
   metálicas que se descarga con `python3 testdata/fetch_real_images.py`. Dos
   límites que conviene conocer: si tienes **muchas piezas pegadas** —diez
   tornillos y tuercas amontonados— no lo resuelve ninguno de los dos métodos, y
   para eso está *Separar las piezas que se tocan*. Y si el **fondo es
   texturizado** con sombras largas, el canto puede no cerrar ningún contorno:
   cuando pasa te lo dice y te manda al otro método, no te deja una imagen vacía
   sin explicación.

   Las
   herramientas de dibujo usan **iconos**, y debajo de la rejilla se lee **la
   explicación completa** de la que señalas: qué mide y cómo se traza, entera y
   sin cortes. Antes se veían tres renglones y el resto solo aparecía al dejar el
   ratón quieto encima — medido, 29 de las 32 explicaciones no cabían. Ahora el
   bloque se queda con el hueco que sobra en el panel y, si el panel es bajo, el
   texto se desplaza en vez de recortarse. La rejilla no se mueve al pasar el
   ratón, que es lo que el alto fijo protegía.

   **Los menús están ordenados por lo que contestan**, no por dónde vive el
   código: **Archivo** (exportar, importar, restablecer), **Fuente** (cámaras y
   *Configurar…*), **Medida** (calibrar la escala, marcador ArUco, unidad, medir
   la pieza y su modo), **Pieza** (registrar, gestionar, plantillas),
   **Inspección** (inspeccionar, auto-inspección, editor, historial), **Ver**
   (capas: contorno, tablero, regla) y **Ayuda**.

   Antes, para preparar una medición en milímetros había que pasar por dos menús
   que no hablan de medir: *Calibrar escala* estaba en **Fuente** junto a
   «Buscar cámaras», y *Unidad de medida* en **Ver** junto a «Mostrar contorno».
   Y todo lo que se puede hacer desde la barra tiene ahora su entrada de menú:
   una acción que solo vive en un botón no la encuentra quien va con el teclado.

   **Restablecer una pestaña** (panel *Configurar*, botón **Restablecer**):
   devuelve solo la pestaña que estás viendo a sus valores de fábrica. Quien
   viene a desenredar el umbral no quiere perder la calibración de la máquina.
   El umbral vuelve a **automático**, no a un número: son dos cosas distintas.

   **Restablecer de fábrica** (menú **Archivo** ▸ *Restablecer configuración de
   fábrica…*): devuelve la máquina al estado de recién instalada. Te dice antes
   **qué se lleva** —calibración, ajustes y perfiles de detección, zona de
   trabajo, preferencias, atajos, controles de cámara, capas de la vista y
   tamaños de ventana— y **qué no toca**: tus piezas registradas, sus plantillas
   y el historial siguen ahí. No se puede deshacer, así que si quieres conservar
   la puesta a punto, exporta antes.

   Los atajos y la disposición de las ventanas se aplican al volver a abrir el
   programa, y la app te lo dice en vez de dejarte pensando que no funcionó.

   **Clonar la configuración a otra PC** (menú **Archivo**): *Exportar
   configuración…* vuelca a un `.json` la calibración, los ajustes y perfiles de
   detección, los atajos y las preferencias; *Importar configuración…* los
   aplica en otra máquina. No incluye piezas ni plantillas (esas se comparten
   con el export de plantillas). Ojo: la **calibración de escala depende de la
   cámara y la resolución**, así que en la otra PC la app te avisará si ya no es
   válida.

   **Perfiles de detección**: en *Detección…* puedes guardar el juego de
   ajustes con un nombre ("luz brillante", "contraluz", "pieza negra") y
   reutilizarlo. El perfil elegido **se guarda con la pieza seleccionada**, así
   que al cambiar de pieza se aplican sus ajustes de detección solos. Si borras
   un perfil, las piezas que lo usaban vuelven a los ajustes globales.

   **Atajos de teclado** (*Ayuda ▸ Atajos de teclado…* o tecla `F1`): guía completa y
   **editable** — haz clic en el atajo y pulsa la combinación nueva; se
   guardan en la BD. Por defecto: `Ctrl+Z`/`Ctrl+Y` deshacer/rehacer las
   herramientas dibujadas (crear, mover, borrar — también dentro del editor),
   `Supr` borrar la seleccionada, `Esc` volver a Mover/Elegir, `1`–`9` y `0`
   elegir herramienta de dibujo (`0` = Posición), `V` iniciar/detener cámara,
   `R` registrar y activar, `A` auto-inspección, `I` inspeccionar, `P`
   plantilla, `C` calibrar, `D` rasgo distintivo, `Ctrl+S` guardar la
   plantilla. Vista: `Ctrl++`/`Ctrl+-` acercar/alejar, `Ctrl+0` ajustar,
   `Ctrl+Alt+1` 100 % y `Ctrl+Alt+2` zoom máximo.

   **Rasgo distintivo** (piezas simétricas o para robustez extra): con el
   botón *Rasgo distintivo*, haz clic sobre un punto visualmente único de la
   pieza (un agujero, una marca, una esquina oscura — rombo magenta). Ese
   rasgo fija la orientación del fixture: la pieza se detecta igual **en
   cualquier rotación, incluso girada 180°**, cosa que los momentos por sí
   solos no distinguen en piezas simétricas. Se guarda con la pieza y aplica
   al registro, al video en vivo y a la inspección.
6. **Registrar y activar**: un solo botón — pide el nombre **validando
   duplicados al instante** (si la pieza ya existe ofrece guardar como nueva
   versión de su referencia o renombrar), pregunta el **modo de medición** de
   la pieza (y lo aplica ya, para que captures viendo el tablero con el que se
   va a medir), captura automáticamente 30
   referencias válidas del video (cada frame se valida: nitidez, exposición,
   pieza completa; los rechazos muestran el motivo en el progreso), guarda la
   referencia de embeddings, la miniatura **y las herramientas dibujadas**, y
   arranca la auto-inspección. El panel derecho muestra desde entonces la
   **comparación en vivo: pieza registrada vs pieza actual** (recortes
   normalizados) con la similitud y su umbral durante la auto-inspección.

   **Sin el modelo ONNX** (no descargado, o si prefieres usar la app como
   medidor puro) también puedes registrar: se avisa una vez y la pieza queda
   registrada **solo con herramientas** — se mide con las que dibujes, pero no
   hay comparación de apariencia que detecte defectos inesperados.
   **La referencia que falta se te dice al dibujar.** Cinco herramientas no
   miden nada sin una referencia declarada —Orientación y Desviación de centros
   necesitan un datum, el Punto y la Recta construidos necesitan de qué
   construirse, el Patrón de agujeros necesita los agujeros—. Antes te enterabas
   al medir. Ahora, al soltarla: si **sólo hay una** referencia posible se pone
   sola y se te dice cuál; si hay **varias** se te nombran para que elijas; y si
   **no hay ninguna** se te dice qué hace falta dibujar primero.

   Con varias no se elige por ti a propósito: medir contra el datum equivocado
   da un número que parece correcto.

   **Medidas en vivo, en una tabla** (*Ver ▸ Medidas en vivo (tabla)*): panel
   a la **izquierda** —el lado derecho ya tiene la paleta, la comparación y el
   mosaico— con lo que mide **cada herramienta de cada pieza**, su banda y su
   estado.

   Cada fila trae, además del número:

   - un **ojo** para dejar de dibujar esa cota encima de la pieza. No deja de
     medirse: sigue en la tabla con su veredicto y sigue contando para el OK/NG.
     Es para no tapar la imagen cuando hay catorce;
   - una **papelera** para quitarla, con el mismo Ctrl+Z que cualquier otro
     borrado;
   - y **pulsar la fila remarca esa cota** sobre la imagen, que es lo único que
     permite saber cuál es cuál cuando hay muchas encima.

   Arriba, un **selector de pieza** cuando hay varias en el encuadre. Es la misma
   elección que hacen las flechas y el mosaico —no un estado aparte que pueda
   discrepar—, y arranca en *Todas*.

   **El estado dice por cuánto**, no sólo «OK»: *Cumple, margen 0,4 px* o *No
   cumple, se pasa 1,2 px*. Un OK a secas no distingue una pieza sobrada de una
   que se va a salir en la siguiente. Sobre el vídeo los números se pintan encima de cada herramienta, y eso
   funciona con tres cotas: con catorce se pisan, y con varias piezas sólo caben
   los de **una** —catorce por seis serían ochenta y cuatro etiquetas—. En la
   tabla se leen todos.

   Y la herramienta que **no llega a medir** enseña ahí su motivo, en la celda
   del valor: «Se necesitan 2 bordes y se detectaron 0», «falta el datum». Es lo
   que responde a «esta herramienta no muestra medida» sin tener que abrirlas
   una por una. El resumen de abajo cuenta aparte las que no cumplen y las que
   no miden, porque son dos averías distintas: una es de la pieza, la otra del
   trazo o de la referencia que falta.

   Arranca cerrado y sólo se rellena mientras está abierto.

7. **Auto-inspección**: el botón queda activo y la app inspecciona el video
   continuamente (~1/s): banner **OK/NG** en vivo, resultados por herramienta
   dibujados sobre el video y estadísticas del día en la barra de estado.
   Todo queda en el historial. Se puede prender/apagar cuando quieras con la
   pieza seleccionada. **Inspección ▸ Ver historial…** abre la tabla de
   inspecciones recientes (fecha, veredicto, similitud, versión de referencia)
   por pieza, con la cantidad a mostrar ajustable, más un **gráfico de tendencia
   OK/NG por día** (barras dibujadas con QPainter, últimos 30 días).

   **Exportar el informe del turno.** El botón de exportar no saca la lista que
   se ve en pantalla, sino un informe: un turno son cientos de inspecciones, y
   una hoja con cuatrocientas filas contesta «qué pasó exactamente a las 14:32»
   —que casi nunca se pregunta— y esconde las tres que sí:

   - **¿Cuántas van?** Total, correctas, rechazadas y rendimiento.
   - **¿Qué está fallando?** Los motivos contados y ordenados: *«3 × diámetro
     exterior»*. Es lo que convierte 47 rechazos en algo que se puede ir a
     mirar. El motivo es el **nombre de la herramienta** que falló, no su
     detalle: si llevara la medida dentro, cada rechazo sería un motivo distinto
     y no se agruparía ninguno.
   - **¿Desde cuándo?** Un desglose por horas — *09h: 0 de 12; 10h: 1 de 11;
     11h: 10 de 11* — que enseña de un vistazo cuándo empezó a torcerse. Una
     lista ordenada por fecha no lo contesta: hay que leerla entera llevando la
     cuenta a mano.

   El resumen va **arriba** del fichero y las filas enteras debajo, para quien
   quiera cruzarlas con otra cosa. Al terminar se enseña el mismo resumen en
   pantalla, en texto corto para pegar en un parte o un correo.
8. **Guardar plantilla (Ctrl+S)**: las herramientas que dibujas o ajustas en
   vivo se guardan en la plantilla activa de la pieza **sin tener que volver a
   registrarla** — pulsa **Guardar plantilla** o `Ctrl+S`. Antes solo se
   persistían al registrar; ahora puedes iterar sobre una pieza ya registrada y
   conservar los cambios (si no hay pieza seleccionada, te ofrece crear una).
   Si hay **cambios sin guardar** y cambias de pieza/plantilla o cierras la app,
   te pregunta **Guardar / Descartar / Cancelar** en vez de perderlos en
   silencio.
9. **Afinar y aprender**: **Plantilla…** abre el editor para ajustar
   tolerancias con "Probar sobre esta imagen" (los valores medidos te dicen qué
   rangos poner). Con la cámara en marcha, al abrirlo eliges la fuente —
   **frame actual** o **abrir archivo** — y dentro tienes **Actualizar desde
   cámara** para recapturar una imagen fresca sin cerrar (la imagen solo cambia
   cuando tú lo pides, así las tolerancias no bailan mientras ajustas). El
   editor arranca con **las mismas herramientas que tienes en vivo** (incluidas
   las que aún no guardaste) y al cerrarlo tus ediciones vuelven a la vista en
   vivo — editor y tiempo real muestran siempre lo mismo. **Inspeccionar** hace
   una inspección
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
.\build\release\pc_inspector.exe --smoke
```

La última línea es el **arranque en seco**: construye la ventana, deja correr el
bucle de eventos —ahí es donde se enumeran las cámaras, que es lo que ya ha
matado el proceso alguna vez por un driver de captura roto— y sale sola con un
código. Sin ella, un banco entero en verde no dice nada sobre si la aplicación
llega a abrirse.

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
recorte canónico 256×256 sin fondo, listo para embeddings. En la UI,
**Ver ▸ Mostrar contorno** superpone contorno/centroide/eje en vivo (máximo
un análisis en vuelo; nunca bloquea la UI). El fixture pasa además por un
**estabilizador temporal** (banda muerta, suavizado y anti-giro de 180°) para
que las herramientas no tiemblen ni giren solas.

Limitaciones conocidas:

- **Requiere fondo contrastante y uniforme** (Otsu global); iluminación muy
  irregular degradará la segmentación.
- **Orientación inestable en piezas casi circulares o con simetría de
  rotación** — inherente al método de momentos; irrelevante para la
  comparación por embeddings de la fase 3. En el modo Especial, la regla de
  giro **se salta sola** cuando el eje no es fiable (y lo avisa), para no dar
  NG falsos; para fijar la orientación de una pieza simétrica, usa el rasgo
  distintivo.
- **La pieza debe estar completa dentro del encuadre**; si toca el borde el
  recorte normalizado puede recortarse.
- El overlay se verificó con imágenes sintéticas; con cámara real
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
sin excepciones cruzando la frontera). **Esquema en la v8** (10 tablas), con
migraciones versionadas por `PRAGMA user_version` —una base de datos vieja se
actualiza sola al abrirla—, foreign keys, modo WAL y `busy_timeout`. Las referencias de embeddings (`Embeddings`) se **versionan
por pieza y nunca se borran**: el aprendizaje incremental inserta una versión
nueva. `repositories/` es el puente domain↔database: `PieceRepository`
(roundtrip exacto de `ml::Reference` como BLOB float32) y
`SettingsRepository` (la cámara elegida se guarda y restaura al abrir).

**Vuelves a donde lo dejaste.** Se recuerdan entre sesiones el tamaño, la
posición, el monitor y si estaba maximizada la ventana; la disposición de
paneles; la pieza y la plantilla con las que trabajabas; el tipo de fuente; las
capas de la vista (incluido *Mostrar contorno*); el desglose de tiempos por
etapa, y el tamaño de cada diálogo por separado.

Dos cosas a propósito: **la fuente se recuerda pero no se abre sola** —igual que
la cámara, que se preselecciona sin arrancar—, y **la geometría se guarda dos
segundos después de mover la ventana**, no solo al cerrar, para que un apagado
brusco de la máquina no se lleve por delante lo que acabas de colocar.

Limitaciones conocidas:

- La BD vive junto al ejecutable (`pc_inspector.db`, demo portable); si no se
  puede abrir o migrar, la app sigue funcionando sin persistencia y lo deja en
  el log (nunca crash).
- BLOBs float32 en orden nativo little-endian: la BD no es portable a
  arquitecturas big-endian (irrelevante para x86/x64).
- Las migraciones posteriores a la v1 añadieron: rasgo distintivo (v2), ajuste
  de orientación (v3), plantillas múltiples (v4), modo de medición y tablero por
  pieza (v5), centrado del tablero y ajuste fino (v6), tolerancias de posición
  (v7) y perfiles de detección (v8).

## Fase 5 — `inspection_editor/` (editor de plantilla)

Editor estilo VisionMaster: botón **"Plantilla…"** en la ventana principal
(usa el último frame de la cámara o una imagen desde archivo —
`sample_images/pieza_demo.png` sirve para probar sin cámara). Las **diez**
herramientas se dibujan sobre la imagen, se seleccionan y mueven con el ratón, y
**"Probar sobre esta imagen"** las ejecuta al instante mostrando OK/NG y el
valor medido para ajustar tolerancias.

- Las cinco iniciales — **Caliper** (distancia entre 2 bordes), **Círculo**
  (diámetro + redondez por mínimos cuadrados sobre rayos), **Punto-Línea**
  (distancia perpendicular), **Borde liso** (desviación máxima respecto a la
  recta ajustada) y **Blob** (conteo por área mínima y polaridad) — más
  **Regla**, **Línea-Línea**, **Ángulo**, **Blob poligonal**, **Posición**
  (desviación respecto al cero del tablero), **Arco** (radio de una esquina) y
  **Eje / Diámetro** (diámetro, conicidad y rectitud de una pieza de torno) y
  **Rosca** (paso, diámetros y ángulo de flanco, con designación métrica) y
  **Engranaje** (dientes, módulo, diámetros y excentricidad). Ver la tabla completa con la
  técnica de cada una en [ARQUITECTURA.md](ARQUITECTURA.md#5-herramientas-de-medición).
- La geometría se guarda **en coordenadas del fixture** (tabla
  `InspectionTools`): si la pieza llega rotada, las herramientas se mueven con
  ella (verificado por test: misma medida ±1.5 px con la pieza a 20° y 125°).
- La detección de bordes usa perfil promediado + gradiente + refinamiento
  subpíxel parabólico (precisión típica ±1 px en sintético).

Limitaciones conocidas:

- Las herramientas de pieza torneada (Arco, Eje, Rosca, Engranaje) **avisan de
  las condiciones en las que midieron**: cámara inclinada, borde con poco
  contraste, arco demasiado corto, filete demasiado pequeño o falta de
  calibración. No es decoración: estas medidas salen de una silueta 2D y con
  datos malos dan números creíbles y falsos. Lee el detalle del resultado, no
  solo el valor.
- La interacción del editor (ratón) sí tiene pruebas automáticas: `pci_gui_tests`
  renderiza el lienzo fuera de pantalla y le inyecta clics y arrastres, y
  `test_canvas_geometry` cubre la aritmética del trazado (vista, zoom,
  desplazamiento, manijas y selección) sin necesidad de ventana. Lo que sigue
  verificándose a mano es la vista en vivo con cámara física.
- El **Borde liso** solo detecta lo que cae dentro de su ventana de escaneo: una
  muesca más profunda pasa desapercibida (súbela si esperas defectos grandes).

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

**Varios acabados de la misma pieza**
(*Pieza ▸ Registrar otro acabado de esta pieza…*).
La misma pieza de dos proveedores, con dos acabados admisibles o antes
y después de un cambio de lote no se parece a sí misma, y meter las dos cosas en
la misma referencia **no da falsos NG: deja de vigilar**. La media se coloca
entre los dos grupos, la banda se ensancha —medido, de 0,98 a 0,68— y un defecto
que antes se detectaba pasa.

Registrado como acabado aparte, cada uno conserva su media y su banda, y una
pieza es buena si se parece a **cualquiera** de ellos. Comparte herramientas,
tolerancias e historial con la pieza, que es lo que lo distingue de registrarla
otra vez — eso crearía una pieza distinta.

**Dónde difiere.** Un NG por apariencia era hasta ahora un número: la similitud
cayó por debajo de la banda y a buscar a ojo qué le pasa a la pieza. Ahora el
diálogo de resultado añade una tercera miniatura que **señala el sitio**, y lo
dice también en palabras: *«lo más distinto está arriba a la izquierda de la
pieza, y ocupa el 2,0 % de su superficie»*. Esa cifra de superficie separa un
arañazo de una pieza equivocada — medido, un arañazo enciende el 0,1 % y otra
pieza distinta el 8,9 %.

La comparación es **tolerante al desalineamiento**, y sin eso no serviría: dos
fotos de la misma pieza nunca coinciden píxel a píxel, así que una resta a secas
encendería el contorno de todas las piezas. Medido: la misma pieza desplazada
uno o dos píxeles, o fotografiada con otra luz, no enciende **nada**. Y si no
hay nada que señalar, la miniatura no aparece: un mapa que siempre enseña algo
enseña a ignorarlo.

Arquitectura: `domain/` (veredicto y criterios de calidad, sin Qt ni OpenCV)
y `engine/` (orquestador). El extractor de embeddings se inyecta como función:
los tests end-to-end corren con embeddings sintéticos sin el modelo ONNX, y
sin modelo la app degrada a inspección solo geométrica (avisado, nunca crash).

Limitaciones conocidas:

- El umbral de anomalía es `simMean − max(k·σ, 0.02)` sobre similitud coseno
  (k configurable en Preferencias, 3 por defecto); con referencias de pocas
  muestras conviene registrar las 30 recomendadas.
- **Sin el modelo ONNX también se puede registrar**: la pieza queda en modo
  "solo herramientas" (se mide, pero no hay comparación de apariencia). Se avisa
  una vez por sesión.
- En el modo **Especial**, el veredicto suma las reglas de posición (centrado y
  giro) a la apariencia y las herramientas.
- Los flujos con cámara en vivo se prueban a mano; la batería automática usa
  imágenes sintéticas.
