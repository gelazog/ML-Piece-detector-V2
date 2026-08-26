# Bitácora de defectos

Cada sección de aquí abajo es un fallo que apareció en uso, lo que se midió al
buscarlo y cómo se arregló. Están contados enteros, con las cifras, porque el
valor no está en el arreglo: está en **por qué el fallo era invisible hasta que
alguien lo miró de la forma correcta**. Varios se repetirían si esto no
estuviera escrito.

Vivían dentro de `ARQUITECTURA.md`, en un capítulo titulado «Persistencia». Eran
**1 191 líneas de las 1 414 de ese capítulo**: la persistencia de verdad ocupaba
veintiuna. Nadie que buscara «cómo se guardan las plantillas» iba a encontrarlas
ahí, y nadie que buscara «qué ha fallado antes» iba a mirar en un capítulo
llamado así.

Se leen sueltas y en cualquier orden. Para saber **cómo funciona** un subsistema
—en vez de qué le pasó una vez— el sitio sigue siendo
[ARQUITECTURA.md](ARQUITECTURA.md); el mapa de todo está en
[CONTEXTO.md](CONTEXTO.md).

---

### «Solo mide una en automático», y la bandeja salía verde

La página de Piezas ofrece dos modos y el automático dice, con esas palabras,
«cuenta las que haya». Lo que hacía era **no mirar**: el motor solo buscaba las
demás piezas cuando había un número declarado mayor que uno, así que en automático
se medía la mayor y las otras cinco de la bandeja no existían para el informe.

Lo grave no es que falten: es que **el resultado sale verde**. Con tres barras y
una fuera de tolerancia, la bandeja daba OK porque la única pieza mirada estaba
bien. El defecto pasa a producción con un sello de conformidad encima.

El motivo escrito para no mirar era el coste. Medido sobre las imágenes reales del
usuario: **+0,70 ms con dos piezas y +50 ms con cien**, y es por INSPECCIÓN, no por
fotograma. La alternativa a esos 50 ms era no medir noventa y nueve piezas.

Ahora el único caso que no enumera es el declarado a **una**, y ahí es una decisión
del operador que la pantalla promete — para que una sombra no cuente como segunda
pieza. Declarar el número sirve para juzgar el recuento y para quedarse con las N
mayores; no puede ser lo que **enciende** la medición.

**Una prueba fijaba el fallo.** Se llamaba «con una pieza esperada nada cambia», no
declaraba nada —o sea, corría en automático— y exigía que de tres barras se midiera
una. Ahora declara el uno que su nombre dice.

### Dos pestañas al medir: lo que mide el programa y lo que mides tú

Petición de uso: «al momento de darle al botón de medir debería de haber dos
pestañas: una donde se vean todas las medidas automáticamente, y otra donde se pueda
elegir qué herramientas se utilizan, cuáles no».

**El hueco era real.** Hasta ahora el botón de medir enseñaba hechos del contorno y
propuestas automáticas, pero **ninguna de las cotas que el operador había dibujado**.
Para verlas había que inspeccionar — que además guarda en el historial, o sea dos
decisiones distintas metidas en un botón.

La separación en pestañas tiene sentido más allá de la petición: lo de una las mide el
programa solo y no se puede tocar; lo de la otra son cotas con tolerancia y veredicto.
Mezclarlas invitaría a buscarle banda a un perímetro, que es el mismo motivo por el
que la primera pestaña ya separaba hechos de cotas.

**El interruptor no inventó nada.** `ToolConfig::enabled` ya existía, ya se respeta al
ejecutar y ya se guarda en la base y en las plantillas — pero **no tenía ningún
control que lo cambiara**, así que valía `true` siempre. Aquí se le pone el
interruptor que le faltaba. Y `save` ya hacía UPDATE con la columna dentro, así que
tampoco hizo falta un método nuevo en el repositorio.

Tres decisiones que se sostienen solas:

- **Las cotas apagadas se miden igual** para poder enseñar qué darían. Es justo lo que
  hace falta para decidir si volver a encenderlas; una fila en blanco no ayuda a nada.
- **Los interruptores se guardan aunque se cierre sin «vigilar».** Apagar una cota y
  vigilar unas propuestas son decisiones independientes: atar la primera a que se
  pulse el botón de la segunda perdería el cambio sin decir nada.
- **La pestaña vacía se explica** en vez de quedarse en blanco. Una pestaña vacía deja
  al operador pensando que algo falló.

**Y el veredicto dice de dónde sale**, que era la tercera parte de la petición: «si no
cumple, simplemente diga que no cumple en su descripción de medida». En vez de un
«NG» pelado que obliga a buscar la tolerancia en otra pantalla, la fila dice
**«NO CUMPLE — mide 140,00 y se admite entre 90,00 y 110,00»**.

### Elegir qué clases de cota propone la medición automática

Petición de uso: «que el usuario elija qué herramientas se van a usar y cuáles no
para la medición automática». Tiene sentido — el proponedor ofrece hasta doce cotas
de siete clases, y quien solo inspecciona diámetros acaba desmarcando nueve propuestas
cada vez.

**Lo que decide si esto sirve de algo es DÓNDE se aplica el filtro.** Va **antes** del
recorte por el tope. Medido sobre una pieza con esquinas redondeadas y el tope en 8:

| | sin filtro | pidiendo arcos |
|---|---|---|
| reglas | 6 | — |
| círculos | 2 | — |
| **arcos** | **0** de 4 | **4** |

Sin filtro los arcos **desaparecen del todo**: el operador que quisiera medir los
redondeos no vería ni uno, y no tendría forma de saber que existían. Si el filtro
fuera después del recorte, pedir arcos seguiría dando cero — se filtraría sobre una
lista que ya los perdió.

Por eso el diálogo **vuelve a proponer** al cambiar una casilla en vez de esconder
filas. Se le pasa una función para hacerlo, no la imagen: así la ventana sigue siendo
la dueña del pipeline y el diálogo solo sabe pedir. Y si no se le pasa, no enseña el
filtro — un control que no puede cumplir lo que ofrece es peor que no tenerlo.

Dos decisiones pequeñas que se sostienen solas:

- El filtro está en **un solo punto** del proponedor y no en los diez sitios donde se
  añade una propuesta. Diez guardas es una para olvidar, y así una clase nueva no
  puede colarse sin pasar por él.
- **Lista vacía significa TODAS**, no «ninguna». Si significara ninguna, cualquier
  llamante que no conozca la opción dejaría de proponer en silencio y nadie sabría
  por qué.

**Y una prueba que no demostraba nada.** La primera versión usaba el tope de fábrica,
con el que esa escena da 12 cotas justas y el recorte no llega a morder: pasaba sin
comprobar la propiedad que decía comprobar. Bajando el tope hasta que el efecto es
visible, la prueba afirma algo.

### «Dice que se ven 3 y tengo dos; cuando le doy, sigue saliendo 2»

Queja de uso, y era un fallo de verdad: el panel de Piezas daba **dos números
distintos para la misma pregunta**.

- el aviso de abajo usaba las **manchas vistas** (`lastPiecesSeen_`) → «se ven 3»
- el botón «usar lo que se ve» ponía las **usadas** (`lastPieceCount_`) → 2

Y las usadas ya vienen recortadas a lo declarado, así que el botón **nunca podía
subir el número** — que es literalmente el propósito que su propio comentario tenía
escrito: «tiene que poder subir el número cuando de verdad hay más piezas de las
declaradas». Encima, al pulsar, el aviso se reescribía con el número nuevo y el «3»
desaparecía.

Si la tercera mancha es una sombra, lo que hay que arreglar es la detección — y para
eso está el aviso de al lado, que dice cuántas ve.

**La prueba costó dos vueltas, y las dos por lo mismo.**

1. La primera construía la `PiecesPage` suelta y **simulaba** lo que hace la ventana.
   Mutando el arreglo seguía en verde: no protegía nada. Es la misma trampa que ya
   apareció en `applyPiecesPage` — un camino de prueba que funciona mientras el real
   no.
2. La segunda sí pasaba por la ventana, pero declaraba **una** pieza. Con una
   declarada el motor **deja de enumerar a propósito**, así que «vistas» y «usadas»
   valen lo mismo y la mutación seguía siendo invisible.

La que funciona reproduce el caso del operador tal cual: **tres manchas y dos
declaradas**. Es el único escenario donde los dos números se separan.

**Y un cambio de nombre pedido:** «Automática» pasó a **«Contador automático de
piezas»**, y el botón «usar lo que se ve ahora» se movió a la misma fila que el campo
que cambia. «Automática» a secas no dice automática QUÉ — y puesto al lado de un
campo numérico se lee como «el número es automático», que es otra cosa.

### «Si las piezas están muy pegadas, las detecta como una sola»

Queja literal, y exacta: `RETR_EXTERNAL` devuelve una sola mancha cuando dos piezas
se rozan. Los dos engranajes engranados del usuario salían como **una** pieza — y con
una pieza no hay nada que recorrer con las flechas ni que enseñar en el mosaico, así
que el operador se queda sin forma de mirarlas por separado. Las dos quejas que llegó
a reportar —«no sale para estarlas checando» y «las detecta como una»— eran **el mismo
problema**: comprobado que con dos piezas SEPARADAS el selector y el mosaico sí salen.

