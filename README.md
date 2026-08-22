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
   unidad de medida se elige en **Ver ▸ Unidad**. La cámara elegida queda
   guardada.

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

   **Configurar** (menú *Cámara ▸ Configurar…*) es **el único sitio** donde se
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

   En la pestaña *Detección* están además el **área mínima y máxima de pieza**
   (en % de la imagen): deciden qué se acepta como pieza y antes estaban fijas
   en el código. Con piezas pequeñas, el 0,5 % por defecto es justo la frontera
   entre "no hay pieza" y "hay pieza" — bájalo si no se detectan, súbelo si se
   cuela ruido.

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

   **Deshacer y rehacer**: con el pincel activo, **Ctrl+Z** deshace la última
   pincelada y **Ctrl+Y** la rehace. Con el pincel apagado, esos mismos atajos
   siguen siendo los de las herramientas dibujadas, como siempre: hay un solo
   deshacer, y hace lo que toca según tengas el pincel en la mano o no.
   También están en el menú *Corregir borde*. Quitar todas las correcciones
   también se deshace.

   La corrección vale para **esa imagen**: no cambia cómo se detectan las demás.
   Pero no hace falta que la ajustes a ojo si se repite — para eso está lo
   siguiente.

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

   El informe **no se corta**: una lista de propuestas que hay que revisar a mano
   sí se limita a doce, pero un informe cortado contesta a medias. Y nada sale
   marcado «OK», porque una cota recién medida está dentro de su propia
   tolerancia por construcción — todavía no la ha comprobado nadie.