La técnica: mirar cada mancha **por dentro**. Se calcula su transformada de distancia
y se buscan los «corazones», las zonas más alejadas del fondo. Dos piezas pegadas
tienen dos corazones separados por un cuello estrecho; una pieza sola tiene uno.

**Dos cosas costaron una vuelta cada una:**

1. **Un umbral global sobre la imagen entera no vale.** El valor que separa los
   engranajes (0,5 del radio máximo de la IMAGEN) deja la bandeja de cien tuercas en
   104 piezas, y el que arregla los tornillos (0,7) la deja en **cero**. El umbral
   tiene que ser relativo al radio de **cada mancha**, y entonces vale igual para una
   tuerca pequeña que para un engranaje grande.
2. **El watershed sobre la máscara binaria no corta.** Dentro de la pieza todo vale lo
   mismo, así que no hay relieve que seguir: dibujaba una línea —quitaba 1 713
   píxeles, medido— y los dos engranajes seguían saliendo como un solo contorno. Sobre
   la distancia **invertida** sí, porque ahí el cuello es una **cresta**. Y la frontera
   se engorda a tres píxeles: con uno, las dos mitades quedan tocando en diagonal y
   `findContours` con conectividad de 8 las vuelve a unir.

**Nace apagada, y está medido por qué.** Sobre las imágenes reales:

| imagen | reales | sin separar | separando |
|---|---|---|---|
| dos engranajes engranados | 2 | **1** | **2** ✓ |
| tres tornillos en fila | 3 | 3 | 3 |
| bandeja de cien tuercas | 100 | 100 | 100 |
| un engranaje solo | 1 | 1 | 1 |
| **un tornillo largo solo** | 1 | 1 | **2** ✗ |

#### La paleta, y lo que apareció al auditar la interfaz

Petición de uso: *«sigue haciendo las interfaces del usuario más estéticos, y
cómodos a simple vista»*. Lo primero que salió al medir no fue cuestión de
gusto.

**No había ninguna paleta.** `main.cpp` no fija estilo ni hoja de aplicación:
**48 llamadas a `setStyleSheet` repartidas en 12 ficheros**, cada una con el
color escrito a mano. El resultado, contado: **81 valores de color distintos**,
con **nueve** para «no cumple», **seis** para «cumple», **seis** para «aviso» y
**siete** para «texto apagado». Un operador que aprende que lo rojo no cumple
tiene que poder fiarse en toda la aplicación.

**Y cinco colores no llegaban al contraste mínimo de WCAG**, calculado:

| color | para qué | contraste |
|---|---|---|
| `#ffc861` | la pista «prueba el otro método» | **1,35:1** |
| `#ffb454` | nota de posición | 1,55:1 |
| `#22cc44` | confirmaciones y el punto ● de estado | 1,88:1 |
| `#e08a00` | avisos | 2,36:1 |
| `#999999` | ayudas y estados vacíos | 2,50:1 |

Ninguno lo sabía, porque nadie los había calculado nunca. Ahora `ui/theme.h`
define los papeles —no los aspectos: `kBad`, no `kRojo`— y `tests/test_theme.cpp`
recalcula el contraste de cada uno en cada compilación.

**Un fallo de mi propia paleta, cazado por su propia prueba.** El primer intento
puso «no cumple» en `#b3261e` y «aviso» en `#8a5300`: los dos con contraste de
sobra sobre blanco, y con luminancias de **0,111 y 0,116**. Casi el mismo gris.
Para un daltónico deutan —uno de cada doce hombres— ese par es indistinguible, y
en un parte impreso también. Los tokens se separan ahora **por luminancia además
de por tono**.

**Dos juegos, porque la aplicación no es de un solo tema.** El informe de
inspección y el calibrador de lente se pintan sobre negro —llevan imagen encima,
y un marco claro alrededor de una foto la falsea—. Poner el rojo de fondo claro
sobre `#1a1a1a` da 1,95:1: ilegible. De ahí `kBadOnDark` y compañía.

**Un trinquete, no un portazo.** Quedan 49 colores a mano y `test_palette_guard`
prohíbe que ese número suba. Convertirlos todos de golpe sería un cambio enorme
sobre diálogos que funcionan, y se revisaría mal; dejarlos sin vigilancia es
exactamente como se llegó a tener nueve rojos.

##### Tres fallos que la auditoría destapó, y no eran de estética

**La ayuda enseñaba a pulsar la tecla que borra el trabajo.** El tooltip de la
tira de capturas prometía *«con Supr se quita de la tira»* y eso **no pasaba**:
`CaptureTray::removeAt` estaba escrita y no la llamaba nadie. Mientras tanto
`Supr` es un atajo de VENTANA atado a borrar la herramienta seleccionada, y
`QListWidget` no se queda con esa tecla — así que pulsar Supr con el foco en la
tira borraba una cota de la plantilla **y de la base de datos**, en silencio.
Ahora hay un atajo de ámbito widget: mientras el foco esté en la tira, Supr es
suyo.

**`Ctrl+1` y `Ctrl+2` estaban asignados dos veces.** Las cinco familias de
herramientas se reparten `Ctrl+1 … Ctrl+5`, y el encuadre volvía a pedir `Ctrl+1`
para «vista al 100 %» y `Ctrl+2` para «zoom máximo». Dos acciones con la misma
secuencia no se turnan: Qt emite `ambiguousActivate` y **no dispara ninguna de
forma fiable**. Cuatro atajos documentados en F1 y ninguno hacía lo que decía.
Los de encuadre se mueven a `Ctrl+Alt`.

**En el vídeo, el veredicto iba solo en el color.** La etiqueta de cada cota
escribía «nombre: medida» en verde o rojo, sin OK ni NG. Es la pantalla que el
operador mira mientras trabaja. Medido: sobre mesa blanca —el montaje normal— la
caja de fondo al 67 % dejaba el rojo de «no cumple» en **2,21:1**, peor que el
verde de «cumple» (3,99:1). *El estado que hay que ver era el que menos se
veía.* Ahora la etiqueta dice «OK» o «NG» y la caja tapa al 88 %: el rojo pasa a
**4,94:1** y deja de depender del color de la pieza que estés midiendo.

#### El clic derecho pedía opciones y borraba

Petición de uso: *«agrega alguna función al clic derecho»*. Al ir a hacerlo
apareció algo peor que un hueco: **el clic derecho sobre una cota la borraba en
el acto**, sin menú y sin preguntar. En cualquier otro programa ese gesto
significa «enséñame qué puedo hacer aquí»; aquí era el único que destruía
trabajo, y bastaba errar el botón del ratón una vez sobre la cota equivocada.

Tres reglas al montar el menú, y las tres se notan:

- **Solo sale lo que aplica.** Un menú con la mitad de las entradas en gris
  obliga a leerlas todas para descubrir que no servían. Sobre una cota se
  ofrecen las de la cota; sobre el vacío, las del vídeo.
- **Lo destructivo, al final y separado.** Borrar comparte menú con duplicar, y
  un gesto de más con el ratón no puede costar el trabajo de media hora.
- **Cada entrada dice sobre qué actúa.** «Borrar» a secas no distingue entre la
  cota bajo el cursor y todas; el nombre va dentro: *Borrar «Ø exterior»*.

Y una entrada que **se ahorra un modo entero**: *marcar aquí el rasgo
distintivo*. Antes era pulsar un botón, dejar el programa esperando en un estado
invisible, y acertar con el siguiente clic — dos gestos y una espera para poner
un punto. En el menú el operador ya ha señalado dónde lo quiere, así que el punto
del clic viaja con el aviso.

La lona **no monta el menú**: dice dónde se pulsó y sobre qué, y quien conoce las
acciones lo monta. Emite también sobre el vacío (`tool` = −1), que antes se
tragaba el clic sin hacer nada.

Dos detalles que salieron al escribirlo:

- **Renombrar rechaza el nombre repetido**, y no por pulcritud: la ventana
  empareja herramienta y resultado **por nombre** en varios sitios, así que dos
  cotas llamadas igual harían que una enseñara el valor de la otra.
- **Copiar lo que mide** copia la ÚLTIMA lectura guardada, no una nueva. Volver a
  ejecutar la herramienta daría un número medido en otro instante, con la pieza
  ya movida, y el operador creería estar copiando lo que tiene delante. Y si no
  hay lectura todavía, se dice — copiar en silencio un portapapeles vacío hace
  que se pegue lo que se copió antes sin enterarse.

#### Tres arreglos de uso, con su porqué

**El botón de borrar todo no hacía nada sin pieza abierta.** Queja: *«la
herramienta de borrar todo no detecta nada o no me deja usarla, hasta que
selecciono una pieza»*. Exacta, y con una ironía dentro: `onDeleteAllToolsClicked`
se iba **en silencio** cuando `liveTools_` estaba vacío, y `liveTools_` solo se
llena al seleccionar pieza. O sea que la salida «borrar las de todas las piezas»
—añadida justo para no tener que ir pieza por pieza— vivía dentro de un diálogo
que ese `return` impedía abrir. Ahora el recuento de todo el programa se hace
**antes** de decidir si hay algo que hacer, y cuando de verdad no hay nada en
ninguna parte **se dice**: un botón que no hace nada y no explica por qué se lee
como un botón roto. La decisión vive en `ui/delete_scope.h`, fuera de la ventana,
porque es la parte que se puede equivocar y la única comprobable sin abrir un
diálogo modal. Sin pieza abierta **no se menciona Ctrl+Z**, y no es un olvido: la
pila de deshacer guarda las herramientas de la pieza abierta, y ahí no hay
ninguna que devolver.

**«Mis herramientas», agrupadas por clase.** Petición: *«que las separases por la
herramienta en cuestión que se está usando, y luego que se desglose todas las
veces que se usó esa herramienta»*. La pestaña pasa a tres niveles —la clase,
cada uso, y todo lo que la figura de ese uso puede medir—. La fila de la clase
lleva su resumen (*«2 cotas NO cumplen»*) porque agrupar esconde lo de dentro, y
sin resumen habría que abrir las siete para saber si hay algo rojo, que es justo
el trabajo que agrupar tenía que ahorrar. El orden de las clases es el de la
**primera aparición**, no alfabético: así se parece al orden en que el operador
fue dibujando.

> **Un fallo que introdujo este cambio, cazado por su propia prueba.** Los
> interruptores se emparejaban con las herramientas **por posición** en el
> vector. Funcionó mientras la pestaña era una lista plana; al agrupar, las
> casillas se crean en otro orden y apagar «Ø» guardaba **«alto»** — se apagaba
> una cota distinta de la que tocaste, sin decir nada. Ahora cada casilla lleva
> el índice de su herramienta. Un emparejamiento implícito que depende del orden
> de pintado es una bomba de relojería.

**Con el pincel puesto no se podía acercar.** Queja: *«hay algo que se siente
incómodo al momento de usar los pinceles»*. Una parte medible de ese algo: con el
pincel encendido la rueda cambiaba **solo** su tamaño y no quedaba ninguna forma
de hacer zoom. Perfilar un borde a mano es justo cuando más falta hace acercarse,
y para hacerlo había que apagar el pincel, mover la rueda y volver a encenderlo:
tres gestos para uno. **Ctrl+rueda** acerca sin apagar nada, y funciona igual con
el pincel apagado para que el gesto no dependa del modo — uno que solo vale a
veces se acaba no usando.

#### Recuperar lo que el brillo se lleva

Queja de uso: *«tengo una tuerca, con reflejos, brillo, sombras, y eso afecta a
la medición y la forma en que toma los bordes»*. Literal, y medido: **tres
tornillos cincados salían como CINCO manchas y un tornillo galvanizado como
DOS**.

Un corte de gris supone que la pieza cae **entera** de un lado. Sobre metal es
falso: el reflejo especular sube un trozo de la propia cara de la pieza hasta el
nivel del fondo, el corte lo deja fuera, y la silueta sale mordida o partida.

**La solución: cortar dos veces.** El corte de siempre da las SEMILLAS —lo que es
pieza con seguridad—; un corte aflojado doce niveles dice hasta dónde PODRÍA
llegar; y se conserva solo lo aflojado que **toque una semilla**. Es la
histéresis de Canny llevada del gradiente al nivel de gris.

Y ahí está el porqué de que no deje entrar el fondo: **la mesa aflojada tampoco
toca ninguna semilla**, porque las semillas son pieza. Sube el brillo pegado a la
cara de la pieza y no la sombra pegada a la mesa.

| imagen | verdad | antes | con esto |
|---|---|---|---|
| tres tornillos cincados | 3 | **5** ✗ | **3** ✓ |
| un tornillo galvanizado | 1 | **2** ✗ | **1** ✓ |
| bandeja de cien tuercas | 100 | 100 | 100 |
| un engranaje | 1 | 1 | 1 |
| dos engranajes engranados | 2 | 1 | 1 (se tocan: otro problema) |

Error total de recuento sobre las siete: **4 → 1**, y lo que queda es la pareja
de engranajes pegados, que es trabajo de `splitTouchingPieces`.

**Doce niveles y no más**: con treinta, la bandeja de cien tuercas se funde en 64
y los tres tornillos en uno. La reconstrucción usa `connectedComponents` y no
dilataciones iteradas porque una pieza larga necesitaría decenas de pasadas para
que la semilla llegue a la punta.

**Lo que se probó antes y NO funcionó**, para que nadie lo repita:

- **Aplanar la luz dividiendo por un desenfoque grande.** Empeoró en las siete
  imágenes —un engranaje pasó de 0,8 % de vaivén a 105,2 % y de 1 pieza a 14—.
  Estas escenas tienen fondo blanco UNIFORME: no hay degradado que quitar, y el
  desenfoque se traga la propia pieza y la divide consigo misma. Código
  eliminado, no aparcado.
- **Recortar los reflejos por percentil alto.** Es un **no-op** aquí y se
  comprobó dos veces: como la pieza es OSCURA sobre fondo claro, el percentil 98
  recorta el fondo, no los brillos. Apuntaba al lado equivocado del histograma.
- **Umbral adaptativo local.** Arregla los tres tornillos (5→3) pero rompe otros
  dos casos (un tornillo pasa a 6 trozos, otro a 2): responde a la textura de la
  rosca.
- **Black-hat morfológico** ayuda en unos y hace añicos un engranaje (1 → 18
  piezas: separa los dientes). **CLAHE** baja el vaivén de unos y dobla el del
  perno cromado. **Apertura en gris con kernel grande** parece ganar
  espectacularmente y es falso: funde las cien tuercas en una.

#### El consejo de «por el canto» que nunca podía darse

Petición de uso: *«si puedes mejorar la detección de bordes, debido a reflejos,
fondo, etc.»*. Lo que apareció al medir no fue que faltara maquinaria: estaba
toda —el método por canto, el consejero `edgeSegmentationLooksBetter`, el aviso
en el panel de detección y hasta un botón para cambiarse—. **Lo que fallaba era
la puerta.**

El aviso se enseñaba solo si las piezas *cabalgaban* el fondo: partes más claras
Y más oscuras que la mesa, cada lado por encima del 1 % del encuadre. Pero «más
claro que el fondo» se cuenta por encima de `fondo + banda`, con la banda a 12
como mínimo. **Con la mesa en 255, ese techo cae en 267, que ningún píxel de 8
bits alcanza.** La cuenta de claros salía `0,00 %` siempre, la condición era
falsa por construcción, y el botón no aparecía jamás sobre fondo blanco — que es
el montaje industrial normal.

Medido sobre las ocho imágenes reales: **fondo entre 244 y 255 en las ocho, techo
fuera de rango en las ocho.** Y `edgeSegmentationLooksBetter` no se llamaba desde
ningún sitio fuera de su propio fichero.

**Un cero que quiere decir «ciego» y un cero que quiere decir «nada» no pueden
compartir casilla.** Ahora `SceneReading::brightSideIsUnmeasurable` lo dice, y el
resumen se lo cuenta al operador en vez de callárselo.

**El segundo motivo por el que un corte único falla.** Cabalgar no es la única
forma: el corte también puede pasar **por dentro** de la pieza. Eso ya lo medía
`checkThresholdClipping` —afloja el umbral 12 niveles y mira cuánto se mueve la
silueta— y nadie lo consultaba al elegir método. Sobre las ocho imágenes separa
limpiamente:

| imagen | verdad | por nivel | por canto | vaivén |
|---|---|---|---|---|
| tres tornillos cincados | 3 | **5** ✗ | 3 ✓ | 36,8 % |
| un tornillo galvanizado | 1 | **2** ✗ | 1 ✓ | 17,3 % |
| bandeja de cien tuercas | 100 | 100 ✓ | **10** ✗ | 4,6 % |
| las otras cinco | — | ✓ | — | ≤ 5,5 % |

O sea que **el canto no es mejor: es para otra escena**, y lo que hacía falta era
distinguirlas.

**Dos listones, no uno.** El primer intento reusó el 10 % con el que se avisa del
recorte, y se llevó por delante una prueba que existía: la bola oscura sobre
blanco, donde el nivel va bien y salía recomendado el canto. Al mirarla, el
motivo: **la foto lleva una regla de acero además de la bola**, y lo que recorta
es la regla. Vaivén 10,8 %, justo al filo.

No es el mismo listón porque no es la misma pregunta. *«¿El corte está mordiendo
la pieza?»* es una advertencia: barata de atender, y pasarse por exceso solo
cuesta que el operador mire. *«¿Conviene cambiar de método?»* empuja a una
decisión que cambia **todas** las medidas de esa pieza, y ofrecer el método
equivocado es peor que no ofrecer ninguno, porque el operador se fía.