4. **Medir automáticamente**: en el editor de plantilla, el botón
   **Medir automáticamente…** mira la pieza y propone las cotas que encuentra.
   No las inserta a lo loco: abre una lista con **la medida de cada una, su
   tolerancia sugerida y por qué se propone**, todas marcadas de entrada, y añade
   solo las que dejes marcadas — en un solo paso que Ctrl+Z deshace entero. Cada
   propuesta se ha medido de verdad antes de ofrecerse, así que lo que aparece
   en la lista ya funciona sobre esa pieza.

   **Primero mira qué FIGURA es tu pieza**, y de ahí sale qué te propone:

   | Si la pieza es… | Te propone |
   |---|---|
   | **Redonda** | el **diámetro** (con su perímetro en el porqué) y la **redondez** |
   | **Una arandela** | **Ø exterior**, **Ø interior** y la redondez |
   | **Un polígono** | **cada lado** con su propia cota, el **recuento de lados** y **cada ángulo** |
   | **Un polígono redondeado** | cada tramo recto, el **radio de cada redondeo** y el largo/ancho |
   | Cualquier otra | **cada cara recta** que tenga, largo y ancho, un círculo por agujero, un espesor por cada par de caras paralelas y un ángulo por esquina |

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

   **Sacar las medidas**: en el resultado de una inspección tienes **Copiar
   medidas** (texto alineado, para pegar en un correo o en un parte) y
   **Exportar CSV…** (columnas que una hoja de cálculo puede sumar y promediar).
   Cada fila lleva su valor, **su unidad**, los píxeles crudos por si luego
   recalibras, el estado y la tolerancia. Sin calibración da píxeles y lo dice.

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
   **Las cinco familias de un vistazo** (32 herramientas). La lista detallada
   viene después; esto es para saber dónde mirar.

   | Familia | Herramientas | Para qué |
   |---|---|---|
   | **Figuras básicas** (7) | Borde liso · Blob · Blob poligonal · Región · Simetría · Lados · Rebabas y mellas | La forma y lo que hay dentro de una zona: contar, describir, buscar defectos de borde |
   | **Medición en línea** (8) | Caliper · Círculo · Punto-Línea · Regla · Línea-Línea · Ángulo · Arco · Holgura | Las cotas de toda la vida: distancias, diámetros, radios y ángulos |
   | **Construcciones** (3) | Punto construido · Recta construida · Eje medio | No miden: **fabrican** referencias (una intersección, una bisectriz, un eje) para que otras midan contra ellas |
   | **GD&T** (7) | Posición · Rectitud · Redondez · Orientación · Desviación de centros · Patrón de agujeros · Perfil de línea | Tolerancias geométricas, siempre contra un marco de referencia declarado |
   | **Máx./mín. y torneadas** (7) | Eje / Diámetro · Rosca · Engranaje · Máx./mín. · Chaflán · Radio de acuerdo · Ranura | Piezas de torno y medidas que no dependen de acertar la dirección del trazo |

   **Lo que una silueta no puede medir.** Conviene saberlo antes de prometerle
   nada a nadie: no es que falte programarlo, es que **la información no está
   en la imagen**. La app no ofrece ninguna de estas medidas, y donde algo se
   le parece lo dice en el detalle en vez de dar un número que aparenta serlo.

   | No se puede | Por qué | Lo que sí hace la app |
   |---|---|---|
   | **Profundidad o altura (Z)** | Una cámara 2D proyecta; la coordenada perpendicular al plano se pierde. Un agujero ciego y uno pasante dan la misma silueta | Nada: se dice, no se estima. Haría falta cámara de profundidad o estéreo |
   | **Rugosidad, dureza, material** | No son geometría del contorno | Nada. La *Apariencia* detecta que algo cambió, pero no mide Ra |
   | **Cilindricidad, alabeo, planitud** | Necesitan varias secciones o un plano fuera de la imagen | *Rectitud* y *Redondez* por zona mínima, que son sus equivalentes 2D, y se llaman así |
   | **Concentricidad (ASME Y14.5)** | Exige el eje real de revolución, que una foto no da | *Desviación de centros*, que es otra cosa y por eso tiene otro nombre |
   | **Ø primitivo real de una rosca (d2)** | Se mide con el método de los tres alambres, no ópticamente | Paso, Ø exterior y Ø de fondo; y el **sesgo de hélice** del ángulo de flanco, cuantificado |
   | **Cotas de una pieza inclinada** | Fuera del plano de calibración la escala cambia con la altura | La calibración por tablero corrige la perspectiva **del plano del tablero**; apoya la pieza plana |

   La lista completa, herramienta por herramienta:

   - *Caliper*: línea que cruce los dos bordes a medir → distancia entre ellos.
   - *Círculo*: arrastra del centro al borde → diámetro y redondez.
   - *Punto-Línea*: línea de referencia → distancia perpendicular del borde.
   - *Borde liso*: línea sobre un borde recto → desviación máxima (muescas).
     Solo ve lo que cae dentro de su **ventana de escaneo**: una muesca más
     profunda que esa ventana pasa desapercibida, así que súbela si esperas
     defectos grandes.
   - *Blob*: rectángulo sobre una zona → conteo de manchas/agujeros.
   - *Blob poligonal*: región de forma libre (clic para marcar vértices, clic
     sobre el primero para cerrar) → mismo conteo que el Blob pero para zonas
     irregulares que un rectángulo no cubre bien.
   - *Arco*: **radio** de una esquina redondeada o un redondeo. Se marcan tres
     puntos sobre el arco (los dos extremos con un arrastre y luego el punto
     por donde pasa), igual que al comprobarlo con una plantilla de radios. El
     *Círculo* no sirve aquí porque pide un centro y un contorno cerrado, y una
     esquina no tiene ninguno de los dos. Ojo: **cuanto más corto es el tramo
     marcado, menos fiable es el radio** — sobre un arco corto el radio y el
     centro son casi indistinguibles y un error pequeño del borde se amplifica.
     La herramienta avisa por debajo de 30° de tramo; alarga el arco si puedes.
   - *Eje / Diámetro*: para piezas de **torno vistas de perfil**. Traza el eje
     a lo largo de la pieza, por el medio, y de un solo trazo salen el
     **diámetro**, la **conicidad** (cuánto cambia el diámetro de un extremo al
     otro) y la **rectitud**. Un calíper mide en un punto, y en un punto un
     cilindro y un cono son idénticos. Da igual que el eje no quede centrado:
     lo que se mide es la separación entre los dos bordes ajustados. Si dice
     que faltan bordes, sube el **alcance de búsqueda** — el aviso te dice el
     valor actual.
   - *Rosca*: **paso**, diámetro exterior, diámetro de fondo y **ángulo de
     flanco** de un tornillo visto **de perfil**. Traza el eje a lo largo de la
     parte roscada: el perfil se repite una vez por vuelta, y de ese periodo
     sale el paso. Con la escala calibrada **propone la designación métrica**
     (M6×1, M8×1.25…); sin ella se niega, porque un paso en píxeles no
     identifica ningún tornillo. Necesita ver **varias vueltas**, y el ángulo de
     flanco además necesita que el filete se vea grande: con 50 px de altura de
     filete sale a ±1°, con 12 px deja de ser fiable y la herramienta lo dice.

     Hay un segundo límite del ángulo de flanco, y este **no se arregla
     acercando la cámara**: una rosca no es un perfil plano repetido, es una
     hélice. Vista de lado, el flanco cercano y el lejano se proyectan con
     inclinaciones distintas y la silueta del filete sale engrosada. Los
     comparadores ópticos lo corrigen **inclinando el eje óptico** el ángulo de
     hélice; una cámara fija sobre la mesa no puede, así que queda un **sesgo
     sistemático** — no se va repitiendo la medida ni promediando piezas.

     Como el ángulo de hélice sale del paso y del diámetro, que ya están
     medidos, la herramienta puede decir **cuánto**: `atan(paso / (π·Ø medio))`.
     Es un cociente entre dos longitudes, así que vale igual sin calibrar. El
     aviso aparece **solo si el sesgo supera 1°**, que es lo mejor que la
     medida resuelve: en un paso grueso salta (5,4°), en una M6×1 salta (3,4°)
     y en una rosca fina se calla (0,7°). Un aviso que saltara en toda rosca
     sería un aviso que se aprende a ignorar, y entonces tampoco serviría donde
     importa.
   - *Engranaje*: cuenta los **dientes** y mide **Ø de cabeza**, **Ø de raíz**,
     **módulo**, **Ø primitivo** y **excentricidad**. La rueda debe verse **de
     cara**: arrastra del centro hacia fuera, pasando la punta de los dientes.
     Cuenta por periodicidad y no picando el contorno, así que un diente mellado
     no descuadra el recuento — y como contraste hace también el conteo por
     picos y **avisa si los dos no coinciden**. El módulo **exige calibración**:
     sin escala real no existe, y se dice en vez de dar un número sin unidad.
   - *Regla*: distancia directa entre dos puntos fijos (no busca bordes) —
     con la escala calibrada mide en mm/cm al vuelo.
   - *Línea-Línea*: dos líneas de referencia (se dibujan en dos pasos) → mide el
     **ángulo** entre ellas en grados (0–90°), con la separación perpendicular en
     el detalle. La tolerancia se define en grados; no se calibra a mm.
   - *Ángulo*: una esquina (vértice + dos lados, en dos pasos) → mide el
     **ángulo interior** en grados (0–180°). Tolerancia en grados; ideal para
     chaflanes y esquinas.
   - *Posición*: marca un rasgo y vigila **dónde cae respecto al cero del
     tablero** de referencia (ver más abajo). Mide la desviación radial, solo en
     X o solo en Y (campo *Eje*: 1/2/3) y la compara con sus tolerancias, así
     que el tablero pasa a ser criterio OK/NG y no solo ayuda visual. Con el
     cero *en la propia pieza* la desviación es constante: usa el centro de la
     imagen o un punto fijado para que mida algo útil.

     **Con datums declarados es la posición verdadera de la norma.** Elige en
     *Referencia* el datum **primario** (una recta: orienta el marco) y en *2ª
     referencia* el **secundario** (fija el origen; vale una recta o un punto,
     que es como se usa un agujero). Entonces la medida pasa a ser el **diámetro
     de zona**, 2·√(dx²+dy²) respecto al punto teórico, medido dentro del marco —
     y **no cambia aunque la pieza llegue girada**, porque el marco gira con
     ella. Sin datums se comporta exactamente como antes.

     Solo es honesta si los datums se resuelven en el plano de la imagen: una
     cara perpendicular a la cámara no da datum, y entonces no mide. Un marco a
     medias tampoco: sin secundario no hay origen, y dos datums paralelos no se
     cortan.
   - *Región*: describe la **forma** de lo que hay dentro del recuadro. Arrastra
     un rectángulo sobre la pieza y elige en *Medida* qué vigilar: **área**,
     **perímetro**, **solidez** (cuánto se aparta de su casco convexo: 1 = sin
     entrantes), **circularidad**, **relación de aspecto** (lado largo/corto del
     rectángulo mínimo) o **número de agujeros**. Cada Región vigila **una** cosa
     con su tolerancia, así que pon una por cada medida que te importe; el
     detalle enseña las seis de todas formas, para que veas cuál merece la pena.
     Referencias medidas de la circularidad: un círculo da **0,99** y un cuadrado
     **0,82** (el valor exacto de un cuadrado es 0,785 — la diferencia es el
     sesgo conocido de medir un borde recto sobre una rejilla de píxeles). Una
     mota de ruido **no** cuenta como agujero.
   - *Ranura*: el **ancho, la profundidad y el diámetro de fondo** de una
     entalla en una pieza de torno — la cota de un alojamiento de anillo de
     retención. Traza el eje a lo largo de la pieza **pasando por la ranura**,
     igual que con el *Eje torneado*, y elige en *Medida* cuál vigilar; el
     detalle enseña las tres de todas formas.

     Por dentro reutiliza el mismo perfil radio-contra-posición-axial que el
     *Eje*, con una diferencia que lo es todo: el *Eje* ajusta una recta a cada
     borde para describir el conjunto, y aquí eso borraría la medida —la ranura
     es justo el sitio donde el borde se sale de esa recta—, así que se usa el
     perfil crudo corte a corte y se busca su mínimo local.

     **El ancho se mide contando cortes, así que el paso axial es su
     resolución**, y eso tiene dos consecuencias que la herramienta dice en voz
     alta en vez de tragárselas:

     - Si la ranura abarca **uno o dos cortes**, sus flancos no están resueltos y
       el ancho **no se mide**: te dice cuántos cortes ha abarcado, cuánto mide
       el paso y cuánta ranura haría falta. Devolver ahí un número sería el paso
       de muestreo disfrazado de medida.
     - Si la ranura es **más fina que el paso**, puede que no caiga dentro
       *ningún* corte y el perfil salga plano. Desde el perfil, «no hay ranura» y
       «se me ha colado entre dos cortes» son indistinguibles, así que el aviso
       de «no se ve ninguna ranura» **añade siempre** cuál es la ranura más fina
       que ese muestreo podría ver. Dar por buena la pieza sin más sería el error
       caro.

     En los dos casos la salida es la misma: subir el número de cortes. Con el
     muestreo adecuado la misma pieza se mide sin problema — el límite es la
     resolución que elegiste, no la herramienta rindiéndose.

     Qué exactitud esperar: medido sobre ranuras de 9 a 59 px con dos muestreos,
     el error se queda **por debajo de un corte** siempre (±1,5 px con paso 2,52;
     ±0,97 px con paso 1,00) y **cambia de signo con el paso**. Es cuantización
     del muestreo, que es justo lo anunciado, y no un sesgo de escala escondido:
     el error relativo se diluye al crecer la ranura (12 % en la de 9 px, 2,5 %
     en la de 59) en vez de mantenerse constante.

     Si la ranura llega al extremo del trazo no se ven sus dos flancos y te lo
     dice: alarga el eje por fuera de la ranura.
   - *Radio de acuerdo*: el radio del redondeo de transición y —lo que de verdad
     aporta— **si empalma tangente** con las caras vecinas. Arrastra un recuadro
     que abarque el acuerdo con un trozo de las dos caras.

     Un acuerdo que no entra tangente es un defecto de mecanizado (un salto, una
     herramienta mal compensada) y **el radio por sí solo no lo delata**: medido
     sobre tres piezas con un escalón de 0°, 12° y 22°, el radio da 50,5 / 50,4 /
     50,4 —indistinguibles— y la tangencia 4,4° / 10,7° / 20,6°.

     El suelo de ruido no es cero: sobre un acuerdo perfectamente tangente, el
     dentado de los píxeles deja unos 3-4° de desviación aparente. Por debajo de
     eso la herramienta no puede afirmar nada, y conviene saberlo antes de poner
     la tolerancia.
   - *Chaflán*: el **ángulo del bisel y sus dos catetos**, que es como lo escribe
     un plano: «1 × 45°». Arrastra un recuadro que abarque la esquina
     achaflanada **con un trozo de las dos caras**; el recuadro *selecciona* qué
     tramos del borde se miran, no recorta la pieza.

     Los catetos se miden desde la **esquina virtual** —donde se cortarían las
     dos caras si no hubiera bisel—, que es de donde los acota el plano. Ahí no
     hay ningún punto de la pieza: hay que construirla, y por eso hace falta ver
     las dos caras. Se dan ordenados por tamaño (mayor y menor) y **cada uno con
     el ángulo del bisel respecto a su cara**, porque el plano acota desde una de
     las dos y tienes que poder comparar con la que sea.

     Si no encuentra tres tramos rectos te dice cuántos vio dentro del recuadro,
     cuántos quedaron fuera y cuántos salieron curvos — las dos razones distintas
     por las que un encuadre falla.
   - *Máx./mín.*: la medida **más grande y más pequeña de la pieza en cualquier
     dirección**, no en la que acertaras a trazar. Arrastra un recuadro sobre la
     pieza y elige en *Medida* cuál vigilar: **anchura mínima** (la banda más
     estrecha que la contiene — «¿pasa por la ranura?») o **diámetro máximo**
     (los dos puntos más separados — «¿cuánto hueco necesita?»). Las dos se dan
     siempre en el detalle **con su dirección**, para que sepas por dónde.

     No salen de `minAreaRect`: ese minimiza el **área**, y ni su lado corto es
     la anchura mínima ni su diagonal el diámetro. En un triángulo equilátero de
     lado 150, su diagonal marca 198.
   - *Perfil de línea*: cuánto se separa el contorno de la pieza del que
     **debería** tener. Es la tolerancia GD&T más honesta para una silueta,
     porque está definida sobre una línea y no sobre una superficie: lo que se ve
     en la imagen es exactamente lo que la cota describe.

     El nominal **se captura del contorno de la pieza buena** al crear la
     herramienta, y se queda guardado dentro de la plantilla — no hace falta
     ningún DXF. Colócala sobre la pieza de referencia; si la que tienes delante
     no es la buena, el nominal que captures tampoco lo será. Da la zona
     bilateral 2·máx|d| y, por separado, **cuánto sobra y cuánto falta**, que son
     dos averías distintas.

     No alinea nada, y no le hace falta: la pieza ya viene alineada por su
     fixture. Comprobado con la misma pieza medida con el fixture girado 30° —
     sigue dando perfil limpio.
   - *Patrón de agujeros*: la cota de una **brida**. Arrastra un recuadro que
     abarque la pieza entera: encuentra los agujeros, ajusta el **círculo
     primitivo** y mide cuánto se sale cada uno de su sitio. La medida es la
     desviación del **peor** agujero en diámetro de zona, y el detalle dice **a
     qué ángulo está** — no un número de agujero, que en la pieza no está
     escrito en ninguna parte. Con *Agujeros esperados* puesto, que falte uno es
     el defecto y lo dice en vez de medir un reparto que ya no significa nada.

     La referencia es el **propio patrón**: su primitivo ajustado y su reparto
     angular. Girar la brida entera no cambia nada, que es lo que se quiere
     aquí; para medir contra un datum de fuera, usa *Posición verdadera* en el
     agujero que te interese. El primitivo se ofrece como referencia para otras
     herramientas.
   - *Desviación de centros*: la **distancia entre los centros** de dos
     elementos circulares — «¿están estos dos agujeros centrados uno con otro?».
     Elige los dos en *Referencia* y *2ª referencia*; vale también un punto
     construido, así que se puede medir el punto medio de dos agujeros contra un
     tercero.

     **Esto no es concentricidad ISO/ASME.** La concentricidad se retiró de la
     norma en 2018 por inverificable de forma repetible; para la cota formal usa
     *Posición verdadera* con su marco. El número es correcto, pero no puede
     viajar con el nombre de una cota que ya no existe: acabaría copiado en un
     informe como si lo fuera.
   - *Orientación*: **paralelismo, perpendicularidad y angularidad**, que son la
     misma medida con distinto ángulo nominal (0, 90 o el que pongas). Traza una
     línea sobre el borde tolerado y elige en *Referencia* la herramienta que da
     el **datum**; sin datum no mide, porque una orientación sin decir respecto
     a qué no significa nada.

     **No devuelve un ángulo: devuelve una distancia** — la anchura de la banda,
     orientada según el datum, que contiene todo el borde. La diferencia no es
     académica: un borde ondulado puede ir a 0,4° del datum (paralelo de media)
     y necesitar una banda de 12 px. El ángulo no lo vería, y es el número que
     lleva el plano. El desvío angular se da igualmente, marcado como
     informativo.

     Siempre sale **mayor o igual que la rectitud** del mismo borde: la rectitud
     elige la orientación de la banda buscando la más estrecha, y aquí la impone
     el datum.
   - *Redondez (zona mínima)*: el valor **de la norma** — la separación radial
     entre los dos círculos **concéntricos** más juntos que contienen el borde.
     Se arrastra igual que el *Círculo*. Da **los dos números**: el de zona
     mínima (el del plano) y el de mínimos cuadrados (el que dan casi todas las
     máquinas de medir, y con el que vas a comparar); el primero nunca es mayor.

     **Solo vale de frente.** La silueta de un cilindro visto de perfil no es un
     círculo: son dos tangentes, y ahí no hay redondez que medir por mucho que
     la herramienta se deje dibujar encima. Y con medio contorno tapado **no
     mide**: un diámetro se saca de media pieza, pero la redondez es la forma.

     El *Círculo* ya no llama «redondez» a su número: da la **desviación radial
     máxima** respecto al ajuste, que es media banda. Sobre el mismo disco, 5,1
     px de desviación frente a 10,1 px de redondez de la norma.
   - *Rectitud (zona mínima)*: el valor **de la norma** — la anchura de la banda
     más estrecha de dos rectas paralelas que contiene todo el borde. Traza una
     línea sobre el borde a vigilar; las dos rectas de la banda se dibujan, para
     que el número se pueda comprobar a ojo.

     **Ojo al compararlo con el *Borde liso***: aquel da la desviación máxima
     respecto a la recta media, que es media banda. Este número saldrá **mayor
     sin que la pieza haya empeorado** — medido sobre el mismo borde, 10,3 px de
     rectitud frente a 5,8 px de desviación. Son dos cosas distintas y la que
     aparece en un plano es esta. El detalle publica además la banda de mínimos
     cuadrados, para poder comparar peras con peras.

     Límite de la óptica: es la rectitud **proyectada** en el plano de la imagen.
     Lo que se tuerza hacia la cámara o en contra no se ve, y ninguna cámara sola
     puede verlo.
   - *Holgura*: la separación **más corta** entre dos figuras, y **dónde** está.
     Arrastra un recuadro que abarque las dos; se miden las dos mayores que haya
     dentro (si hay más, lo dice). No es lo que da un *Caliper*: el calíper mide
     donde cruzaste tú, y el sitio donde la pieza está más apretada casi nunca
     es ese — medido sobre dos círculos, 25 px de holgura real frente a 52 px si
     mides 30 px fuera del eje. El mínimo se dibuja con sus dos extremos, porque
     un mínimo que no se puede señalar no se puede comprobar a ojo.

     Si solo ve una figura puede que las dos **se estén tocando**: en cuanto se
     tocan, la silueta las une y ya no son dos. Cuánto se solapan dos piezas
     **no** es una medida que una imagen de siluetas contenga, y la herramienta
     lo dice en vez de inventar un número.
   - *Rebabas y mellas*: cuenta y mide los defectos de un borde **uno a uno**,
     en vez de dar una sola desviación máxima como el *Borde liso*. Traza una
     línea sobre el borde a vigilar. De cada defecto da su **altura**, su
     **extensión** y si es **rebaba** (material de más) o **mella** (material de
     menos) — dos averías distintas con dos arreglos distintos, y el lado del
     material lo decide mirando la imagen, no suponiendo hacia dónde trazaste la
     línea. El campo *Altura mínima* dice a partir de qué desviación algo cuenta:
     la medida es «cuántos defectos mayores que esto». Un borde con una mella
     grande y otro con veinte pequeñas dan la misma lectura con el Borde liso, y
     no son la misma pieza.

     Si un defecto es **más alto que media ventana de escaneo** se sale de ella y
     la herramienta **no lo da por limpio**: avisa de que no pudo ver el borde en
     ese tramo y te dice que subas el largo de escaneo. Es el error más peligroso
     que podría cometer, así que se comprueba a propósito.
   - *Lados*: cuenta los **lados** de un perfil poligonal y mide cada uno y sus
     **ángulos interiores** — el hexágono de una tuerca es el caso típico.
     Arrastra un rectángulo sobre la pieza. El campo *Epsilon* (en milésimas del
     perímetro) decide cuánto se simplifica el contorno: súbelo si cuenta lados
     de más, bájalo si se come alguno. Va en **fracción del perímetro y no en
     píxeles** a propósito, para que el recuento no cambie al acercar la cámara
     ni al subir la resolución — comprobado con el mismo hexágono a tres
     tamaños. Si el recuento **no aguanta** al doblar y al partir ese valor, la
     figura no es un polígono claro (un círculo, por ejemplo) y lo dice en vez
     de dar un número que cambiaría solo.
   - *Simetría*: busca el mejor **eje de simetría** de la silueta y da un
     **grado de 0 a 1** (1 = perfectamente simétrica). Arrastra un rectángulo
     sobre la pieza. Sirve para lo que ninguna cota pilla: una pieza **montada
     del revés**, o con un rasgo que no debería estar. El detalle da también el
     grado en el eje **perpendicular**, que es lo que distingue un rectángulo
     —simétrico en dos ejes— de un triángulo isósceles, que solo lo es en uno.
     El eje que encuentra se puede usar como **referencia** de otras
     herramientas. **No** es la simetría de GD&T: esa se retiró de la norma en
     2018, y darla con ese nombre sería vender como cota algo que ya no lo es.
     Ojo: es la herramienta **más cara** de todas (prueba muchos ángulos), unos
     14 ms por imagen — si vas justo de fps, úsala en inspección única y no en
     auto-inspección continua.
   - *Punto construido* y *Recta construida*: **no miden nada**. Calculan un
     punto o una recta a partir de otras herramientas para que puedas
     **declarar una referencia** (un *datum*). Colócalas con un clic —el clic
     solo elige dónde se escribe el resultado— y en el panel eliges la
     **construcción** y sus **dos referencias**:
     - Punto: punto medio de dos, corte de dos rectas, proyección de un punto
       sobre una recta, o centro de un círculo.
     - Recta: por dos puntos, bisectriz de dos rectas (si son paralelas, sale
       la recta media), o paralela y perpendicular a una recta por un punto.

     Donde se pide "un punto" vale también un **círculo**: aporta su centro,
     que es el datum natural de un agujero. Y una construcción puede
     referenciar a otra, así que se pueden encadenar (dos agujeros → su punto
     medio → la recta desde ahí) sin tener que ordenarlas: el programa las
     resuelve en el orden que haga falta y avisa si se referencian en círculo.

     En la tabla de resultados aparecen con **«—»** en vez de OK: no juzgan
     nada. Lo que sí es un NG es que **no se puedan construir** —dos rectas
     paralelas no se cortan, dos puntos que coinciden no dan recta—, y entonces
     lo dicen con el motivo escrito, porque todo lo que las usaba de referencia
     se queda sin ella.

     Las referencias se ven en el lienzo: una **flecha punteada** va de la
     herramienta que aporta el dato a la que lo usa, para que se sepa qué se
     rompe antes de borrar nada.
   - *Eje medio*: la línea que pasa por el **centro** de una pieza alargada, a
     media distancia entre sus dos flancos — el datum natural de una pieza de
     torno. Se traza igual que el *Eje / Diámetro*, a lo largo de la pieza, y
     **da igual que quede descentrado**: lo que se calcula es el punto medio
     entre los bordes reales, no la línea que dibujaste. Mide su **rectitud** y
     avisa de la **desalineación entre la primera mitad y la segunda**, que es
     lo que delata dos diámetros que no son coaxiales. Si en un tramo solo se ve
     un flanco, **no lo mide**: suponer el centro por simetría sería inventarlo
     justo en la herramienta que existe para encontrarlo (sube el alcance de
     búsqueda — el aviso te dice cuántos cortes vieron los dos bordes).

   > **Antes de medir un diámetro, una rosca o un engranaje: cuatro
   > condiciones.** Todo esto se saca de una **silueta**, y una silueta mala da
   > números creíbles y falsos, que es la peor forma de fallar. Las
   > herramientas avisan cuando detectan que falla alguna, pero es más barato
   > montarlo bien:
   >
   > 1. **Luz.** Borde limpio y de alto contraste: **contraluz**
   >    (retroiluminación) o fondo mate uniforme. Con luz frontal sobre metal
   >    brillante, el borde que ve la cámara no es el borde de la pieza.
   > 2. **Cámara de frente** al plano de la pieza. Inclinada, un círculo se lee
   >    como elipse y los diámetros salen **cortos**. Con el marcador ArUco
   >    puesto, el programa mide esa inclinación y lo avisa.
   > 3. **Calibración.** Un paso de rosca en píxeles no identifica ningún
   >    tornillo y el **módulo de un engranaje directamente no existe** sin
   >    escala real. Sin calibrar, esas herramientas se niegan a darlos en vez
   >    de inventar un número sin unidad.
   > 4. **La vista que toca**: el engranaje **de cara**, la rosca y el eje **de
   >    perfil**. Una rosca vista de frente no tiene paso medible.

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
   acercar/alejar, **Ctrl+0** ajustar a la ventana, **Ctrl+1** ver al 100 %
   (píxeles reales), **Ctrl+2** zoom máximo y **doble clic** para volver al
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

   **Escala por marcador ArUco en vivo** (**Cámara ▸ Escala por marcador
   ArUco**): imprime el marcador `sample_images/aruco_4x4_id0.png`, mide su
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

   **Atajos de teclado** (botón *Atajos (F1)* o tecla `F1`): guía completa y
   **editable** — haz clic en el atajo y pulsa la combinación nueva; se
   guardan en la BD. Por defecto: `Ctrl+Z`/`Ctrl+Y` deshacer/rehacer las
   herramientas dibujadas (crear, mover, borrar — también dentro del editor),
   `Supr` borrar la seleccionada, `Esc` volver a Mover/Elegir, `1`–`9` y `0`
   elegir herramienta de dibujo (`0` = Posición), `V` iniciar/detener cámara,
   `R` registrar y activar, `A` auto-inspección, `I` inspeccionar, `P`
   plantilla, `C` calibrar, `D` rasgo distintivo, `Ctrl+S` guardar la
   plantilla. Vista: `Ctrl++`/`Ctrl+-` acercar/alejar, `Ctrl+0` ajustar,
   `Ctrl+1` 100 % y `Ctrl+2` zoom máximo.

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
7. **Auto-inspección**: el botón queda activo y la app inspecciona el video
   continuamente (~1/s): banner **OK/NG** en vivo, resultados por herramienta
   dibujados sobre el video y estadísticas del día en la barra de estado.
   Todo queda en el historial. Se puede prender/apagar cuando quieras con la
   pieza seleccionada. **Inspección ▸ Ver historial…** abre la tabla de
   inspecciones recientes (fecha, veredicto, similitud, versión de referencia)
   por pieza, con la cantidad a mostrar ajustable y **exportación a CSV**, más
   un **gráfico de tendencia OK/NG por día** (barras dibujadas con QPainter,
   últimos 30 días).
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