`kSwingWorthChangingMethod` = **15 %**, en mitad del hueco entre 10,8 % y 17,3 %.
Con eso, las **diez** imágenes disponibles salen bien. Pero hay **un solo caso a
cada lado del hueco**: la separación es limpia en lo que hay y la muestra es
corta, así que si aparecen más fotos esto es lo primero que hay que volver a
mirar.

El caso de la bola enseña además el límite de la señal: **mide el encuadre
entero**, así que un objeto brillante que no es la pieza cuenta igual. Por eso
conviene que el listón esté alto.

#### Piezas metálicas buscadas a propósito

Petición de uso: *«busca en internet imágenes que cumplan con esos problemas para
medirlos, como tuercas, tornillos, engranajes, piezas»*. El corpus tenía bolas
cerámicas y una tuerca mate: **nada que reflejara de verdad**, que es justo donde
el umbral por nivel se rompe.

Se descargaron nueve de Wikimedia Commons y **se quedaron tres**. Las seis
descartadas conviene nombrarlas: un montaje de tres fotos en un fichero, dos
primeros planos que desbordan el encuadre, una instantánea de una mano sujetando
un mecanismo y dos arandelas cortadas por los cuatro lados. Ninguna es una escena
de inspección, y medir sobre ellas daría números que no significan nada.

| imagen | verdad | por nivel | por canto | consejo |
|---|---|---|---|---|
| conjunto cromado con arandela | 1 | **4** ✗ | 2 | CANTO ✓ |
| diez tornillos y tuercas pegados | 10 | **2** ✗ | **2** ✗ | nivel |
| pieza clara sobre fondo texturizado | 1 | 2 | falla | CANTO |

**El conjunto cromado es el caso que la petición describía**, y ahí el consejo
acierta: el corte por nivel parte la pieza en cuatro trozos y el canto la deja en
dos. Es un positivo nuevo, y con él el listón del 15 % pasa a tener dos casos
claros a favor en vez de uno.

**Las diez piezas pegadas son un LÍMITE, y están en el corpus como límite.** Ni
el nivel ni el canto se acercan a diez: lo que falla ahí no es el nivel de gris
sino que las piezas se tocan, y eso es otro problema con otra herramienta
(`splitTouchingPieces`). Un corpus que solo guarda lo que sale bien deja de
avisar de nada.

**La pieza clara sobre fondo texturizado prueba otra cosa**: que el canto FALLE
DICIÉNDOLO. No cierra ningún contorno y lo explica —«no queda ninguna pieza,
prueba con el umbral por nivel»— en vez de devolver una máscara vacía.

**Un error propio que casi se cuela.** La primera medida de los candidatos daba
«nivel 3, canto 9» sobre las diez piezas pegadas, y parecía un contraejemplo
limpio del consejo. Era falso: la sonda descartaba manchas por debajo de **200
px² fijos**, y sobre una foto de 1920×1285 eso es polvo. Con un mínimo relativo
al encuadre la misma escena da **2 y 2**. Un recuento cuyo umbral no escala con
el tamaño de la foto no compara nada — la sonda quedó corregida, y las medidas
de las ocho imágenes del usuario se rehicieron para comprobar que la decisión del
15 % no dependía de ese defecto. No dependía.

**Una hipótesis que se cayó al medirla.** La explicación natural era que los
reflejos saturan a 255 y se confunden con la mesa. Es falsa: los dos tornillos
son justo los que **menos** saturan (0,5 % y 1,5 % de su superficie), y la
bandeja —donde el nivel acierta— es la que más. Lo que rompe el corte no es la
saturación sino que el umbral cae sobre gris que es material.

**Y el consejo se frena en el tiempo.** `readScene` corría por fotograma con el
panel abierto cuando costaba «un desenfoque y dos comparaciones». Ahora arrastra
el comprobador de recorte, que segmenta dos veces: **13,1 ms medidos, el 39 % del
presupuesto de un fotograma a 30 Hz**, justo mientras el operador mueve la luz
para ver el efecto. Se lee una vez por segundo, porque lo que describe es la
ILUMINACIÓN y eso cambia en segundos, no en fotogramas.

Un efecto colateral: `checkThresholdClipping` llamaba a `readScene` para leer un
solo campo. Al hacer que `readScene` consulte el recorte, eso pasaba a ser
recursión infinita — se rompió tomando el nivel del fondo directamente, que
además es el trabajo que de verdad hacía falta.

Un tornillo largo tiene la cabeza y el vástago lo bastante distintos como para
parecer dos corazones. Por eso es una **opción** y no el comportamiento de fábrica —
la misma decisión que con «por el canto» y por la misma razón: gana en unas escenas y
pierde en otras, así que la elige quien conoce sus piezas. Cuesta entre 3 y 16 ms.

**Hasta dónde llega, medido.** La pregunta útil no es «si se tocan» sino «cuánto se
solapan». Con discos de 260 px de diámetro:

| solape | 0 px | 20 px | 35 px | 50 px | 80 px |
|---|---|---|---|---|---|
| piezas | 2 ✓ | 2 ✓ | 2 ✓ | 1 | 1 |

Aguanta el 13 % y se rinde con el 19 %; **entre esos dos valores no se ha
medido**, así que el límite exacto no se conoce. Y **rendirse ahí es lo correcto**: con medio
disco dentro del otro el cuello es tan ancho como las propias piezas y ninguna
técnica basada en la forma puede saber dónde acaba una. Lo que importa es que se
rinda devolviendo **una** pieza y no inventando tres.

El barrido del umbral también se midió: **0,55** es el único valor que aguanta los 35
píxeles de solape, y no cuesta nada en los demás casos. Por debajo los corazones
crecen y se funden antes; por encima se quedan tan pequeños que los engranajes vuelven
a contar como uno.

### Un residuo de cero no dice «no aplica»: dice «ajuste exacto»

La cabecera de `ShapeClass::deviation` promete que es «el número con el que se
decidió», y añade una frase que vale como regla: **«una clasificación sin su residuo
es una opinión»**. La rama de polígonos redondeados lo ponía a `0.0` a mano.

No es cosmético: es la lectura **contraria** a la verdadera. Quien lee «polígono
redondeado(5), desviación 0,00 px» entiende que el contorno encaja perfectamente en
ese modelo, cuando lo que pasa es que nadie midió si encajaba. Y esa etiqueta es de
las más discutibles del clasificador —salía en engranajes y tornillos, que no son
pentágonos de nada— así que es justo donde más falta hace el residuo para NO fiarse.

El número sí estaba disponible: cada primitiva del contorno trae su `rmsResidual`. Se
publica el **peor**, igual que hacen las otras dos ramas — no la media, que
escondería un tramo malo entre veinte buenos. Sobre las piezas reales pasa de 0,00 a
1,74 / 1,22 / 1,10 / 1,06 / 1,01 px.

**Y el mismo fallo un nivel más abajo**: el texto se rotulaba con `round0`, que
redondea a entero, así que un residuo de 0,093 px se escribía «se separa 0 px» — la
misma impresión engañosa que se acababa de quitar del número. Da igual medir bien si
luego se rotula a cero. Los tres sitios que rotulan un RESIDUO pasan a dos decimales
por debajo de 10 px; los que rotulan diámetros y porcentajes se quedan en entero,
donde el detalle no aporta.

### La misma cota dando 22,61 y 227,81 px, las dos marcadas OK

El calibre elige entre pares de bordes de polaridad opuesta maximizando
`min(|fuerza_a|, |fuerza_b|)`. Ese criterio **no mira dónde están los bordes**, sólo
cuánto marcan. Cuando la línea cruza dos rasgos —la silueta de la pieza y un taladro
dentro— hay dos pares válidos, y si puntean parecido **cuál gana lo decide el ruido**.

Medido sobre una tuerca real, desplazando la imagen fracciones de píxel (lo que hace
cualquier cámara por vibración o deriva térmica):

| corrimiento | medida | ¿ok? |
|---|---|---|
| 0,00 px | 22,61 | SÍ |
| 0,25 px | **227,81** | SÍ |
| 0,50 px | 22,65 | SÍ |
| 0,75 px | **227,78** | SÍ |

No es deriva: es un **biestable que salta un factor diez con cuarto de píxel**, y las
cuatro lecturas salían con `ok = true`. En producción esa cota alternaría entre dos
valores en fotogramas consecutivos, y una pieza buena daría NG cada dos ciclos sin
nada que lo explicara.

**Lo que NO se puede hacer** es elegir mejor. La silueta y el taladro son las dos
cotas legítimas y sólo el operador sabe cuál quiso trazar; preferir siempre la más
ancha rompería a quien mide una ranura interior. **Lo que SÍ se puede** es saber que
la lectura no se sostiene.

#### La inestabilidad se mide, no se infiere

La primera versión la **deducía**: guardaba el segundo mejor par y marcaba ambiguo
cuando los dos punteaban parecido y medían cosas distintas. Sonaba razonable. Medido
sobre las fotos reales con 136 colocaciones de calibre:

| criterio | rechaza | falsas alarmas | se le escapan |
|---|---|---|---|
| deducido (dos pares parejos) | **72 (53 %)** | **61** | 3 |
| medido (correr la línea y mirar) | **14 (10 %)** | — | — |

El deducido era **peor en las dos direcciones**: rechazaba más de la mitad de lo que
antes se aceptaba —y un aviso que salta una de cada dos veces se apaga en una semana—
y además se le escapaban tres lecturas que sí saltaban.

Lo que hace ahora: repetir la medida con la línea corrida un tercio y dos tercios de
píxel a lo largo de su propia dirección, y mirar si la respuesta salta más del 10 %.
Eso marca exactamente las lecturas que cambiarían de un fotograma a otro.

**El coste, medido** y no supuesto —que es la tercera afirmación de esta tanda que
hubo que ir a comprobar—: diez calibres CON el sondeo cuestan 1,33 ms frente a los
5,38 ms del análisis que los alimenta (**24,8 %**) en una imagen pequeña, y 0,41 ms
frente a 65,47 ms (**0,6 %**) en la bandeja de cien piezas. Las herramientas siguen
siendo una fracción pequeña del análisis, así que el sondeo no marca el ritmo de
nada.

Y el aviso mejoró de paso: en vez de nombrar dos candidatas deducidas, dice **el
rango real entre el que oscila** («da entre 22,6 y 227,8 px»), que está medido.

**Y dos correcciones de mí mismo por el camino**, las dos por leer mal mi propia
sonda:

- Creí ver lecturas de **0,00 px marcadas OK** y añadí una guarda de span mínimo. Era
  falso: esos casos ya los rechazaba el control de «se necesitan 2 bordes», y mi sonda
  los etiquetaba mal porque sólo buscaba la palabra del aviso nuevo. Comprobado que la
  guarda **no es alcanzable** —ni con un rasgo de 1 px, que el suavizado separa a
  3,00— y retirada.
- La prueba sintética de «barra con hueco» dejó de saltar con el criterio medido, **y
  con razón**: en una figura de rellenos planos la lectura da 85,00 px y no se mueve.
  La inestabilidad necesita el continuo de grises de una foto; forzarla en un
  `fillRect` sería ajustar la escena hasta que la prueba pase. La prueba se mudó a la
  tuerca real.

Comprobado que **no salta con lo que ya funcionaba**: una barra sola sigue midiendo
140,00 px y sigue dando OK a cinco alturas distintas.


#### Lo que queda sin tocar, y por qué

`runRegion` **rebinariza con su propio Otsu dentro del recuadro** (verificado leyendo
`tool_executor.cpp`). Como Otsu sobre un recorte pequeño ve un histograma distinto al
del encuadre entero, la silueta que mide no es la del pipeline. De ahí salen **dos**
discrepancias medidas, no una:

**El área**: entre −10,6 % y **−37,5 %** según la imagen. La misma aplicación da dos
áreas para la misma pieza.

**El recuento de agujeros**, que falla en las dos direcciones a la vez:

| imagen | a la vista | ≥12 px² | ≥40 px² | ≥250 px² |
|---|---|---|---|---|
| un tornillo solo | **0** | 111 | 103 | **29** |
| tres tornillos | **0** | 24 | 24 | 4 |
| un engranaje | **11** | **2** | 2 | 2 |

Cuenta 29 agujeros en un tornillo que **no tiene ninguno** —son los valles de la
rosca, que se cierran como regiones dentro de la silueta— y encuentra 2 de los 11 que
sí tiene un engranaje.

**Y no es cuestión de afinar el umbral.** Se probó filtrar por FORMA, que sería lo
razonable —un taladro es redondo y un valle de rosca es una tira—: con circularidad
≥ 0,75 los tornillos siguen dando 13, 15 y 11 agujeros inexistentes mientras el
engranaje se queda en 1. Ni el área ni la forma los separan.

##### Lo que se creía que era, y no era

Esta nota decía que la raíz de las dos discrepancias era la misma —que la Región
deriva todo por su cuenta en vez de usar `pieceMaskWithHoles`, «que sí recupera
los agujeros de verdad»—. **Al medirlo, no.** `pieceMaskWithHoles` recupera
agujeros: recupera ciento diecisiete.

Cuatro medidas, hechas al buscar la mejor opción:

**1. No es la Región: son diez herramientas.** `runEdgeFlaw`, `runFillet`,
`runChamfer`, `runExtremes`, `runProfile`, `runBoltPattern`, `runClearance`,
`runPolygon`, `runSymmetry` y `runRegion` rebinarizan cada una su recorte con su
propio Otsu. Cualquier cambio de ese patrón es sistémico, no un parche.

**2. El desacuerdo de área va en las DOS direcciones**, y es mayor de lo que esta
nota decía:

| imagen | contorno | región | desacuerdo | agujeros que cuenta |
|---|---|---|---|---|
| tuerca (1 agujero) | 18 095 | 22 252 | **+23,0 %** | 23 |
| arandelas | 366 922 | 343 757 | −6,3 % | 215 |
| perno cromado | 79 590 | 63 037 | **−20,8 %** | 67 |
| piñón (1 agujero) | 573 495 | 871 386 | **+51,9 %** | 77 |

**3. «Otsu inventa cuando la ventana es uniforme» — hipótesis falsa.** Parecía la
explicación natural: Otsu siempre devuelve un corte, tenga la ventana dos
poblaciones o una. Su separabilidad (varianza entre clases / varianza total)
debería desplomarse en el segundo caso. **No se desploma**: 0,617–0,866 en
recortes con pieza y fondo, 0,690–0,742 en recortes tomados enteros dentro de la
pieza. Se solapan, y el interior de la tuerca puntúa *más alto* que su propio
recorte honrado. El metal mecanizado tiene contraste real por dentro, así que ahí
no hay señal que aprovechar.

**4. Y la que importa: LA SEGMENTACIÓN DEL PROGRAMA TAMPOCO CUENTA BIEN.** Medido
por el camino exacto del botón *Medir pieza* —`analyzeFrame` →
`pieceMaskWithHoles` → `measureWholePiece`—, el informe que lee el operador dice:

| imagen | agujeros de verdad | lo que dice el informe |
|---|---|---|
| moneda de 5 yenes | **1** | **117** |
| «tuerca» | *no es una tuerca* — ver abajo | 5 |
| «piñón» | *no es un piñón* — ver abajo | 19 |

> **CORRECCIÓN.** La primera versión de esta nota daba las tres como defecto,
> con «un agujero» de verdad en las tres. **Dos de esas tres verdades eran
> falsas, y se pusieron sin abrir las fotos.** Al mirarlas:
>
> - `tuerca_dominio_publico.jpg` son **siete** tuercas y racores distintos. La
>   pieza mayor es una brida con cuatro taladros más el central: **sus cinco
>   agujeros eran correctos** y se denunciaron como fallo.
> - `pinon_corona_dentada.jpg` es un **montón de piñones solapados** llenando el
>   encuadre, no un piñón con su eje. Diecinueve no es obviamente un error.
> - `arandelas_con_agujero.jpg`, usada en las medidas de área, es una **bolsa con
>   etiqueta impresa en ruso**: la figura que se mide es el texto y sus
>   «agujeros» son las letras.
>
> Queda **un solo caso verificado**: la moneda, que es una moneda con un agujero
> y sale con ciento diecisiete. El defecto es real; el tamaño de la muestra, no.

##### La mejor opción, con la evidencia delante

**Son dos problemas, no uno, y conviene no mezclarlos.**

**(a) El recuento de agujeros se arregla EN SU ORIGEN, no en la Región.** El
número equivocado ya está en la pantalla del operador —una moneda con 117
agujeros— y sale de `describeContour`, que no filtra nada. Arreglarlo ahí arregla
el informe y de paso deja a las diez herramientas algo correcto que mirar.
Arreglarlo solo dentro de la Región dejaría el informe mintiendo.

**¿Con qué criterio?** La primera respuesta —«un filtro por tamaño no vale, al 1 %
salen 2, 4 y 0 agujeros»— se apoyaba en las verdades de campo falsas y no se
sostiene. Con el único caso verificado delante, el filtro por tamaño **sí
separa**, y con holgura:

| moneda de 5 yenes | área del hueco | % de la figura |
|---|---|---|
| el agujero de verdad | 110 595 px² | **16,40 %** |
| el siguiente mayor | 6 174 px² | 0,92 % |

Casi **dieciocho veces** entre el agujero y el mayor de los impostores: un
mínimo del 3 % deja exactamente uno. Lo que NO se puede decir es que ese 3 %
valga en general — está ajustado sobre **una sola imagen verificada**, y elegir
un umbral con un caso es lo mismo que fabricar la respuesta.

Lo que hace falta antes de tocar `describeContour` son **más piezas con agujeros
contados a mano**: una arandela de verdad, una brida, una tuerca sola. Sin eso,
cualquier número que se ponga aquí es una corazonada con aspecto de medida.

Ventaja práctica: **nadie pierde nada**. No hay tolerancia declarada sobre «117
agujeros» que hoy funcione, así que corregirlo no rompe ninguna medida buena.

**(b) El desacuerdo de área es otra cosa, y esa sí es decisión del usuario.**
Viene de binarizar local en vez de global. Es defendible como diseño —una Región
mide *lo que hay dentro del recuadro*, y eso permite medir un rasgo interior que
la silueta global no ve— y cambiarlo movería medidas ya guardadas hasta un 52 %,
con sus tolerancias. **Y mandar las herramientas a la silueta del programa no
arreglaría los agujeros** (medida 4), así que ni siquiera es un dos por uno.

Orden recomendado: **(a) primero**, que es una mentira visible y sin coste; **(b)
después y solo si el usuario lo pide**, que cambia números que alguien pudo haber
declarado.


### El fallo que no falla: medir corto y no decir nada

Es el peor que puede tener una aplicación de medida. No lanza, no avisa, y devuelve
un número creíble. Medido sobre las fotos reales del usuario: el umbral automático
se come la cabeza cromada de un tornillo y le quita el **36 %** del área. El aviso de
contorno sucio **no salta** —está en 3,0 y esa pieza mide 2,14— y no puede saltar:
un contorno recortado es perfectamente limpio. **No hay nada sucio; hay pieza que
falta.**

**La señal**: aflojar el umbral unos niveles HACIA el fondo y mirar cuánta pieza
aparece. Con una pieza bien separada, entre ella y la mesa hay un desierto de grises
y aflojar no encuentra nada. Si aparece mucha, es que había masa de pieza pegada al
corte — o sea, que el corte caía **dentro** de la pieza y no en su borde.

Lo que la hace utilizable en producción: **no necesita la verdad**. No se compara con
el área buena, que nadie conoce; se compara la imagen consigo misma.

| imagen | vuelco | veredicto |
|---|---|---|
| engranaje-1 | +5,7 % | correcto |
| engranajes-1 | +5,5 % | correcto (su problema es que se tocan) |
| tornillo-1 | +2,5 % | correcto |
| tuerca suelta | +3,9 % | correcto |
| bandeja de 100 | +4,6 % | correcto |
| **tornillo-2** | **+15,4 %** | **CORTA** — le faltaba el 32 % del área |
| **tornillos-1** | **+23,8 %** | **CORTA** — le faltaba el 36 % del área |

Todo lo correcto por debajo del 6 %, todo lo cortado por encima del 15 %. El umbral
se pone en el **10 %**, en medio del hueco y no pegado a ninguno de los dos lados.

**Se probó una versión barata** —solo umbralizar y contar píxeles, sin pasar por el
pipeline— que cuesta menos de 1 ms. **No separa**: 11,9 % en una imagen correcta
contra 13,3 % en una cortada. El filtrado por área y la morfología del pipeline son
lo que quita el ruido que confunde la señal, así que hay que pagarlos: dos análisis
completos, 60 ms con cien piezas. Por eso va **a petición** y no por fotograma.

#### Y una escena que no se pudo construir

La mitad «que el aviso SÍ salte» se comprueba sobre las fotos reales y **no** con una
escena dibujada. Se intentó tres veces:

1. Cabeza plana puesta a ojo → vuelco de **−100 %**. No reproducía un recorte:
   reproducía una escena rota. Y la aserción de entonces —que el resumen no estuviera
   vacío— la daba por buena.
2. Cabeza plana con el nivel buscado **iterando contra el propio Otsu** → 0,0 %. Con
   un histograma de dos picos y nada en medio, Otsu cae SOBRE uno de los picos y no
   existe ningún nivel que quede «a doce del corte».
3. Cabeza con **degradado** de brillo, que es lo que tiene una superficie curva y
   pulida → **+5,3 %**. Dirección correcta, pero no llega al 10 %.

Para que llegara habría que agrandar la cabeza hasta que el área recuperada pesara lo
bastante — y eso ya es **ajustar la escena hasta que la prueba pase**, que es fabricar
la respuesta. Una prueba así no demuestra que el aviso funcione: demuestra que se le
encontró una entrada a medida. Las fotos reales no tienen ese problema: el recorte
está medido de forma independiente —32 % y 36 %— y el aviso acierta contra esa verdad.

### El afinado subpíxel se ignoraba en cuanto había dos piezas

El afinado vivía en `analyzeFrame`, el camino de UNA pieza. `analyzeFrames` —el de
varias— pasa por `analyzePiece`, que no lo hacía, así que el ajuste se ignoraba **en
silencio** con más de una pieza en el encuadre.

Lo grave no es el hueco: es que ese ajuste **abre un diálogo** avisando de que «las
medidas de la pieza cambian a partir de ahora» y pidiendo revisar las tolerancias. El
operador revisaba sus tolerancias contra un cambio que en su bandeja no se había
producido — y no tenía forma de notarlo, porque el aviso sí salía.

Y desde que el modo automático mide TODAS las piezas, ese camino pasó de ser la
excepción a ser el normal: el arreglo del recuento automático volvió **alcanzable**
un hueco que llevaba tiempo ahí sin molestar.

**Coste medido** sobre la bandeja real de cien tuercas: 58,09 ms sin afinar contra
84,21 ms afinando — **+45 %**. Se paga porque el operador lo ha pedido expresamente y
el diálogo le ha dicho lo que cambia; la alternativa anterior era cobrarle cero y no
darle nada.

### Escenas dibujadas a partir de sus cotas

Las fotos reales dicen cómo se **comporta** el programa; solo una imagen construida
dice si el número es **correcto**. En una foto de un engranaje no se sabe cuánto mide
de verdad su agujero: se puede medir dos veces y comprobar que coinciden, pero no se
puede afirmar que el resultado sea el bueno.

`tests/synthetic_scenes.h` le da la vuelta: **la cota va primero y la imagen se dibuja
a partir de ella**, así que la verdad de campo es exacta por construcción. Eso es lo
que permite escribir «esto tiene que dar 62,5 mm» en vez de «esto tiene que dar lo
mismo que la vez anterior».

Las tres escenas reproducen las piezas reales del usuario porque cada una rompe el
pipeline por un sitio distinto:

| Escena | Qué rompe | Resultado medido |
|---|---|---|
| Tablero 8×8 acotado a 500 mm | la escala | calibra a **1,041667 mm/px**, idéntico al de dibujo; casilla **62,5000 mm** |
| Ídem, esquinas internas | la rejilla | paso **60,000 px**, peor desvío **0,000 px** |
| Engranaje con agujero y 28 dientes | agujeros y dentado | área 59 383 px²; el agujero son 3 632 (**6,8 %** del cuerpo) |
| Tres tornillos de 320/440/560 px | recuento y orden | 3 de 3, izquierda a derecha, alturas exactas |
| Ídem con escala | la cadena entera | **45,71 / 62,86 / 80,00 mm**, clavados |

Todo se dibuja con `cv::LINE_8` (sin antialias) a propósito: con bordes suavizados el
recuento de píxeles depende del umbral y deja de ser exacto, que es justo lo que se
viene a evitar.

**El área del engranaje incluye su agujero**, y conviene saberlo: `contour.area` es el
área del contorno EXTERIOR, así que un agujero de 3 632 px² sobre un cuerpo de 53 093
no se resta. No es un fallo —es lo que significa ese campo— pero en una cota de área
con tolerancia del 5 %, un 6,8 % es la diferencia entre OK y NG. La prueba comprueba de
**qué lado cae** para que nadie cambie el significado sin enterarse.

#### Lo que las fotos reales sí dicen

La sonda de `test_nut_probe.cpp` —que informa y no falla nunca— encontró dos cosas
sobre las imágenes del usuario que ninguna figura dibujada habría destapado:

- **`engranaje-1.webp`: 2 piezas donde hay 1.** La segunda es una tira de 84×8 px
  (0,59 % del encuadre) que pasa el filtro de área mínima.
- **`engranajes-1.jpg`: 1 pieza donde hay 2.** Los dos engranajes se **tocan**, y
  `RETR_EXTERNAL` los devuelve como un solo blob de 407×210. Además se pierde la
  esquina superior izquierda del engranaje claro, que se funde con el fondo casi
  blanco.

Los tres tornillos salen 3 de 3. El caso de las piezas que se tocan es el único de la
tanda que la aplicación **hoy no resuelve**, y es justo el que importa en una bandeja.

### Cambiar de pieza con Configurar abierto

La ventana de Configurar es **única**: volver a pulsarla trae al frente la que ya
está abierta. Bien pensado — pero el selector de piezas vive **fuera** de ella, en
la barra de arriba, y se puede cambiar de trabajo con la ventana delante.

Lo que había dentro era entonces de la pieza **anterior**. Y «piezas esperadas»,
«ver en mosaico» y el **perfil de detección** se guardan con la pieza, así que
aceptar escribía los ajustes de la bandeja encima de la pieza suelta recién
seleccionada. Sin avisar, y sin forma de notarlo hasta que esa pieza empieza a dar
NG de recuento o su detección deja de funcionar con un umbral que nadie le puso.

No es que se pierda un ajuste: **es que se le copia a un trabajo que no es el
suyo**, que es peor — los valores son legítimos, solo que de otra escena, así que no
hay nada que parezca roto.

El orden importó: la página de Detección se pone al día **después** de cargar el
perfil de la pieza nueva, y no junto a la de Piezas. `loadMeasurementForSelectedPiece`
corre antes y deja `pipelineConfig_` y `currentProfileId_` todavía con lo viejo:
refrescar desde allí habría rellenado la página con exactamente lo que se venía a
quitar.

**Una expectativa mía que no era una promesa del programa.** La primera versión de
la prueba exigía que al pasar a una pieza sin perfil cambiara también el umbral.
No: sin perfil «todo sigue como antes» es el diseño documentado —un perfil es un
*override*— y la página estaba enseñando la verdad. Queda escrito para no
«arreglar» dos veces algo que no está roto. Lo que sí hay que proteger, y lo que
protege ahora, es que **el perfil no se contagie**.

### Abrir Configurar y aceptar sin tocar nada

Hay una familia de fallos que encaja con «el menú de configuración tiene bastantes
fallos en sus características» y que **no se ve mirando la pantalla**: un valor que
la ventana enseña pero no devuelve, o que devuelve distinto de como lo recibió.

El síntoma es de los peores. Entras a cambiar el umbral, aceptas, y de paso se te
ha movido la polaridad o el área mínima. Nadie lo relaciona con la ventana de
ajustes; se nota semanas después como «la detección va peor desde hace un tiempo».

La comprobación va en dos alturas, porque un campo puede caerse en cualquiera:

- **Por página**: se construye con valores distintos de los de fábrica en TODOS los
  campos y se leen de vuelta. Con los de fábrica puestos, un campo perdido daría el
  mismo resultado que uno conservado y la prueba pasaría sin comprobar nada.
- **De extremo a extremo**: ventana real, abrir Configurar, emitir `applied()` sin
  tocar un solo control, y comparar los ajustes guardados **clave a clave**. Los
  dos tramos que las pruebas de página no ven —la ventana rellenando los `Inputs`
  y la ventana leyendo la página para aplicar— solo se cubren aquí.

La comparación es clave a clave a propósito: un «no son iguales» a secas obligaría
a investigarlo desde cero. Mutando el aplicado para que pierda la polaridad, la
prueba dice literalmente «aceptar sin tocar nada cambió det_polarity: era 1 y quedó
0».

**Lo que encontró**: la sensibilidad de anomalía tenía el campo a **un decimal**.
Un valor que llegara con más precisión —importando la configuración de otro puesto,
que copia los ajustes verbatim— se redondeaba **al abrir la ventana**: 2,75 pasaba
a 2,8 al aceptar, sin tocarlo nadie. El paso sigue siendo 0,1, que es como se
ajusta a mano; los dos decimales son para no estropear lo que ya había.

### La misma decisión de unidad, tomada nueve veces

Para añadir pulgadas había que tocar nueve sitios, y los nueve tenían copiada la
misma línea:

```cpp
useCm = unit == LengthUnit::Centimeters || (unit == LengthUnit::Auto && mm >= 100.0);
```

**Y ya habían derivado**: unas copias escribían un decimal y otras dos, así que la
misma medida salía «12,3 mm» en el lienzo y «12,34 mm» en el informe. Nadie decide
eso mal a propósito; se decide nueve veces y basta con que una se quede atrás. Es
exactamente la incoherencia de la que se queja el operador, y era la razón para
centralizar antes de añadir nada: nueve sitios son nueve oportunidades de olvidar
uno, y un sitio olvidado en una aplicación de medida no da un error — da un número
con aspecto correcto y la unidad equivocada.

Ahora hay **una** función —`pickLength` / `pickArea` en `tool_executor`— que
devuelve el valor convertido, su sufijo y con cuántos decimales tiene sentido
escribirlo. Los decimales son parte de la decisión y no un detalle del sitio que
imprime: una pulgada son 25,4 mm, así que con dos decimales el último dígito
valdría un cuarto de milímetro — resolución escondida, que el operador no tiene
forma de detectar.

Centralizar destapó de paso **dónde estaba la copia atrasada**: el lienzo era el
único sitio que ignoraba el modo Automático para áreas, así que la misma pieza
salía «17506 mm²» en el vídeo y «175,06 cm²» en el informe. Había una prueba que
exigía «mm²» a secas: estaba **fijando la inconsistencia**. Se reescribió para
comprobar lo que importa —que el número sea correcto en la unidad que se enseñe,
con la escala al cuadrado y no lineal— y que pedir milímetros dé milímetros.

`Inches` va **al final** del `enum`: el valor se guarda como entero en los ajustes,
así que meterlo en medio le cambiaría la unidad a quien ya tenía una elegida. Por lo
mismo, el menú ahora busca su acción **por valor y no por posición en la lista**:
eran lo mismo mientras las dos listas coincidieran, y basta insertar una entrada
para que dejen de hacerlo.

### Elegir centímetros y que calibrar te hable en milímetros

Queja literal: «si está relacionado la opción de ver las medidas en cm, y entras y
lo ves en mm». Lo estaba y no lo estaba. La escala se guarda en **mm/px** por
dentro —eso no cambia y no debe cambiar, es el dato— pero enseñársela en
milímetros a quien ha pedido centímetros le obliga a convertir de cabeza **justo en
la pantalla donde una conversión mal hecha estropea todas las cotas de la pieza**.

El diálogo recibe ahora la unidad elegida y rotula con ella: escala en cm/px,
distancia de cámara en cm, el ejemplo en cm. Y la longitud de referencia se puede
dar en mm, cm, m o pulgadas, con su selector al lado del campo.

### Apagar una ayuda del pincel y que volviera encendida

Las tres ayudas del pincel se guardan al pulsarlas y se recuperan al arrancar.
Leído, el código parece correcto — y no lo era.

El «recuperar» era `action->setChecked(guardado)`, confiando en que `toggled`
llevara el valor al lienzo. Una `QAction` empieza **sin marcar**, así que con un
«apagado» guardado, `setChecked(false)` sobre algo que ya está en `false` **no
emite nada** y el lienzo se quedaba con su valor de fábrica.

«Pulso estable» viene de fábrica **encendido**. El operador lo apagaba,
reiniciaba, y el menú se lo enseñaba apagado mientras el pincel lo seguía
aplicando. No es que se olvide el ajuste: es que **la pantalla afirma una cosa y
el programa hace otra**, y no hay forma de descubrirlo salvo notando que el trazo
no obedece. Es la queja de «no se guarda la configuración anterior» escondida
detrás de un código que parece guardarla.

La regla que queda: **recuperar un ajuste no puede depender de una señal que solo
salta al cambiar**. El valor se empuja al destino siempre, haya cambiado el
control o no.

**Y había un segundo caso, peor.** «Mostrar contorno» lleva un comentario diciendo
que era la única capa del menú Ver que no se recordaba y que ya se arregló. El menú
sí lo recuerda; el lienzo no se enteraba, por dos motivos encadenados: el
`setChecked(false)` mudo, **y** que el `connect` que lleva el valor al lienzo se
hacía DESPUÉS del `setChecked`, así que aunque emitiera no había nadie escuchando.
Como el lienzo trae el contorno visible de fábrica, el menú decía «oculto» y el
contorno se seguía pintando encima del vídeo.

Los demás conmutadores que empujan estado al lienzo —realce de vista, corrección
de lente, tablero, regla— ya lo hacían bien: empujan el valor justo después de
marcar la casilla. Eran estos dos.

Se buscó comparando **qué claves de ajustes se escriben y cuáles se leen**. Ese
cruce da falsos positivos —las claves que se pasan por variable no salen— pero
obliga a mirar cada una, y fue mirando esta cuando apareció.

### Lo que el programa no decía de sí mismo

Queja: «los menús están toscos, no son intuitivos ni coherentes; debería decirle al
usuario qué hace cada cosa». Eso se mide en vez de opinarlo, y salió esto:

- **25 de las 40 entradas de menú** no explicaban nada. 62 %.
- Y algo que no se nota leyendo el código: **Qt no enseña las ayudas de los menús**
  salvo que se pida con `setToolTipsVisible`, y nadie lo había pedido. Las quince
  que SÍ estaban escritas tampoco se veían. En el código están, bien redactadas, y
  parece que el trabajo está hecho; el operador ve lo mismo que si no hubiera nada.
- Los botones de los diálogos salían **«OK», «Close» y «Apply»** en una aplicación
  entera en español: `QDialogButtonBox` usa la traducción de Qt y el paquete de
  MSYS2 viene sin `share/qt6/translations`. Un comentario del código daba por hecho
  que saldrían «con el texto de su idioma». Se ponen a mano en un solo sitio: esto
  se despliega copiando, y un `.qm` que falte en la PC de la línea fallaría en
  silencio volviendo al inglés.
- Y la aplicación **mandaba a menús que no existen**: «Cámara ▸ Calibrar…» cuando
  ese menú se llama Fuente y la calibración vive en Medida. Peor que en el manual,
  porque estos textos salen justo cuando el operador está atascado.

Las explicaciones de menú van **juntas en un sitio** (`explainMenus`) y no
repartidas por los veinticinco puntos de construcción: así se leen de una vez y se
ve si dos se pisan o si una dice lo contrario que otra, que es de dónde sale la
sensación de incoherencia.

**El recuento de Configurar mintió primero.** Decía «18 controles, 0 sin explicar»
porque se saltaba los que no tienen nombre accesible — y los campos numéricos casi
nunca lo tienen, su rótulo lo pone el formulario al lado. O sea que se saltaba
justo los controles donde equivocarse cuesta más caro. Contando de verdad son 27, y
aparecieron la **polaridad** y el **número de piezas esperadas** sin una palabra.

### Cien piezas no se revisan con una flecha

Con varias piezas en el encuadre, el selector `◀ ▶` de la barra inferior recorre
las piezas en orden de lectura y sirve perfectamente para dos o tres. Con una
bandeja llena deja de servir, y no por lento: **nadie pulsa una flecha cien
veces**. Y mirarlas en el vídeo tampoco es una opción — cada tuerca ocupa unos
ochenta píxeles en pantalla y no hay forma de ver si a una le falta un canto.

`PieceMosaic` (`src/ui/piece_mosaic.{h,cpp}`) recorta cada pieza y las pone en
cuadrícula, todas al mismo tamaño y con su número. Tres decisiones lo sostienen:

- **Come de lo mismo que el vídeo.** Los contornos ya los calculó el análisis;
  volver a segmentar para pintar un panel sería pagar dos veces por la misma
  respuesta y, peor, arriesgarse a que las dos no coincidieran: el operador
  elegiría la pieza 3 del mosaico y se le mediría otra.
- **El recorte sale de `analysedFrame_`, no de `lastFrame_`.** El análisis va por
  detrás del vídeo: cuando termina, el último frame de la cámara ya es otro. Los
  contornos son del frame que se analizó, así que el recorte tiene que serlo
  también o saldría descuadrado. Por eso `maybeStartAnalysis` guarda una copia.
- **Pulsar una baldosa es ELEGIRLA**, el mismo enfoque que mueven las flechas, no
  un estado aparte del panel. Si fueran dos cosas distintas habría dos «piezas
  actuales» y ninguna de las dos sería de fiar.

**Cuesta 12,3 ms reconstruirlo** con la bandeja real de cien tuercas del usuario:
cien recortes, cien escalados y cien pasadas de `QPainter`. **Con el panel cerrado
no se pinta**: gastar eso en algo que nadie ve no se justifica, y al reabrirlo se
vuelve a analizar para que no reaparezca con lo de antes.

**La prueba de rendimiento falló primero por ser absoluta.** Exigía «menos de
40 ms»: sola daba 12,3 y pasaba, y con la máquina corriendo la suite entera en
paralelo se iba por encima y fallaba sin que nada estuviera roto. Una guarda que
grita cuando no debe se acaba ignorando, y entonces no protege de nada — la misma
lección que con el guard del manual, que dio cinco rutas «rotas» que estaban bien.

Ahora **el patrón se mide en el mismo proceso**: repintar el panel tiene que costar
menos que el análisis que lo alimenta. Sale al **31 %**, y esa afirmación sigue
siendo válida en una máquina lenta, en una rápida y bajo carga, porque las dos
mitades se miden en las mismas condiciones. Y dice algo más útil que un umbral:
mientras se cumpla, el mosaico no puede ser lo que marca el ritmo del vídeo.


Con **una sola pieza no enseña nada**: el vídeo ya la da entera y más grande. Un
panel que ocupa sitio para repetir lo que ya se ve enseña a cerrarlo — y entonces
tampoco estará el día que haya cien. Por lo mismo se ofrece **solo la primera vez**
que aparecen varias: después manda el operador, y se recupera desde
*Ver ▸ Piezas del encuadre (mosaico)*.

La pieza elegida, además, **se remarca en el vídeo** (halo y contorno a 1,8×)
mientras las demás bajan a un tono apagado. El engrosamiento solo aparece cuando
la elección es del operador: si la pieza le ha tocado por ser la mayor,
destacarla afirmaría una decisión que nadie tomó.

#### Las cotas se quedaban sobre la primera pieza

`EditorCanvas::setFocusedPiece` existía desde C6 y **no lo llamaba nadie**. Decide
de qué pieza se escriben los números —las marcas se dibujan de todas, pero
treinta etiquetas sobre seis piezas no se leen— y como valía 0 y nadie lo movía,
eran siempre las de la primera en orden de lectura.

Mientras las medidas en vivo no llevaban número de pieza esto era inofensivo:
todas valían 0 y todas se escribían. Deja de serlo en cuanto el operador puede
enfocar la tercera — la pieza se remarca en verde y **las cifras se quedan encima
de otra**, que es peor que no enseñar cotas: parecen las de la pieza señalada.

Ahora el camino en vivo pone `pieceIndex` con la misma convención que el motor
—posición en orden de lectura— y la ventana mueve el enfoque del lienzo con la
pieza medida. Dos convenciones para el mismo campo son dos formas de pintar sobre
la pieza equivocada.

**La primera versión de la prueba no valía.** Contaba píxeles de etiqueta y
exigía que hubiera algunos con la pieza 1 enfocada y algunos con la 3. Mutando el
filtro a «siempre la pieza 0» salían 276 píxeles en los dos casos y la prueba
pasaba: la etiqueta que se pintaba era la misma, sólo que la comprobación no
miraba **dónde**. Con la x media de la tinta —228 contra 501— la mutación falla.

#### El informe contaba las piezas de otra forma que la pantalla

Al mirar si el mosaico y el motor se ponían de acuerdo apareció que **el informe
y la pantalla no lo estaban**, y llevaba así desde que la lista pasó a orden de
lectura. El motor analiza aparte **la mayor** y mide las demás después; a esas les
ponía `pieceIndex = 1, 2, 3…` según iban saliendo del filtro, y a la mayor un 0
que además se traducía en «no le pongas número».

Con tres barras y la estrecha la primera leyendo, el veredicto decía **«ancho
(pieza 2) fuera de tolerancia»** de la que en pantalla es la **1**. El operador va
a la segunda barra, la encuentra perfecta, y a partir de ahí no vuelve a fiarse ni
del informe ni del panel. Es el mismo fallo silencioso contra el que se acababa de
poner un guard entre mosaico y motor, un piso más abajo.

Ahora `pieceIndex` **es** la posición en orden de lectura, la mayor incluida, y el
sufijo «(pieza N)» se pone cuando **hay varias** y no cuando el índice no es cero
— con la condición vieja, la pieza 1 habría sido la única sin numerar, justo
cuando hace falta saber cuál es.

La prueba que había comprobaba que los índices fueran `{0,1,2}`: un **conjunto**,
no un reparto, y eso lo cumple cualquier permutación. La nueva fija qué pieza
lleva qué número con una escena donde las dos numeraciones dan resultados
distintos — la estrecha es la primera leyendo y no es la mayor.

**Lo que no se puede arreglar**: las filas de historial escritas antes de esto
llevan la numeración vieja. El frame ya no está, así que no hay forma de
recalcularlas, y reescribirlas a ciegas sería inventar. Quedan como están.

#### El guard del manual daba por buena cualquier ruta

Añadir la entrada de menú destapó un agujero en `test_readme_paths.cpp`. La
comprobación aceptaba una ruta si **cualquiera** de los dos nombres era prefijo
del otro. Con acciones cortas en el menú —«Piezas», «Ver»— eso da por buena
toda ruta que empiece por esa palabra. Se vio mutando: se cambió el nombre de la
entrada recién añadida a algo completamente distinto y el guard siguió en verde,
diciendo «21 rutas comprobadas, 0 rotas».

Dejando solo el sentido «el nombre real EMPIEZA por lo que dice el manual» —y
quitando los puntos suspensivos de los **dos** lados, no solo del manual— la
mutación falla como debe. De paso salieron dos cosas de verdad rotas que llevaban
tiempo escondidas: una ruta ArUco partida con un «ArUco» suelto colgando y la
línea que la contenía. Es la **segunda** vez que este guard se destapa por
añadirle una ruta nueva y mirar el recuento; la primera fue el salto de línea que
se comía 7 de 20.

### El tablero global era una promesa a medias

La regla estaba escrita: el ajuste global del tablero es **solo la plantilla para
piezas nuevas**; con una pieza seleccionada mandan sus columnas. Está bien
pensada —cada pieza puede necesitar un cero distinto— y no era el fallo que
parecía al ver que la pieza pisa el valor global nada más arrancar.

Lo que no se cumplía era la otra mitad. Una pieza recién creada se quedaba con
los valores por defecto del **esquema** (`bounds`, sin giro, sin desfase), no con
los del operador, así que configurar el tablero sin ninguna pieza seleccionada
—que es el único momento en que ese ajuste global se puede tocar— no servía
absolutamente para nada. Ahora la pieza nueva hereda ese tablero en los dos
caminos que crean piezas.

---
