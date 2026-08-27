# Cómo funciona por dentro

Recorrido técnico del programa, pieza por pieza: qué hace cada módulo, **cómo
se llama la técnica que usa**, por qué se eligió y dónde están sus límites.
Para el flujo de uso, ver el [README](README.md).

Índice:

1. [Mapa del código](#1-mapa-del-código)
2. [Captura de cámara](#2-captura-de-cámara)
3. [Detección de la pieza](#3-detección-de-la-pieza-visión-clásica)
4. [Sistema de coordenadas de la pieza](#4-sistema-de-coordenadas-de-la-pieza-position-fixture)
5. [Herramientas de medición](#5-herramientas-de-medición)
6. [Tablero de referencia y modos de medición](#6-tablero-de-referencia-y-modos-de-medición)
7. [Escala real: de píxeles a milímetros](#7-escala-real-de-píxeles-a-milímetros)
8. [Apariencia: el modelo y la detección de anomalías](#8-apariencia-el-modelo-y-la-detección-de-anomalías)
9. [Veredicto](#9-veredicto)
10. [Persistencia](#10-persistencia)
11. [La interfaz](#11-la-interfaz)
12. [Empaquetado](#12-empaquetado)
13. [Glosario: cómo se llama cada técnica](#13-glosario-cómo-se-llama-cada-técnica)
14. [Cómo mejorarlo](#14-cómo-mejorarlo)

Los defectos que aparecieron en uso, con lo que se midió al buscarlos, están
aparte en [BITACORA.md](BITACORA.md). Vivían dentro del capítulo 10 y eran
1 191 de sus 1 414 líneas.

---

## 1. Mapa del código

Diez módulos en capas. **La dependencia siempre baja**, nunca sube ni se cruza:

| Módulo | Qué contiene | De qué depende |
|---|---|---|
| `core/` | `Result<T>`, log, contador de FPS, blindaje anti-caída | nada |
| `domain/` | Veredicto, calidad de captura, calibración, modos de medición | `core` (**sin Qt ni OpenCV**) |
| `camera/` | Enumeración nativa, hilo de captura, controles de la fuente | `core`, Qt, OpenCV |
| `vision/` | Segmentación, contorno, fixture, estabilizador, tablero, escala, ajustes geométricos y periodicidad | `core`, `domain`, OpenCV |
| `ml/` | Extractor de embeddings ONNX, referencia estadística | `core` |
| `database/` | SQLite envuelto en RAII, esquema y migraciones | `core` |
| `inspection_editor/` | Geometrías, ejecutor de herramientas, perfiles, medición automática, lienzo | `vision`, `core`, Qt |
| `repositories/` | Puente entre dominio y base de datos | `database`, `domain`, `ml`, `vision` |
| `engine/` | Orquestador: registro e inspección completa | todos los anteriores |
| `ui/` | Ventana principal y diálogos | todo |

Dos reglas que sostienen el diseño:

- **`domain/` no conoce Qt ni OpenCV.** Ahí vive lo que decide (veredictos,
  tolerancias, criterios de calidad), y por eso se puede probar sin imagen ni
  ventana.
- **Nada lanza excepciones a través de la frontera de un módulo.** Todo devuelve
  `core::Result<T>`, que obliga al llamador a mirar el error. Las excepciones de
  OpenCV se atrapan donde ocurren.

---

## 2. Captura: la cámara y los ficheros

### Una imagen es una fuente, no un modo

La observación que ordena esta parte: **`MainWindow` cuelga entero de una
señal.** Llega un `QImage`, y todo lo que hay detrás —segmentar, contorno,
fixture, herramientas, zona de trabajo, recuento, medición automática,
inspección— ya trabaja sobre ese `QImage` **sin preguntar de dónde salió**. Lo
único atado a la cámara es quién produce el frame.

Así que «que lo del vídeo funcione sobre una imagen» no es un modo nuevo ni una
pantalla nueva: es una **fuente** más (`camera/frame_source.h`,
`camera/file_sources.*`). Hacerlo como modo aparte daría dos caminos que
divergen, y este proyecto ya pagó eso una vez con los botones y otra con las
tres paletas.

El desplegable de cámaras pasa a ofrecer también **Abrir imagen…** y **Abrir
vídeo…**, y hay un efecto secundario que vale más que la función pedida: antes,
**sin cámara la aplicación era inservible** —el botón Iniciar salía
deshabilitado y no se podía ni ajustar la detección ni preparar una plantilla—.
Ahora se abre una imagen y funciona todo.

Tres decisiones que no son evidentes:

- **La imagen REEMITE su frame** cuatro veces por segundo en vez de una sola.
  Media aplicación reacciona a «llegó un frame nuevo», así que emitir una vez
  dejaría la pantalla congelada en cuanto el operador tocara un ajuste de
  detección. Y a ritmo bajo porque reanalizar treinta veces por segundo una
  imagen que no cambia es quemar CPU para nada.
- **El vídeo va en su propio hilo** y en bucle, como la cámara: la regla de esta
  capa es que nada bloquea la interfaz, y descodificar 1080p en el hilo de la UI
  la dejaría a tirones.
- **El índice de cámara viaja en el DATO del elemento del combo, no en su
  posición.** En cuanto se añaden dos entradas al final, cualquier código que
  asumiera «posición del combo == índice en `cameras_`» apunta a otra cosa sin
  avisar. Este proyecto ya pagó ese precio una vez con las pestañas de
  Configurar.

`CameraController` **no** implementa la interfaz común, y es deliberado: tiene
controles, resolución y perfil de exposición que un fichero no puede prometer, y
forzar una interfaz común obligaría a rellenar esos huecos con métodos vacíos.
Lo que comparten —lo único que la ventana necesita— es que llega un frame.
`capabilitiesOf()` responde qué se puede hacer con cada fuente, para que la
interfaz deshabilite **con motivo** en vez de repartir `if (esCámara)`.

#### Lo que no aplica se dice, y se dice bien

La pestaña de cámara ya caía en un sustituto cuando no había controles que
sondear, pero decía **«Inicia la cámara»** — y eso, con una imagen abierta,
manda al operador a hacer algo que ya hizo y le deja pensando que la aplicación
no se entera. Son dos motivos distintos para el mismo hueco y ahora se
distinguen: con la cámara parada, «arráncala»; con un fichero, qué es la fuente
y **qué sigue funcionando igual** —porque un panel que solo dice «esto no se
puede» deja al operador creyendo que ha perdido la aplicación entera.

Lo mismo con dos textos que habían dejado de ser ciertos:

- El primer consejo de una instalación nueva empezaba por **«enfoca la pieza»**.
  Sobre una fotografía eso es imposible, y un asistente que pide lo imposible se
  deja de leer entero. El paso sigue siendo calibrar; lo que cambia es el
  consejo.
- El aviso de calibración obsoleta decía **«otra cámara»**. La escala px→mm
  depende de la óptica y de la distancia al plano, y un fichero no garantiza
  ninguna de las dos, así que ahora lo dice tal cual.

#### La foto: congelar el frame y trabajar sobre él

Con el vídeo en vivo la pieza tiembla y la segmentación late, así que dibujar una
herramienta encima es puntería. **Capturar foto** congela el frame actual y a
partir de ahí se traza, se calibra y se mide con calma.

Dos decisiones que no son evidentes:

- **La cámara no se cierra, solo se deja de escuchar.** Volver al vídeo cuesta
  cero, y sobre todo no hay que resondear controles ni relanzar el perfil de
  exposición — que además cambiaría la imagen, justo lo que no se quiere de una
  foto.
- **`SourceKind::Photo` es un tipo distinto de `Image`, y la razón es la
  calibración.** Una foto sale de esta cámara, con esta óptica y a esta distancia
  del plano, así que los mm/px siguen valiendo exactamente igual que en vivo; un
  fichero no garantiza ninguna de las tres. Tratarlas igual obligaría a elegir
  entre dos errores: avisar de «calibración obsoleta» cada vez que alguien
  congela —un aviso que se aprende a ignorar en dos días— o callarse también al
  abrir un fichero, que es cuando de verdad hay que avisar.

#### La barra de fuente, auditada

El hallazgo que más importaba era de **orientación**: tras abrir `pieza.png` el
desplegable seguía diciendo «Abrir imagen…», así que no había dónde leer con qué
se estaba trabajando. Y aquí eso pesa el doble, porque recalibrar, comparar y
registrar dependen de con qué imagen se está. Ahora el fichero abierto **entra
en la lista con su nombre** y sale al cerrar (buscado por su dato, no por su
índice: entre abrir y cerrar puede haberse reenumerado).

Lo demás fue nomenclatura que se había quedado atrás: el rótulo y el menú decían
«Cámara» cuando ya no describen lo que hay (ahora **Fuente**), el botón decía
«Iniciar» cuando lo siguiente que iba a hacer era abrir un diálogo de fichero
(ahora **Abrir…**), y el indicador de estado decía «Cám» en verde mientras se
analizaba una fotografía — exacto en el color y falso en la palabra, que es
justo lo que no puede pasar cuando el color no debe cargar solo con el
significado. Ahora dice **Cám / Img / Víd**. Y el diálogo de fichero vuelve a la
última carpeta usada, porque quien revisa casos abre diez de la misma.

### Corregir el borde a mano

Cuando la segmentación se equivoca en UNA foto —una sombra pegada a un lado, un
reflejo que parte la pieza— hay dos salidas posibles y llevan a herramientas
distintas: afinar la detección, o corregir esa imagen. Esto es lo segundo, y la
distinción se dice en la propia interfaz: *«la corrección vale para esta imagen;
si tienes que corregir siempre lo mismo, lo que hay que ajustar es la
detección»*.

`PipelineConfig` lleva dos máscaras —`forcePiece` y `forceBackground`— que se
aplican **justo después de segmentar**, en el mismo punto donde la zona libre
recorta y por la misma razón: pintar sobre la IMAGEN metería bordes artificiales
que la segmentación leería como contornos de verdad; sobre la máscara, lo
marcado simplemente cuenta o deja de contar.

Dos máscaras y no una con tres estados porque así cada una dice UNA cosa. El
orden de aplicación es el del pincel —primero añadir, después quitar— para que
marcar fondo sobre algo recién marcado como pieza gane lo ÚLTIMO que hizo el
operador; si ganara lo primero, corregirse una pincelada sería imposible.

Y al pintar, el color contrario se **borra** en el mismo sitio. Sin eso, el mismo
píxel quedaría marcado como pieza y como fondo a la vez, y el resultado
dependería del orden en que se aplicaran — exactamente la clase de estado que
nadie puede razonar.

**Solo con imagen quieta.** En vídeo en vivo el contorno se recalcula en cada
frame, así que un borde corregido a mano sería mentira en cuanto la pieza se
moviera un píxel. El botón está apagado con su motivo, y el pincel se apaga solo
al volver al vídeo — dejarlo encendido haría que el siguiente clic pintara sin
que nadie lo pidiera.

Medido: una sombra que se comía el borde derecho dejaba la pieza en 180 px de
ancho; pintando ese trozo como pieza vuelve a 240, que es el real.

#### El pincel pintaba a puntos

Lo destapó el test, y es de los que no se ven probando a ojo. Se pintaba un
círculo por cada evento del ratón, y el ratón **no emite un evento por píxel**:
un trazo de 100 px con radio 20 marcaba 2.514 px —dos manchas con el medio sin
tocar— en vez de los 5.357 de un trazo continuo. Pintando despacio parecía
funcionar perfectamente; a poco que se moviera rápido, habría dejado huecos y
habría que repasar. Ahora se une punto con punto, y el trazo nuevo no se enlaza
con el anterior.

La corrección se emite **al soltar**, no en cada punto: reanalizar la imagen por
cada píxel de la pincelada dejaría el pincel a tirones.

#### «Solo detecta una»: seis piezas y la ventana callada

El recuento existía y funcionaba. Lo que no existía era **verlo**: sólo se
encendía si la pieza declaraba esperar varias o si el operador tenía abierta la
pestaña *Piezas*, y sólo se mostraba DENTRO de ese diálogo. Con los ajustes de
fábrica y seis piezas en el encuadre, la aplicación medía la mayor en silencio.

La justificación de no contar siempre era el coste. Medido sobre 1920×1080 con
seis piezas: **7,5 ms** quedarse con la mayor, **11,2 ms** contarlas todas. Son
3,7 ms de los 33 que dura un frame a 30 fps — no es un coste, es ruido.

Lo que sí era real es la otra mitad: contar **suelta el recorte automático**,
que rodea a la pieza mayor y por construcción daría siempre uno. Por eso la
regla nueva es: se cuenta siempre SALVO con la zona automática activa, donde se
respeta la regla anterior y hay que pedirlo.

Y el recuento sale a la ventana principal, junto al modo de medición. Se
**destaca** sólo con más de una pieza, porque es el único caso en que cambia lo
que hay que hacer: las herramientas miden la mayor y las demás quedan sin medir.
Un aviso que salta siempre deja de ser un aviso.

#### Una zona en píxeles sin su resolución no significa nada

La zona de trabajo se guardaba en píxeles y **no a qué resolución se dibujó**.
Dentro de una misma sesión eso ya se corregía al cambiar de resolución; lo que
faltaba era el arranque, donde no hay resolución anterior con la que comparar
porque el programa acaba de abrirse.

El resultado, medido: una zona dibujada sobre 480×320 que rodeaba la pieza
pequeña, reabierta con una fuente de 240×160, se recorta contra el frame y deja
de rodear nada — **no se detecta ninguna pieza y nadie dice por qué**. Con la
resolución de referencia guardada, la misma zona pasa de x=330 ancho 79 a x=165
ancho 39: la mitad exacta, la misma pieza.

Y no es sólo la zona. El **cero del tablero** es otro punto en coordenadas de
imagen, y quien tuviera puesto un cero y ninguna zona sufría el mismo fallo sin
que nada lo cubriera: el origen de todas las medidas de Posición, corrido y sin
avisar. Medido: un cero en (300, 150) sobre 480×320 pasa a (150, 75) sobre
240×160. Por eso la referencia la escribe una función propia,
`persistPixelReference`, llamada desde los **dos** sitios que guardan píxeles —
estaba sólo en el de la zona, y con un cero de tablero a solas no se guardaba
referencia alguna: el arreglo no podía ni dispararse.

La referencia se actualiza a la resolución nueva al reajustar. Sin eso, el
siguiente arranque volvería a reajustar desde la vieja y lo movería una segunda
vez.

**Cuidado con `QSize::isValid()`.** En Qt, `QSize(0, 0).isValid()` es `true` —
sólo exige que no sean negativos. La primera versión de esta comprobación usaba
`isValid()` y por eso no saltaba nunca: un ajuste ausente se lee como cero y
pasaba por bueno. Lo que hace falta es `isEmpty()`.

#### Precision: el borde no es un escalon, es una rampa de 15 pixeles

Tres formas de medir el MISMO diametro de la bola de 10 mm no coincidian: el
largo daba 253,4 px, el ancho 245,4 y la circunferencia ajustada 250,8. Un
3,2 % de desacuerdo entre tres numeros que describen la misma cosa.

La repetibilidad no era el problema: mover la ventana de trabajo un pixel deja
la medida **exactamente igual** (0,000 % de deriva). Lo que falla es donde se
pone el borde. Medido sobre la foto: la intensidad pasa de 33 a 240 a lo largo
de **15 pixeles**, y el radio del contorno variaba entre 118,6 y 129,0 px sobre
la misma bola. Con una rampa asi, un umbral duro coloca el borde en cualquier
punto de esos quince segun la iluminacion.

`refineContourSubpixel` hace lo que un calibre optico: para cada punto del
contorno mira el perfil de intensidad **a lo largo de su normal**, saca el nivel
de dentro y el de fuera EN ESE PUNTO, y coloca el borde donde el perfil cruza la
mitad entre los dos, interpolando. Que los niveles sean **locales** es la mitad
del asunto: una pieza con una cara iluminada y otra en sombra tiene dos umbrales
correctos distintos, y ningun valor global puede ser los dos a la vez.

Medido sobre un borde colocado a proposito en una posicion fraccionaria, donde
la respuesta se conoce exacta:

| radio verdadero 60,50 px | medido | error |
|---|---|---|
| umbral duro | 60,083 | 0,417 px |
| **subpixel** | 60,475 | **0,025 px** |

Diecisiete veces mas exacto. Sobre la bola real la ganancia es mucho menor
(4,9 % menos dispersion de radios) y la razon esta medida: el contorno de esa
bola **no es circular de verdad** — la sombra suave de abajo y el reflejo
especular lo deforman. El afinado coloca bien el borde que hay; no puede
arreglar que ese borde incluya sombra.

**El suavizado del contorno, y por que hizo falta.** Cada punto se afina por su
cuenta a lo largo de su normal, asi que dos vecinos pueden quedar en zigzag. El
area apenas lo nota —es una integral— pero el perimetro suma cada zigzag. Y
debajo de eso hay un problema mas viejo: el efecto **escalera**, que hace que
sumar pasos entre pixeles enteros sobreestime la longitud de una curva suave.
Medido: el radio deducido del perimetro salia un **6,75 %** mayor que el
deducido del area sobre la misma pieza. Con el suavizado baja a **1,75 %**.

Y el suavizado **solo se aplica si el afinado encontro bordes de verdad**. Si no
se afino nada es que no habia borde visible, y mover el contorno seria modificar
datos sin ninguna prueba a favor. La garantia de que en el peor caso esto NO
HACE NADA vale mas que un perimetro algo mejor: sobre una imagen plana, cero
puntos movidos.

**Los cruces ambiguos, y hasta donde llega arreglarlos.** El punto malo tenia
causa fisica: donde hay un reflejo especular, el perfil de intensidad cruza el
nivel medio **dos veces** —se entra en la pieza, se sale al reflejo, se vuelve a
entrar— y elegir uno de los dos cruces es adivinar. Rechazarlos bajo el peor
punto de **10,68 a 6,35 px** y el desacuerdo area/perimetro de 3,06 % a 2,00 %.

Se probo un segundo filtro —descartar los puntos que no se parecen a sus
vecinos— y **no se quedo**: bajaba el peor punto de 6,346 a 6,332 px. Catorce
milesimas de pixel no pagan un filtro mas que mantener.

Y ahi esta el limite, que resulto no ser del algoritmo. El error residual es
**direccional**: hay un sector del contorno que se va hacia fuera y el opuesto
hacia dentro, con **6,48 px** entre ellos. Esa asimetria es la firma de una luz
que viene de un lado — la mascara incluye la sombra pegada a la pieza, y ahi hay
un borde de verdad: el de la sombra, no el de la pieza. Ningun afinado arregla
eso, porque no es un problema de resolucion. Lo arregla la iluminacion difusa o
el pincel de corregir el borde.

**Y una leccion de metodo que costo un test fallado.** La primera version fijaba
QUE sector se va hacia fuera, apoyada en un analisis rapido en Python que decia
que 39 de los 40 peores puntos caian donde la sombra. El test fallo: ese analisis
usaba Otsu **crudo**, y el pipeline aplica antes suavizado y morfologia, lo que
mueve que sector domina. Medir por fuera del camino que usa el programa mide
**otra cosa**. Lo que se afirma ahora es solo lo que sobrevivio: que el error es
direccional. Cual sea el lado depende del preprocesado y no merece un aserto.

**Donde SI se usa y donde NO, con el numero delante.** El afinado alimenta el
area y el perimetro, que son integrales sobre todo el contorno y por tanto
robustas a un punto malo. **No** alimenta la clasificacion de figura, y eso se
decidio midiendo: con el afinado enchufado ahi, la bola pasaba de «circulo» a
«irregular».

La razon esta en como juzga `classifyShape`: `worstRadialDeviation` es un
**maximo**, no una media. Basta un punto mal colocado para cambiar el veredicto.
Y el afinado mejora la media a costa del peor punto, porque donde hay un reflejo
especular el perfil de intensidad cruza el nivel medio **dos veces** y ese punto
se coloca en el cruce equivocado. Medido sobre la bola:

| desviacion radial | umbral | subpixel |
|---|---|---|
| media | 2,078 px | **1,807 px** |
| peor punto | 5,032 px | **10,677 px** |

Parte la media por la mitad y **duplica el peor punto**. Una medida mas fina que
hace fallar la clasificacion es peor que la de antes, asi que no se enchufa. El
parametro de `classifyShape` se queda —probado y documentado— porque es la
costura por donde entrara el dia que el afinado sepa descartar los cruces
ambiguos. Hoy no lo sabe, y hay un test que lo dice.

**Enchufado como OPCION, y apagada.** No por prudencia generica: encenderlo
cambia donde esta el borde, y con el el area, el perimetro y toda medida que
salga del contorno. Una pieza YA REGISTRADA tiene sus tolerancias ajustadas
contra el borde de antes — cambiar la definicion por debajo moveria todas sus
cotas a la vez, y una pieza buena empezaria a salir NG por un cambio de
definicion y no por un defecto. Quien lo encienda tiene que volver a mirar sus
tolerancias, y por eso es una decision y no un arreglo silencioso.

Con la opcion apagada el resultado es identico al de antes de que el afinado
existiera, y hay un test que lo fija con los numeros exactos. `pci_probe
--subpixel` permite comparar las dos versiones sobre el material de cada cual
antes de decidir.

**Y el suavizado va acotado a 1 px, que lo destapo una tuerca hexagonal.** Sin
tope, el suavizado movia un punto hasta **19,6 px** — mucho mas de lo que el
propio afinado tiene permitido. No estaba quitando ruido: estaba **redondeando
las esquinas del hexagono**, que son la pieza y no el muestreo. La regla que lo
separa no necesita detectar esquinas: el ruido es pequeno por definicion, asi
que lo que pida moverse mas que eso es un rasgo y se respeta. Con el tope, el
punto que mas se mueve en esa tuerca lo hace 5,65 px.

El precio se pago a proposito, y resultó ser menor de lo que costo al principio:
la contradiccion entre area y perimetro subio de 1,75 % a 3,06 %, y al rechazar
despues los cruces ambiguos volvio a bajar hasta **2,00 %** (desde el 6,75 % de
partida). Vale la pena: un numero algo peor en una pieza redonda a cambio de no deformar las que tienen esquinas.

#### Las cifras que este documento afirma tienen quien las vigile

Un numero medido y copiado a mano a un documento **empieza a caducar el mismo
dia**. Este documento tenia uno: decia que acotar el suavizado del contorno
subia la contradiccion entre area y perimetro «de 1,75 % a 3,06 %». Era cierto
cuando se escribio; despues se anadio el rechazo de cruces ambiguos y la cifra
real bajo a **2,00 %**. El documento se quedo **exagerando el precio de una
decision**, y quien lo leyera podria revisitar esa decision por un motivo que ya
no existia.

Lo destapo un repaso deliberado: sacar todas las cifras que el documento afirma,
sacar todas las que imprimen los tests, y cruzarlas. De 31 cifras comprobadas,
12 no las producia ningun test. La mayoria con razon —describen el estado ANTES
de un arreglo y su valor es historico— pero dos estaban simplemente obsoletas.

`test_documented_numbers.cpp` ata las afirmaciones que describen **como se
comporta el programa hoy** a una comprobacion: el desacuerdo area/perimetro, la
tabla entera de dentado por fotografia, y la exactitud del afinado subpixel. Si
una deja de ser cierta, falla y dice cual y en cuanto se movio.

No estan todas, y es a proposito: las cifras historicas —«se publicaba un Ø de
130.901 px»— describen algo que ya no ocurre y atarlas a un test seria pedirle
al programa que siga fallando como fallaba.

#### Y el manual manda al operador a sitios que existen

El mismo repaso, aplicado al README, encontro **seis referencias rotas de una
vez**. El menu «Camara» se renombro a «Fuente» y sus entradas de medida se
movieron a «Medida»; el manual se quedo con los nombres viejos:

Las rutas viejas van entre comillas de codigo y no en cursiva, y no es
cosmetica: son CADENAS DE TEXTO del pasado, no sitios a los que ir. La guardia
de rutas (`tests/test_readme_paths.cpp`) lee toda la documentacion buscando
rutas en cursiva y comprobandolas contra los menus reales — con estas en
cursiva, las cuatro salian como rotas, que es correcto y a la vez inutil: estan
rotas a proposito, esa es la noticia.

| el manual decia | donde esta de verdad |
|---|---|
| `Camara ▸ Configurar…` | *Fuente ▸ Configurar…* |
| `Camara ▸ Calibrar escala` | *Medida ▸ Calibrar escala (mm)…* |
| `Camara ▸ Escala por marcador` | *Medida ▸ Escala por marcador ArUco (en vivo)* |
| `Ver ▸ Unidad` | *Medida ▸ Unidad de medida* |

Una cifra obsoleta en un documento tecnico despista a quien programa. Una ruta
de menu obsoleta en el manual **manda a quien esta midiendo piezas a buscar algo
que no esta**, y le hace concluir que el programa esta roto.

`test_readme_paths.cpp` lee las rutas del propio README y las resuelve contra
los menus, submenus, acciones y **botones** de la ventana de verdad. Los botones
hacen falta: la zona de trabajo vive en uno cuyo menu no tiene titulo, asi que
«Zona ▸ Quitar la zona» salia como rota siendo correcta.

Y obligo a arreglar una ambiguedad de notacion. El manual escribia igual una
ruta de menu y una navegacion por pestanas de un dialogo, y no son lo mismo:
«Detección» no es una accion, es una pestana de *Configurar*. Ahora se distingue
—*Fuente ▸ Configurar…*, pestana **Detección**— y el lector tampoco las confunde.

#### Material REAL: lo que una foto de verdad destapa en el primer intento

Durante meses, todo lo que este proyecto probaba eran PNG que se dibujaba a si
mismo: rectangulos planos, sin ruido, sin reflejos, sin compresion. Por eso
todos los fallos de las ultimas semanas los encontro el operador usando la
aplicacion y no la bateria de pruebas.

Una fotografia de verdad trae de una sola vez lo que las sinteticas no tienen:
reflejos especulares, sombras suaves, compresion JPEG, y sobre todo **cosas que
no son la pieza** — una regla, una barra de escala, texto.

La primera foto real destapo un fallo **en el primer intento**. Es una bola
oscura sobre fondo claro con una regla metalica al lado; la segmentacion se
queda con las marcas grabadas de la regla (son oscuras, como la bola) y el
contorno resultante es largo, delgado y casi recto. Ajustar una circunferencia a
un contorno casi recto da un circulo enorme: se publico un **Ø 130.901 px sobre
una imagen de 1920 px de ancho**. Sesenta y ocho veces mas ancha que la foto,
con un motivo que lo explicaba con toda seguridad.

La causa es que la vara era **relativa al radio ajustado** (`deviation <= radius
* 0,05`): cuanto mas absurdo el circulo, mayor su radio y mas facil pasar el
5 %. Se justificaba a si mismo — con Ø 130.901 px el margen eran 3.272 px y el
contorno se separaba 175. Ahora el diametro no puede pasar del doble de la
diagonal de la caja del propio contorno: para una circunferencia de verdad esa
razon es 0,71, y el caso de la regla estaba en **99,4**.

Y el video real cierra el hueco que mas costo: los AVI del test son intra-frame
(cualquier frame es punto de entrada, buscar siempre acierta), mientras que el
operador trabaja con H.264, donde solo los keyframes lo son. Medido sobre un MP4
real: **buscar por tiempo aterriza con 8,8 ms de desvio sobre 10.036 ms**, el
tiempo no retrocede en 454 frames seguidos, y con cuatro dados en el aire se
encuentran **hasta 5 piezas a la vez** sobre un fondo metalico que brilla tanto
como ellas.

#### Una pieza cortada por el encuadre no se puede medir

Lo que se ve de una pieza cortada es un TROZO, y todas sus cotas —ancho, alto,
area, perimetro, diametro— son **limites inferiores** de las de verdad.
Publicarlas como medidas es publicar un numero que se sabe corto.

El proyecto ya lo comprobaba, pero solo en el control de calidad **al**
**registrar** una pieza (`capture_quality`). Al MEDIR no lo miraba nadie, asi
que se podia sacar un informe entero de una pieza cortada sin un solo aviso.

Lo destapo la moneda de 5 yenes al intentar comprobar su razon nominal 5/22 =
0,2273. Medida daba **0,2578**, un 13 % de mas, y la causa no era la deteccion:
la moneda **toca el borde de arriba (20 px) y el de la izquierda (60 px)**. Su
diametro exterior esta cortado y su agujero no, asi que la razon sale inflada.
Ninguna cantidad de precision arregla eso — la informacion no esta en la foto.

El aviso dice POR QUE LADO se sale y, sobre todo, QUE PASA CON LAS MEDIDAS.
Saber que toca el borde no le sirve de nada a quien esta leyendo un ancho: lo
que necesita saber es que ese ancho se queda corto.

El margen de 2 px tampoco es mania: la morfologia y el suavizado mueven el
contorno esa distancia, y una pieza que de verdad llega al borde puede quedarse
a uno de el. Hay un test que comprueba las dos caras — que con margen 2 se
detecta y con margen 0 no — para que el valor sea una decision y no una
casualidad.

**Y los avisos viajan CON el informe, no en la ventana.** Un aviso que se queda
en la barra de estado llega a la mitad de la gente que lo necesita: la otra
mitad exporta el CSV y se lleva las cifras sin el. Por eso «esta pieza esta
cortada» y «este perimetro no es de fiar» son campos de `PieceReport`, y el
dialogo los pinta **encima de la tabla** — puesto debajo, el aviso llega cuando
las cifras ya se han leido y entonces no cambia nada. Hay un test que compara
las coordenadas en pantalla para que siga siendo asi.

Y sobre todo **viajan en el fichero**. La primera version metia los avisos en el
informe con este argumento —«un aviso que se queda en la ventana llega a la
mitad de la gente; la otra mitad exporta el CSV y se lleva las cifras sin el»— y
luego exportaba el CSV **sin los avisos**, con lo que el argumento quedaba sin
cumplir justo donde importaba.

La ventana se cierra. El fichero se guarda, se manda por correo y se abre tres
semanas despues, cuando ya nadie se acuerda de si la pieza entraba entera en el
encuadre. Los avisos salen como lineas `AVISO,"..."` **antes de la cabecera**,
separados por una linea en blanco: quien lo abra en una hoja de calculo los lee
arriba del todo, y quien lo parsee encuentra la cabecera donde estaba.

Sin avisos el fichero es **byte a byte el de antes**, y hay un test que lo fija:
quien ya tuviera una hoja apuntando a estas columnas no se entera del cambio. Y
otro comprueba que un aviso con comas y comillas —los textos reales las llevan,
«ancho, alto, area, perimetro»— no rompe el parseo.

Y sin el tamano del encuadre no se afirma nada: `measureWholePiece` sin `frame`
no puede saber si la pieza esta cortada, y entonces **no avisa** en vez de
inventarselo.

#### Lo dentado del contorno: un aviso barato que separa dos cosas

Una moneda de 5 yenes —22,0 mm de diametro y 5,0 mm de agujero, publicados—
entro en el corpus como la mejor verdad de campo que tiene: la razon 5/22 =
0,2273 se puede comprobar **sin saber cuantos pixeles son un milimetro**.

Y destapo otra cosa antes de llegar a eso. Su relieve grabado hace que la
segmentacion siga el DIBUJO de la superficie en vez del borde, y el contorno
sale con un perimetro diez veces mayor del que le tocaria. `contourRaggedness`
lo pone en un numero: **perimetro medido / perimetro de un circulo de la misma
area**. Vale 1 para un circulo, no depende de la escala, y nunca puede bajar de
1 (desigualdad isoperimetrica: de todas las figuras con un area dada, la
circunferencia es la de menor perimetro).

Medido sobre el corpus **por el camino que usa el programa**:

| imagen | dentado |
|---|---|
| bola de 20 mm | 1,59 |
| tres bolas sobre negro | 1,72 |
| tuerca hexagonal | 2,42 |
| *(banda vacia — el umbral va aqui, en 3,0)* | |
| monton de pinones | 5,88 |
| arandelas en una bolsa | 6,26 |
| moneda con relieve | 10,11 |
| bola junto a una regla | 21,96 |

El aviso dice las **dos** posibilidades —«o la pieza es muy dentada, o la
deteccion sigue el dibujo de la superficie»— en vez de acusar a una: un pinon
bien detectado pasaria de 3 con toda la razon.

**Y el umbral se recoloco por la misma leccion de ayer.** El primer valor, 2,5,
salio de medir con un script en Python; los numeros del programa son otros
porque aplica antes suavizado y morfologia. Con los suyos, la banda vacia esta
entre 2,42 y 5,88, y el umbral va en medio en vez de rozando un caso bueno.
Medir por fuera del camino del programa mide otra cosa: van dos veces.

**Dos fotos del corpus estan por lo que NO se puede medir en ellas.** Se
buscaron para probar el agujero pasante y resultaron ser otra cosa: unas
arandelas dentro de una bolsa de plastico, superpuestas y bajo una etiqueta
impresa que ocupa media imagen; y un monton de pinones amontonados en
perspectiva, sin fondo y con brillos por todas partes.

Se quedan por eso mismo. En una escena asi no hay ninguna medida correcta que
dar, y lo unico exigible a un instrumento es que **no invente una**. Las dos dan
«irregular», que es la respuesta honesta.

De ahi sale una invariante que vale para todo el corpus: **lo que no se
reconoce no publica diametro**. Si la clasificacion no identifico la figura, no
puede dar el diametro de una figura que no identifico — es la misma regla que
faltaba el dia del «Ø 130.901 px». Y el motivo nunca puede faltar: una
clasificacion sin explicacion deja al operador sin nada que revisar cuando el
resultado le extrana.

El corpus **no se versiona** (obras de terceros con su licencia, y pesan): se
versiona la receta, `testdata/fetch_real_images.py`, y las pruebas se saltan
Las HERRAMIENTAS tambien se prueban contra ese material, y ahi aparece la
unica verdad de campo fisica que tiene el proyecto: una bola de **10 mm
nominales**. Medida con las propias herramientas de la aplicacion sale en
**10,10 mm de largo y 9,79 mm de ancho**, y el clasificador de forma da Ø 250,8
px por otro camino, a un 1-2 % de la herramienta. Que dos caminos distintos
coincidan sobre una pieza cuyo tamano real se conoce es mas fuerte que acertar
una cifra suelta.

Y una anotacion de metodo, porque la trampa costo tiempo y estuvo a punto de
convertirse en un defecto reportado que no existia: **el fixture tiene que ser
el mismo al proponer y al ejecutar**. La geometria de una herramienta vive en
coordenadas de PIEZA; proponer con un fixture y ejecutar con otro deja a las
herramientas buscando el borde donde no esta, y entonces no mide ninguna. Con
el fixture cruzado, 24 de 46 propuestas "fallaban"; con el correcto, cero.
La segunda trampa de la misma familia: `runTool` devuelve un `Result`, y que
ese `Result` sea valido solo dice que la herramienta CORRIO — si midio o no lo
dice `value().ok`.

solas si no esta descargado.

#### Un informe de cierre que culpaba a quien no era

El manejador de fallos escribia siempre la misma causa: «un driver de captura
fallo al negociar formato y **dividio por cero**». La escribia tambien cuando el
codigo de excepcion era `0xC0000005` (ACCESS_VIOLATION), que es otra cosa. En el
registro de este proyecto hay **tres cierres** con ese texto y ese codigo:
cualquiera que los leyera se pondria a buscar una division que nunca ocurrio.

Ahora la causa se **deduce** de la evidencia. La explicacion del fallo conocido
de `kswdmcap.ax` no desaparece: se condiciona a que el codigo sea de division
por cero. Y con una violacion de acceso se dice lo que esa excepcion **trae** y
antes se tiraba: si fue leyendo, escribiendo o ejecutando, en que direccion, y
si esa direccion es un puntero nulo.

Lo mas util es el **modulo**: `GetModuleHandleEx` sobre la direccion que fallo
dice a que DLL pertenece. Si es un `.ax` de captura, la culpa del driver deja de
ser una hipotesis; si es `pc_inspector.exe`, es de este codigo y no de ningun
driver.

La redaccion se separo en `describeCrash`, que toma unos `CrashFacts` y escribe
en un buffer dado. Asi el texto **se puede probar sin matar un proceso**, que es
la razon por la que llevaba tanto tiempo mintiendo: solo se veia cuando ya no
habia nadie mirando.

**Y enumerar pasa a estar blindado.** Los tres cierres registrados dicen «ultima
operacion: (ninguna)», que es justo lo que se ve cuando el proceso muere antes de
que nadie deje una miga de pan — y enumerar es lo primero que hace la aplicacion.
No abrir el pin de captura evito el fallo de division por cero, pero
`BindToStorage` sigue tocando el dispositivo para leer su bolsa de propiedades, y
eso puede cargar la DLL del fabricante. Ahora va con `runProtected` y con su miga
de pan: lo peor que pasa es quedarse sin lista de camaras, y la aplicacion
arranca igual para trabajar con imagenes y videos.

#### Lo que el propio registro de cierres senalaba

De los tres cierres registrados, dos dicen «ultima operacion: (ninguna)» y el
tercero dice **«midiendo los fps de cada exposicion»**. Ese tercero es una pista
literal: nombra la llamada dentro de la cual murio el proceso.

Y esa llamada dejaba su miga de pan pero **no iba dentro del blindaje**. Igual
el sondeo de resoluciones. Los dos hacen lo mismo que abrir la camara —pedirle
al driver que negocie formato, exposicion o tamano— y solo abrir estaba
protegido. Ahora los tres lo estan.

El precio, escrito para que nadie lo descubra por las malas: `runProtected` sale
por `longjmp`, que **no ejecuta destructores**, asi que un fallo a mitad del
barrido pierde lo que esa llamada hubiera reservado. Se paga con gusto: el
camino alternativo es que muera el proceso, donde se pierde exactamente lo mismo
y ademas todo lo demas.

Los tests lo ejercitan con una camara de mentira cuyo driver revienta a la
tercera llamada: el barrido muere, el proceso no, y el siguiente barrido con una
camara sana vuelve a funcionar. Que el test siga corriendo despues es la
demostracion.

#### Aprender de una captura, y por qué es explícito

La tira de capturas guardaba fotos y nada más. La visión del proyecto dice
«actualizar la referencia estadística tras cada pieza buena, nunca reentrenar»,
y eso sólo se podía hacer desde el diálogo de una inspección recién corrida:
las fotos que uno guarda durante la puesta a punto —que son precisamente las
buenas, elegidas a mano— no servían para alimentar nada.

**Explícito y por foto, nunca automático.** Una referencia contaminada con
piezas malas no falla ruidosamente: falla **dejando pasar defectos**, y nadie lo
nota hasta que llega una reclamación. Es el peor modo de fallo de toda la
aplicación, así que aprender es siempre un acto del operador sobre una foto
concreta que él ha mirado. No hay «aprender de todas» ni aprendizaje al vuelo.

**Y antes de sumarla se inspecciona.** Si el programa considera mala esa pieza,
se dice con el motivo y se pregunta: añadirla mueve lo que se considera normal
hacia ella, y a partir de ahí defectos parecidos pasarán como buenos. El
operador puede tener razón —la referencia se quedó estrecha— o haberse
equivocado de foto; lo que no puede es decidirlo sin la información.

El botón apagado dice **cuál** de los tres motivos lo apaga (sin foto elegida,
sin pieza elegida, sin modelo ONNX), porque cada uno pide un arreglo distinto y
un texto único los escondería. Y su disponibilidad se recalcula al cambiar la
selección de la tira y al cambiar de pieza: calcularla una sola vez es
exactamente el fallo que costó tres rondas con el pincel.

#### El trazo es un gesto, no un resultado

La pincelada se pintaba encima y ahí se quedaba. A los tres trazos ya no se
sabe qué se está mirando: si el contorno que detecta el programa o la mancha
que uno dibujó. Ahora el trazo se **retira** en cuanto el contorno corregido
llega a la pantalla.

No antes: entre soltar el pincel y ver el resultado hay unas décimas, y quitar
la mancha en ese hueco dejaría un momento sin trazo y sin contorno nuevo, que
se lee como que no ha pasado nada. Se retira cuando el análisis aterriza.

Retirar el trazo **no deshace la corrección** — eso es lo que fija el test, y
es el error fácil: si se fuera con él, quitar la mancha desharía el trabajo.
Y como la corrección deja de verse, tiene que **decirse**: un aviso «Borde
corregido» junto al modo de medición, con los píxeles. Sin él, una corrección
activa sería estado invisible, y el operador estaría mirando un contorno que
no sale de la detección sin forma de saberlo.

**Deshacer con parches, no con instantáneas.** Cada máscara mide lo que la
imagen: un paso completo son 4 MB a 1920×1080, y cincuenta pasos serían
doscientos megas. Una pincelada toca una zona pequeña, y es esa zona —antes y
después— lo único que se guarda. Guardar las dos caras deja deshacer y rehacer
simétricos sin reconstruir nada. Medido: sesenta trazos dejan cincuenta pasos
recordados, que es el tope, el mismo que el de las herramientas.

**Un solo Ctrl+Z.** La aplicación ya tenía deshacer para las herramientas
dibujadas. Darle al pincel su propio atajo obligaría a saber cuál de los dos
está uno usando, y a acertar — peor que no tener deshacer. La regla es la que
espera cualquiera con un pincel en la mano: mientras el pincel está activo,
Ctrl+Z deshace la pincelada; con el pincel apagado, sigue siendo el de las
herramientas. Un test cuenta las acciones con Ctrl+Z registrado y exige que
haya **una**: dos serían un atajo ambiguo y Qt no dispararía ninguno.

**Cambiar de imagen tira la corrección y su historia.** Cada paso de deshacer
guarda un rectángulo en coordenadas de la imagen sobre la que se pintó; al abrir
otra más pequeña, ese rectángulo se sale de la máscara nueva — y recortar una
`cv::Mat` fuera de sus límites no devuelve vacío, **lanza**. Deshacer después de
cambiar de imagen cerraba la aplicación, y el test lo reprodujo antes de
arreglarlo. La corrección tampoco tenía ya sentido: el análisis descarta las de
otro tamaño, así que lo que quedaba era historia muerta capaz de romper algo.

La ventana suelta su copia desde `onFrame`, no emitiendo desde el lienzo:
`setFrame` corre en mitad de la llegada de un frame, y reentrar ahí en el
análisis mediría el frame viejo.

#### De corregir una imagen a arreglar la detección

Corregir el borde tapa el fallo en la imagen que tienes delante. Corregir el
mismo borde diez veces es un ajuste mal puesto que nadie ha mirado.

La corrección es la pieza que faltaba para poder mirarlo: es la **respuesta
correcta** para un caso que la detección falló. Con ella se puede buscar,
probando, qué ajuste la habría dado solo. `suggestSegmentation` hace eso
—barrido grueso sobre el umbral y afinado alrededor del mejor, más las dos
polaridades explícitas— y devuelve el ajuste junto con **dos** cifras de
parecido: la de ahora y la del propuesto. Sin las dos, la sugerencia no se
puede juzgar.

El parecido se mide con **IoU** y no con «porcentaje de píxeles iguales», y la
diferencia no es académica: medido, en una imagen donde la pieza ocupa el 1 %,
decir «todo es fondo» acierta el 99,0 % de los píxeles y no detecta nada. Con
IoU eso vale 0,00.

`applyMaskCorrection` es pública por esto mismo. El afinador tiene que
reproducir EXACTAMENTE la máscara que se ve en pantalla; con dos copias de esa
operación, acabaría optimizando para algo distinto de lo dibujado en cuanto una
de las dos cambiara.

Va a petición y no tras cada pincelada: medido, la búsqueda cuesta **653 ms**
sobre un frame de 1920×1080, y meterlos en cada trazo convertiría el pincel en
algo intratable. Al aplicar el ajuste se **retira** la corrección a mano — si se
dejara puesta, no habría forma de saber si lo que se ve sale del ajuste nuevo o
sigue saliendo de la pincelada.

Y cuando no hay nada que ganar, se dice con las cifras y no se toca nada.
Proponer un cambio que no arregla nada gasta la confianza que hace falta para
cuando sí lo arregle.

### La tira de capturas: las fotos tienen que coexistir

«Capturar foto» congelaba el frame y ahí se quedaba: tomar la siguiente tiraba la
anterior. Eso vale para medir UNA pieza y no vale para las tres cosas que se
piden de un montón de fotos —tener historial, comparar unas con otras, alimentar
el aprendizaje— porque **las tres necesitan que las fotos coexistan**.

`ui::CaptureTray` las guarda juntas durante la sesión y las vuelca a disco cuando
el operador lo pide. **No se guarda sola a cada disparo**, y es deliberado: en
una puesta a punto se disparan veinte fotos de las que interesan tres, y una
carpeta con diecisiete descartes es peor que no tener carpeta.

El panel va **a la izquierda**: la derecha ya es de las herramientas, y se lee de
izquierda a derecha — primero lo que has recogido, después sobre qué trabajas.
Al elegir una miniatura, la foto pasa a ser la fuente actual, así que todo lo que
ya funciona —medir, dibujar, inspeccionar— vale igual sin un camino nuevo que
mantener.

Cuatro decisiones del guardado, todas con su prueba:

- **PNG y no JPEG.** Estas fotos son para volver a medir sobre ellas, y el JPEG
  inventa bordes donde no los hay. Este proyecto ya midió lo que le cuesta eso a
  la detección.
- **`<pieza>_<AAAAMMDD-HHMMSS>_<nn>.png`.** La fecha en ese orden hace que el
  orden **alfabético** de la carpeta sea el **cronológico**; con
  `HH-MM-SS_DD-MM-AAAA` se ordena por hora del día y mezcla semanas, y el fallo
  no se ve hasta que hay dos días de fotos.
- **El número al final** porque en una ráfaga entran varias fotos en el mismo
  segundo, y sin él la segunda pisaría a la primera sin que nadie se enterara.
- **Nunca sobrescribe.** Perder una captura anterior por repetir un nombre es el
  peor fallo posible aquí: no se nota hasta que se va a buscar.

Un nombre de pieza como `Eje 3/4"` es razonable y es un nombre de fichero
imposible. Se limpia en vez de fallar al guardar: perder la captura por un
carácter sería castigar al operador por escribir bien.

El dock es **nuevo**, así que cae en el caso que ya documenta la sección de la
paleta: ninguna disposición guardada hasta hoy sabe de él, y `restoreState` lo
dejaría oculto. Lleva la misma salvaguarda — quien actualice y no lo viera no
tendría forma de adivinar que le falta un panel.

**Lo que todavía no hace:** conectar esas capturas con el aprendizaje incremental
de la referencia, que ya existe (`updateReference`). Falta decidir antes el
criterio, y no es un detalle: **solo deben entrar piezas confirmadas como
buenas**, o la referencia aprende defectos.

### El vídeo se controla, no solo se reproduce

`VideoFileSource` reproducía en bucle y no exponía nada más, así que para volver
a un frame había que esperar a que el bucle pasara otra vez por ahí. Un vídeo así
no sirve para lo que se abre un vídeo: **encontrar EL frame en el que la pieza se
ve bien y trabajar sobre él**.

Ahora tiene pausa, salto por fracción y paso a paso. Todo lo que cruza al hilo de
reproducción va en **atómicos**, y las dos operaciones que tocan el
`VideoCapture` —saltar y leer— se aplican **dentro del bucle**: moverlo desde el
hilo de la interfaz mientras está leyendo es pedir una corrupción.

Tres decisiones que costaron pensarlas:

- **En pausa se sigue atendiendo.** El bucle espera a trozos de 20 ms en vez de
  dormir de una vez, porque parar y saltar tienen que funcionar con el vídeo
  detenido — que es justo cuando más se usan. Con una siesta larga, cerrar
  tardaría lo que durase.
- **El paso deja el vídeo en pausa.** Es lo que se pide cuando se busca un frame
  concreto, y con la barra no se puede: en un vídeo largo, un píxel de barra son
  varios frames.
- **Sin total, la barra se apaga.** Hay contenedores que no dicen cuántos frames
  tienen. Colocar el pulgar sin saberlo sería inventarse dónde va el vídeo, así
  que se informa 0 y quien pinta la barra la deshabilita y enseña el número de
  frame.

En la interfaz, la barra **solo aparece con un vídeo abierto**: con una cámara no
hay nada que rebobinar, y una barra muerta bajo la imagen es ruido que además
invita a pulsarla. Mientras el operador arrastra el pulgar, la barra deja de
seguir al vídeo — si no, saltaría bajo el dedo cada vez que llega una posición.

Medido: saltar al 75 % de un vídeo de 40 frames aterriza en el **frame 30**, y el
paso da exactamente **un** frame y se queda.

### El banco sin cámara: `pci_probe`

Un ejecutable de consola (`tools/probe_main.cpp`, sin Qt) que corre el pipeline
entero sobre una imagen o un vídeo y escribe lo que midió: qué se segmentó, el
fixture, **la figura reconocida**, las propuestas de medición, la escala en
mm/px y el reparto de tiempos por etapa. Con `--json` para consumirlo desde un
script y con códigos de salida distintos según el fallo (2 argumentos, 3 no se
pudo abrir, 4 sin pieza) — un banco cuyo código de salida siempre es 0 no sirve
para nada automatizado.

Existe porque **hasta ahora nada que dependiera de ver una pieza se podía
comprobar sin hardware delante**, y eso ya ha costado varios diseños con tests
verdes que la cámara real desmintió.

Y lo justificó el primer día, destapando dos fallos que ninguna prueba
sintética había cazado:

- **Clasificar el contorno que devuelve `analyzeFrame` llamaba círculo a un
  cuadrado.** `findLargestContour` extrae con `CHAIN_APPROX_SIMPLE`, o sea con
  los tramos rectos colapsados: de un cuadrado alineado a los ejes quedan ocho
  puntos, todos en las esquinas. Y una circunferencia pasa por las cuatro
  esquinas de un cuadrado, así que sin ningún punto en medio de los lados no hay
  nada que la desmienta: la salida era «contorno circular de Ø 402 px, el punto
  peor se separa **0 px**». Exacta según su propia medida y completamente falsa,
  y con la trampa puesta justo para quien hiciera lo natural, porque la cabecera
  promete clasificar «el contorno exterior». `classifyShape` ahora **rellena**
  los tramos largos antes de medir: sobre un contorno ya denso no añade nada, y
  sobre uno simplificado devuelve la respuesta correcta en vez de rechazarlo.
- **Un «Radio» que anunciaba 3899 px y medía 43**, sobre una pieza de 199. La
  descomposición devuelve como arco cualquier tramo que no consigue llamar
  recta, y a un tramo casi recto le sale una circunferencia enorme. Además esa
  cifra alimentaba la banda de búsqueda (`radio × 0,3` = 1170 px en una imagen
  de 640×480), así que la herramienta buscaba el borde por media pantalla.
  Ahora un redondeo mayor que la propia pieza se descarta, la banda se limita al
  tamaño de la pieza, y se aplica la misma regla que ya tenía el calíper: si lo
  medido no es lo prometido, la propuesta se descarta.

Queda **documentado y sin arreglar**: una zona de trabajo completamente fuera
del frame se convierte en silencio en «sin zona» (`pipeline.cpp`, `roi &
frameRect` vacío → `useRoi = false`), o sea que se pide mirar una esquina y se
acaba midiendo todo. `pci_probe` lo detecta y se niega antes de analizar.

### Enumerar y abrir la cámara

**Enumerar sin abrir.** La lista de cámaras se pide a la API nativa del sistema
—DirectShow COM en Windows, V4L2 en Linux— que devuelve el **nombre real** del
dispositivo sin abrirlo. Esto no es un detalle estético: abrir una cámara
virtual que no está lista (por ejemplo AndroidCam antes de conectar el móvil)
hacía que su driver dividiera por cero y **matara el proceso entero** con una
excepción estructurada del sistema operativo que ningún `try/catch` de C++
atrapa.

**Apertura blindada.** El momento de abrir va envuelto en `core::runProtected`,
que instala un *Vectored Exception Handler* y usa `setjmp/longjmp` (GCC/MinGW no
soporta `__try/__except`) para sobrevivir a un fallo del driver y convertirlo en
un simple "no se pudo abrir". Si aun así el proceso muere, un manejador de
último recurso escribe el código de excepción y **la última operación en curso**
en `pc_inspector_crash.log`, para que un cierre silencioso quede diagnosticado.

**Hilo propio.** La captura corre en su propio hilo y entrega copias `QImage` a
la interfaz por señales encoladas; el búfer se fija en 1 frame para preferir
perder frames viejos antes que acumular retardo.

**Controles de la fuente.** Brillo, contraste, ganancia, exposición y enfoque se
sondean **escribiendo de verdad** en cada propiedad al abrir la cámara y
midiendo su rango al empujarla a los extremos. Mirar solo `get()` no vale: se
comprobó con una cámara real que devuelve brillo 91 mientras rechaza cambiarlo.
Los cambios se **agrupan por propiedad** antes de aplicarse, porque cada
`set()` cuesta milisegundos en el hilo de captura y arrastrar un deslizador
llegaba a atascar el vídeo.

**Resolución.** OpenCV tampoco sabe enumerar las resoluciones admitidas, así que
se sondean igual: se pide cada candidata de una lista estándar y se lee **la que
la cámara acabó dando** (los backends ajustan a la más cercana sin avisar), sin
repetidos y restaurando la original al terminar. Medido con una webcam real:
**~15 s para diez candidatas**, con el vídeo detenido mientras dura, porque cada
cambio reinicia el flujo de captura. Por eso el sondeo **no es automático**: lo
lanza el operador con un botón y **el resultado se cachea por cámara** en
`Settings`, de modo que se paga una sola vez.

Cambiar de resolución tiene efectos que hay que atender o se convierten en
fallos silenciosos:

- La **calibración px→mm deja de valer** (está sellada con la resolución) — ya
  existía el aviso, y ahora se dispara también aquí.
- La **zona de detección** y el **cero fijado del tablero** están en píxeles de
  imagen: se **reescalan proporcionalmente** (`vision::rescaleRect` /
  `rescalePoint`) para que sigan señalando el mismo punto de la escena. Las
  herramientas no necesitan nada: viven en coordenadas de pieza.
- El reajuste se dispara al detectar que **el frame recibido cambió de tamaño**,
  no al pedir el cambio: la cámara puede dar una resolución distinta de la
  solicitada, y así también se cubre que la cambie ella sola.

### El perfil de medición de la cámara, y por qué se prueba antes de creérselo

Una webcam viene ajustada para que una cara se vea bien: automático todo, que la
imagen se adapte sola. Para medir, «que se adapte sola» es exactamente el
defecto — significa que **el borde de la pieza se mueve cuando cambia la luz de
la nave**. De ahí un perfil de arranque para cámaras que el operador no ha
configurado nunca (`measurementDefaults`).

Lo interesante no es el perfil: es cuántas veces lo desmintió la cámara real.

**Intento 1 — «congelar cada control donde ya está».** Suena inmejorable:
repetibilidad sin cambiarle la imagen a nadie. Medido: la captura pasó de
**29,7 a 8,0 fps**. Con el automático puesto, `get(CAP_PROP_EXPOSURE)` devuelve
el nominal —el más largo del rango— mientras el sensor usa exposiciones cortas
de verdad. **El valor reportado bajo automático miente**, igual que miente el
del propio interruptor, que devuelve −1 pase lo que pase.

**Intento 2 — «apagar el automático y elegir la exposición midiendo».** Los fps
no bajan poco a poco con la exposición: son planos —30,2 a 30,3— desde −11 hasta
−5 y se desploman solo en el extremo largo (−4 da 16,0 y −3 da 8,0), porque el
tiempo de integración pasa a ser mayor que el periodo del frame. La regla que
sale de esa forma es **la exposición más larga que todavía da la velocidad
máxima**: toda la luz que no cuesta fps. Medido: escribir *solo*
`auto_exposure = 0`, sin elegir valor, dejó la cámara en **8,0 fps** — al quitar
el automático se cae a su manual, que era el más largo. De ahí una regla que
vale para toda esta capa: **no se apaga un automático que no se pueda
sustituir**.

**Intento 3 — el que quedó.** Con la exposición elegida por medida, la cámara
daba 30,0 fps... y **el 21 % del contraste**. En automático la cámara gobierna
también la GANANCIA, y en esta máquina `gain` sale no ajustable: ese refuerzo se
pierde y no hay con qué reponerlo. O sea que el perfil compraba repetibilidad a
cambio de una imagen mucho peor, por un 0 % de velocidad.

Así que el perfil **se prueba y se juzga** (`judgeProfile`): se mide qué da la
cámara en automático sobre esa escena, se aplica, se vuelve a medir, y si no se
lo ha ganado se vuelve al automático **diciendo por qué**. Perder contraste está
bien si a cambio hay velocidad de verdad —los 3,8× del caso lento sí lo valen, y
esa imagen más oscura se arregla con una lámpara—; por un 3 % no. Una estación
que mide repetible pero no ve la pieza no mide nada.

El experimento entero cuesta **3,2 s** y solo corre en cámaras sin configurar.
Se repite en cada arranque a propósito: si alguien añade luz, la respuesta
cambia sola y el perfil pasa a aceptarse.

La moraleja, que es lo que hay que llevarse: **de una cámara no se cree lo que
dice, se mide lo que hace**. Las tres versiones tenían tests verdes; las tres
veces fue la cámara real la que dijo que no.

**Y la contrapartida: la cámara de esta máquina tampoco lo prueba todo.** Aquí
hay poca luz, así que el perfil **siempre acaba rechazado** y el camino de
aceptación no se había visto correr nunca — precisamente el que deja la cámara
tocada. Por eso la orquestación vive en `runExposureProfile` (en
`camera_controls`), detrás de una costura de tres funciones —fijar la
exposición, poner o quitar el automático, mirar— que es todo lo que el barrido
necesita de una cámara. `drainExposureSweep` quedó en cableado: monta las tres
lambdas sobre la `cv::VideoCapture` y traduce el resultado a log y señal. Los
tres diseños fallidos no fallaron en las piezas sueltas —`chooseExposure` y
`judgeProfile` estaban bien— sino en **el orden en que se llamaban**, que era
justo lo único sin probar.

Con la cámara de mentira aparecieron dos agujeros más, los dos con la misma
forma: `judgeProfile` los aprobaba, porque ninguno es asunto suyo.

- **La cámara sorda.** Acepta las escrituras y las ignora. El barrido sale
  plano, el veredicto no tiene nada que reprochar (velocidad ×1,00, contraste
  ×1,00) y se anunciaba «exposición fija aceptada»: el operador se fiaba de una
  repetibilidad inexistente. Ahora, si **todas** las medidas salen idénticas
  —incluida la del automático— el resultado es `Ignored` y se restaura el
  automático. La igualdad se exige casi exacta (1e-9) a propósito: en una cámara
  viva dos ventanas nunca dan el mismo contraste, así que no puede haber falsos
  positivos.
- **El techo mal medido.** El techo es la primera medida. Si esa ventana se
  pierde sale 0, la salida temprana se dispara con la primera candidata y la
  elegida acaba siendo la **más larga**, que es la peor. Y el veredicto lo
  aprobaba con buena nota, porque una exposición larga da *más* contraste. La
  regla que faltaba es la que no admite intercambio: **el perfil no puede dejar
  la cámara más lenta que el automático**. Sin esa red, 29,7 → 8,0 fps: el mismo
  desastre de 3,7× que este código existe para evitar.

### La compresión JPEG, que es lo que trae toda imagen de fuera

Todo el banco de imágenes usaba mapas de bits perfectos, y eso no es lo que
llega: una foto de cámara o de móvil viene en JPEG, y sus artefactos se
concentran en los **bordes de alto contraste** — justo donde se mide. Una
calibración hecha sobre un PNG y aplicada a un JPEG podría estar midiendo otra
cosa sin que nada avisara.

No se simulan los artefactos: se generan pasando la imagen por el codificador de
verdad (`imencode`/`imdecode`), porque el daño del JPEG no es ruido blanco — son
bloques de 8×8 y campanas alrededor del borde.

Medido, y es una buena noticia: **de calidad 100 a 15, el lado de un hexágono se
mueve un 0,24 % y el diámetro de un disco un 0,04 %**, y la clase se reconoce
igual en todo el rango. El JPEG mueve la medida menos que el propio rasterizado.

Donde sí hace daño es con **poco contraste**, y tiene sentido: con un escalón de
30 niveles entre pieza y fondo, el error de cuantización es del tamaño del
escalón y el codificador se lo come. Ahí aguanta hasta **calidad 15** —muy por
debajo de lo que da cualquier cámara— y en 5 la figura se sigue reconociendo
como redonda pero ya no se propone su diámetro. El test afirma solo el lado
bueno de esa frontera: pasado ese punto la respuesta correcta es «no se puede
medir», y fijar una degradación concreta ataría el test al codificador.

### Dar por bueno lo que no se ha visto

La auditoría de las 32 herramientas destapó tres que devolvían **un número
creíble sin haber medido**, que es la forma más cara de fallar que tiene esta
aplicación: no falla, aprueba.

**Borde liso y Rectitud daban 0,000 y veredicto OK sobre una mella de 26 px.**
Las dos recorren el borde con escaneos perpendiculares de largo `scanLength`
centrados en la línea trazada. Un defecto más hondo que media ventana cae FUERA
del escaneo, así que esa estación no encuentra borde… y se descartaba en
silencio. La recta se ajustaba solo con las estaciones buenas, y la herramienta
declaraba perfecto justo el tramo donde estaba el defecto.

«Rebabas y mellas» ya tenía la red —avisa del tramo ciego y manda subir el
largo—; las otras dos no. Ahora la comparten las tres.

**Y la red estaba mal medida**, cosa que solo se vio al llevarla a las otras dos.
El corte era «tres escaneos seguidos sin borde», y eso significa cosas distintas
según lo fino que se muestree: con 120 escaneos sobre 280 px son 4,7 px de borde
ciego —ruido de binarización— y con 20 escaneos son 29 px, o sea una mella entera
escondida. Con esa regla, **subir la resolución del muestreo hacía saltar el
aviso y bajarla lo silenciaba**, que es exactamente al revés de lo que debe
pasar. Ahora el umbral va en LONGITUD: un suelo de 3 px para el ruido de un
escaneo suelto y un 2 % del tramo, porque en un borde largo un hueco pequeño pesa
menos.

**El Perfil de línea no tenía polaridad.** Era la única herramienta de silueta
que binarizaba siempre con `THRESH_BINARY_INV`, o sea dando por hecho que la
pieza es lo oscuro. Con el montaje contrario —contraluz, pieza clara sobre fondo
negro— comparaba el nominal contra el **fondo** y devolvía 125,7 px de perfil con
veredicto bueno. Ahora lleva `darkPiece` como sus ocho hermanas, y las plantillas
guardadas antes conservan el valor de entonces para seguir midiendo igual.

### Quién decide qué mide una herramienta: el modelo, no el panel

Cinco herramientas —Región, Ranura, Chaflán, Acuerdo y Máx./mín.— calculan de
dos a seis números y **solo uno lleva la tolerancia**. El desplegable del editor
estaba habilitado únicamente para la Región, porque el panel preguntaba «¿es una
Región?», así que las otras cuatro vigilaban siempre la primera opción de su
enum: el ancho de la ranura, el ángulo del chaflán, el radio del acuerdo y la
anchura mínima. El operador veía los tres números en el detalle y no tenía forma
de decir cuál era la cota.

Es **exactamente el mismo error** que ya se corrigió con las referencias de
GD&T, y con la misma causa: una pregunta sobre tipos concretos escrita en la
interfaz. La solución es la misma que entonces — `measureChoicesOf(geometry)` y
`setMeasureChoice(geometry, valor)` en el modelo — y su valor está en que la
sexta herramienta que publique varios números no puede quedarse fuera en
silencio: el panel ya no sabe cuáles son, pregunta.

`setMeasureChoice` **rechaza** un valor que no sea del enum en vez de corregirlo:
corregir en silencio es lo que hace que un fichero corrupto mida otra cosa sin
que nadie se entere, y esta capa ya lo hace en otro sitio (el eje de la Posición
cae a `Radial` ante cualquier número raro).

### Qué hace utilizable una imagen: el contraste, no el brillo

El juicio de calidad rechazaba **dos montajes estándar y opuestos**, los dos
perfectamente medibles:

- **Contraluz** —pieza clara sobre fondo negro, que es como se miden las
  siluetas—: brillo medio del frame **37**, por debajo del mínimo de 40. Es
  decir, ninguna captura a contraluz se podía registrar con los criterios por
  defecto.
- **Pieza oscura sobre mesa blanca**: si el brillo se midiera sobre la pieza en
  vez de sobre el frame —que fue el primer arreglo que probé— daría **30**, y se
  rechazaría por el mismo motivo.

Ningún nivel medio aprueba a los dos, y ahí está la lección: **lo que hace
inservible una imagen no es que sea oscura ni que sea clara, es que la pieza no
se distinga del fondo**. Así que `QualityMetrics` gana `pieceContrast` —la
separación en niveles de gris entre la pieza y su fondo— y el criterio de brillo
**solo se aplica cuando la pieza no se separa**. Medido: el contraluz da 190
niveles y la mesa blanca 153; el umbral está en 60, con holgura por los dos
lados. Sin pieza detectada no hay contraste que valga y el nivel medio vuelve a
ser lo único que hay.

Y cuando rechaza, el motivo dice **las dos cosas** —«demasiado oscura *y* la
pieza no se separa del fondo»—, porque con solo la primera el operador sube la
luz sin entender que su problema es el contraste.

### El primer arranque

Una instalación nueva abre sin calibrar y sin ninguna pieza registrada, y no
decía por dónde empezar. Son tres pasos y siempre los mismos —enfocar,
calibrar, registrar la pieza— pero solo se saben si alguien te los ha dicho una
vez.

Tres decisiones, y la forma importa tanto como el contenido:

- **No es un asistente modal.** Esos se cierran sin leer, y encima tapan justo
  la ventana que hay que mirar para hacer el primer paso. Es una línea sobre el
  vídeo, con un botón de «Entendido».
- **Dice el SIGUIENTE paso, no los tres.** Enseñar tres cosas cuando solo se
  puede hacer una es la manera de que no se haga ninguna. Y el orden no es una
  preferencia: calibrar con la imagen desenfocada fija una escala mala, y
  registrar una pieza antes de calibrar guarda sus medidas en píxeles.
- **Una vez y no vuelve.** Repetirlo cada arranque sería un cartel que se
  aprende a no ver. El estado permanente ya lo lleva la tira de indicadores,
  que para eso está — y por eso el texto la señala explícitamente en vez de
  repetir lo que ella dice.

Sin cámara en marcha no dice nada: el botón de arrancar está a la vista, y
decirle «enfoca la pieza» a quien todavía no ve imagen es ruido — que en el
primer arranque es exactamente lo que enseña a ignorar los avisos.

La regla vive en `ui/setup_guide.*` y se prueba sin ventana.

### La tira de estado de la estación

Los cuatro datos que deciden si una medida vale —**escala calibrada, enfoque
fijo, exposición fija y zona de trabajo**— estaban repartidos por las pestañas
de «Configurar». Para saber si estabas midiendo en condiciones había que abrir
el panel y recorrerlas, que es justo lo que nadie hace antes de medir.

Ahora son cuatro indicadores en la barra de estado, cada uno con su motivo en
el tooltip y **un clic que lleva a la pestaña que lo arregla**: enseñar un
problema sin decir dónde se toca es media ayuda.

La regla del color vive en `ui/station_status.*` y es lo único que aquí puede
estar mal, así que va aparte y se prueba entera sin ventana ni cámara:

- **El mismo estado significa cosas distintas según haya milímetros de por
  medio.** El enfoque en automático es **ámbar** sin calibrar —una comodidad
  legítima: las medidas van en píxeles y nadie ha prometido milímetros— y
  **rojo** con la escala calibrada, porque entonces un reenfoque cambia todas
  las cotas a la vez.
- **Procesar la imagen entera nunca es un aviso.** Es más lento pero es lo más
  difícil de que falle: una elección legítima, no un defecto. Avisar de algo que
  no es un problema es la forma más rápida de que se deje de mirar la tira.
- **Lo que la cámara no deja tocar no se pinta como culpa del operador.** Si el
  sondeo dijo que el foco no es ajustable, el indicador queda neutro y lo
  explica. Pedirle que arregle algo que no tiene con qué arreglar es donde una
  tira de estado deja de creerse.
- **La estación en condiciones no tiene nada que decir**: hay un test que exige
  que los cuatro salgan en verde, porque si encontrara algo que decir en el caso
  bueno el operador aprendería a ignorarla.

Se refresca desde `updateCalibrationLabel()`, que ya se llamaba en todos los
sitios donde cambia cualquiera de los cuatro —calibrar, cambiar de cámara,
tocar un automático, mover la zona—, así que engancharse a él es engancharse a
todos de una vez.

### El aviso de «calibrado + automático encendido»

La combinación que produce números **creíbles y falsos**, que es la peor clase
de error que este programa puede dar. La escala px→mm se fijó con una
magnificación concreta: si el autofoco reenfoca, la magnificación cambia y
**todas las cotas cambian a la vez y proporcionalmente**, sin que nada en
pantalla lo delate. La exposición automática es más sutil pero del mismo tipo —
mueve el umbral aparente del borde, así que la misma pieza sale más gorda o más
fina según la luz.

Tres decisiones:

- **Solo con las dos condiciones juntas.** Sin calibrar, el autofoco es una
  comodidad legítima: las medidas van en píxeles y nadie ha prometido
  milímetros. Avisar ahí sería ruido, y un aviso que salta siempre es uno que se
  aprende a ignorar — con lo que tampoco serviría donde importa. El barrido
  recorre los cuatro cuadrantes y exige silencio en tres.
- **Va donde se ve la escala**, en la misma etiqueta que dice «Escala: 0,0421
  mm/px», no en una pestaña que el operador no va a abrir. Reutiliza el sitio y
  el estilo del aviso de calibración obsoleta, que existía por la misma razón.
- **Nombra cuál está encendido**, porque el daño no es el mismo ni lo que hay
  que hacer tampoco.

El estado de los automáticos **se lleva en la ventana, no se le pregunta a la
cámara**: `get(CAP_PROP_AUTO_EXPOSURE)` devuelve −1 pase lo que pase, y sobre
esa mentira ya se perdieron dos diseños del perfil de medición. Se actualiza en
los cuatro sitios donde puede cambiar: el sondeo inicial, el perfil, el veredicto
del barrido y un cambio del operador.

### Asistente de enfoque

`vision::sharpnessOf(imagen, roi)` es la varianza del Laplaciano de una región.
La métrica ya existía en `computeQualityMetrics`, pero solo se usaba para
validar capturas al registrar, y el deslizador de enfoque se movía a ciegas.

Tres decisiones:

- **Se mide sobre la pieza, no sobre el frame.** Medido en el test: con un fondo
  ruidoso y la pieza desenfocada, el encuadre completo da **90 970** y la pieza
  **155**. Quien mirase el número del frame estaría enfocando el fondo.
- **La barra es relativa al máximo visto.** La varianza del Laplaciano no tiene
  tope: un valor absoluto no dice nada, y lo único accionable es "¿sube o baja?"
  — que es exactamente cómo se enfoca.
- **No se tocó `computeQualityMetrics`.** Su umbral de aceptación
  (`QualityCriteria::minSharpness = 40`) está ajustado contra el número del
  frame completo; moverle la medida debajo habría cambiado en silencio qué
  capturas se aceptan al registrar.

Un hallazgo del test que conviene no olvidar: la nitidez **no es monótona hasta
el final**. Medido sobre la misma imagen desenfocada progresivamente: 7444 →
1372 → 765 → 98 → 2,5 → **6,5**. Por debajo del 0,1 % del pico ya no queda
detalle que perder y lo que se mide es residuo numérico. Por eso la prueba exige
monotonía solo mientras la medida signifique algo, y por separado que el
desenfoque fuerte deje el valor cien veces por debajo. Afirmar monotonía en la
cola sería afirmar sobre ruido.

### Contar piezas

`analyzeFrames` devuelve **todas** las piezas de la imagen ordenadas de mayor a
menor; `analyzeFrame` sigue devolviendo la mayor y **no cambia de contrato**.

Van por separado, y no una encima de la otra, por coste: el camino de una sola
pieza es el que corre en cada frame del vídeo, y hacerle analizar también las
manchas que pasan el filtro de área sería pagar de más en el sitio más caliente.
Contar solo se activa cuando alguien va a mirar el número — la pieza declara que
espera más de una, o el panel Configurar está abierto — y entonces el análisis
se hace **una sola vez**: la mayor de `analyzeFrames` es exactamente la que
habría dado `analyzeFrame`.

Cada pieza se procesa **dentro de su propia envolvente**, no a tamaño de frame.
Sin eso, seis piezas costarían seis análisis completos y se perdería lo ganado
en C4b. Medido: seis piezas cuestan **1,62×** lo que cuesta una, no 6×.

El recuento es **una inspección por sí misma**: `domain::evaluatePieceCount`
compara lo esperado con lo encontrado y `combineVerdict` lo suma al veredicto,
con los dos números en el motivo ("se esperaban 6 piezas y se ven 5") — decir
solo "faltan piezas" obligaría a ir a contarlas a mano, que es justo el trabajo
que esto ahorra. Con `expected <= 0` no se juzga nada: la regla de siempre, un
aviso que salta siempre acaba ignorándose.

El número vive **en la pieza** (`Pieces.expected_pieces`, migración v9) y no en
los ajustes de la máquina, porque "seis tornillos en bandeja" es una propiedad
del trabajo. Al cambiar de pieza se recupera el suyo y no se arrastra el
anterior.

**Medir las N piezas sale casi gratis**, y esa es la recompensa de haber puesto
las herramientas en coordenadas de pieza desde la fase 2: medir la segunda barra
de la bandeja es `runTools` con **otro fixture**, sin tocar ni una herramienta.
Lo que hubo que decidir es todo lo demás:

- **La pieza 0 sigue siendo la de siempre.** Es `outcome.analysis`, con su rasgo
  distintivo y su giro ya aplicados; las demás salen de `analyzeFrames`, que ya
  se llamó para contar. Así, inspeccionar de una en una da exactamente el mismo
  resultado que antes, y no se analiza dos veces.
- **El veredicto es el de la peor pieza**, y el nombre de cada medida dice de
  cuál es ("ancho (pieza 3)"). Un NG que dijera solo "ancho fuera de tolerancia"
  obligaría a ir barra por barra, que es el trabajo que esto ahorra.
- **`ToolRunResult` lleva su `pieceIndex`.** Viaja en el propio resultado y no
  en una lista paralela, así la cadena entera —motor, historial, lienzo— sabe a
  quién pertenece cada medida sin llevar la cuenta aparte. El historial lo
  guarda en `ToolResults.piece_index` (migración v10); `0` es la pieza
  principal, así que lo ya guardado se lee igual.
- **En el vídeo se pintan las marcas de todas y los números de una.** Seis
  piezas por cinco herramientas son treinta etiquetas encima de la imagen: eso
  no se lee, se tapa la pieza.

Limitación anotada: el **rasgo distintivo** solo se aplica a la pieza principal.
Es un punto concreto de *una* pieza y no significa nada en las demás; con
`autoOrient` activo, las piezas 2..N usan su propio eje principal.

#### Qué pieza se mide

`vision::measuredPieceIndex(pieces, wanted)`. `wanted` va en **orden de lectura
empezando por 1**; el cero es un estado con nombre —«la que decidas tú»— y
significa la mayor, que es lo que la aplicación hizo siempre.

Existe un escalón por encima de `largestPieceIndex`, y por el mismo motivo. El
comentario de aquella avisa de que qué pieza se mide «no se puede permitir
divergir en silencio: en vivo se vería una y el informe traería la de la otra».
Era literalmente lo que pasaba. El navegador de piezas **solo lo entendía el
camino del vídeo**; cuatro gestos más llamaban a `analyzeFrame`, que devuelve la
mayor y no sabe nada de navegadores:

| gesto | lo que daba |
|---|---|
| `onMeasurePieceClicked` | el informe de *Medir pieza*, de otra pieza |
| `onOpenEditorClicked` | el editor se abría sobre la mayor |
| `onAutoMeasureClicked` | proponía las cotas de la mayor, ancladas al fixture de la editada |
| `ensureContourReport` | el contorno que se ve y se exporta, de la mayor |

Medido sobre `arandelas-5`, donde la mayor es la **#8** en orden de lectura:
señalar la 1 y pedir el informe devolvía «Arandela» de 274 px cuando la pieza
señalada es un «Polígono redondeado de 3 lados» de 95 px. Ninguno falla ni
avisa — dan el informe de otra pieza.

La ventana enruta por `MainWindow::analyseMeasuredPiece`; el editor, que no ve
el navegador, reconoce **su** pieza por cercanía del origen del fixture
(`EditorWindow::analyseEditedPiece`), con la mayor como respaldo si la suya se
fue de cuadro. Por índice no valdría: el índice cambia en cuanto una pieza entra
o sale del encuadre, y el editor puede estar abierto mientras la cámara sigue
dando frames.

`tests/test_same_piece_everywhere.cpp` vigila el patrón, no el caso: lee el
cuerpo de esos cuatro gestos y exige que ninguno vuelva a llamar a
`vision::analyzeFrame`.

### La zona de trabajo automática

**Es una opción, no el estado de reposo**, y esto se corrigió: antes era el modo
de fábrica y también al que se caía al borrar una zona dibujada a mano, con el
argumento de que la automática «no puede cambiar ninguna respuesta».

**Esa frase es falsa y está medida.** Recortar no mueve una medida —eso sigue
siendo cierto y está comprobado píxel a píxel más abajo— pero sí cambia **qué
piezas existen**. Sobre un frame de 640×480 el recorte se asienta en 188×188, el
**11,5 %** del área, y con dos piezas separadas la imagen entera ve **dos** y el
recorte ve **una** (`tests/test_auto_zone_hides_pieces.cpp`). El operador que
borraba su zona creyendo volver al frame completo se quedaba mirando otro
recorte, más pequeño que el que acababa de borrar, y nada se lo decía.

Sigue siendo útil —ahorra trabajo de verdad cuando hay una pieza que no cambia
de sitio— pero es una elección con contrapartida. Quien la quiera, la enciende.
Su rótulo dice «más rápida» y no «recomendado» por lo mismo.

`vision/auto_roi.*` decide en qué rectángulo buscar la pieza en el próximo
frame. **No hizo falta mecanismo nuevo**: `PipelineConfig::roi` ya recortaba y
`analyzeFrame` ya devolvía las coordenadas en el marco completo; lo que faltaba
era quién calcula ese rectángulo y lo mueve con la pieza.

Lo primero que hubo que demostrar es que **recortar no cambia el resultado**: si
el fixture saliera distinto, todas las herramientas se desplazarían. El banco
(`tests/test_working_zone.cpp`) lo lleva hasta el final: contorno punto a punto,
área, perímetro, fixture, y el recorte canónico **y su máscara** comparados
píxel a píxel. Sobre 20 frames con la pieza cruzando en diagonal el peor desfase
del fixture es de **0,000015 px** —puro redondeo de sumar la esquina en
`float`— y la comparación de imagen da **cero** diferencias.

La pieza de prueba es una «L» **texturada** a propósito: sobre una pieza de un
solo tono la comparación píxel a píxel pasaría igual con el recorte desplazado,
porque estaría comparando una mancha uniforme contra otra. Y la contención se
comprueba contra la verdad del frame completo, no contra los límites del
análisis recortado, que se darían la razón a sí mismos si el recorte hubiera
cortado a la pieza.

Que el banco **muerde** se comprobó mutando producción: desplazar 1 px el offset
del ROI, invertir el orden de las guardas y quitar la unión `(eased | target)`
hacen caer un test distinto cada uno.

La ganancia, medida en el mismo proceso: sobre 1280×720 con una pieza de
180×140, el recorte ocupa el **7,9 %** del área y el análisis pasa de ~13 ms a
~2 ms, **6×**. El test exige solo 2× porque el margen sobra y afinar el umbral
lo convertiría en un generador de fallos intermitentes; y compara **relativo**,
nunca milisegundos absolutos, que dependen de la máquina.

Cuatro decisiones de diseño:

- **Ante la duda, la imagen entera.** El seguimiento se rinde si pierde la
  pieza varios frames, si la pieza toca el borde del recorte o si su área salta
  de golpe. Volver cuesta un frame; medir dentro de un rectángulo equivocado no
  se nota, y esa es exactamente la forma de fallar que hay que evitar.
- **El orden de las comprobaciones importa.** "Se está saliendo" se mira antes
  que "saltó el área", porque una pieza que se sale acaba recortada y al
  recortarse su área cae: salirse es la causa y el salto el síntoma. Al revés,
  el motivo que se le enseñaría al operador sería el equivocado.
- **Crecer es inmediato, encoger es lento.** El recorte se une siempre con el
  objetivo (así nunca corta a la pieza) y solo se encoge interpolando. Sin eso,
  el rectángulo late al ritmo del ruido de la segmentación.
- **No recorta cuando no hay nada que ganar**: ni piezas diminutas (el ahorro no
  compensa el riesgo) ni piezas que ocupan casi todo el frame.

La zona automática **no pisa la zona manual** del operador: `pipelineConfig_.roi`
sigue guardando la que él dibujó y el modo (`Off` / `Automatic` / `Fixed`)
decide cuál se usa. Y la zona activa se dibuja sobre el vídeo, porque un recorte
invisible convierte cualquier fallo en un misterio.

**Tres fallos que costó eso**, y que se corrigieron después de que el operador
reportara que «la zona de detección no funciona»:

- **Dibujar una zona no la usaba.** El modo lo decidía todo y arranca en «imagen
  entera», así que al arrastrar el recuadro este se guardaba, el botón pasaba a
  decir «Quitar zona», la barra de estado decía «zona activa»… y no se pintaba
  ni se usaba. Solo funcionaba si además se iba a *Configurar ▸ Rendimiento* a
  marcar el modo a mano. Meter el modo en medio desacopló el gesto de su efecto,
  y tres sitios de la interfaz afirmaban lo contrario de lo que hacía el
  programa. Ahora **dibujar una zona es usarla** y quitarla apaga el modo que la
  usaba; la regla vive en `vision::modeAfterFixedZoneChanged` —una función pura,
  probada— y no escrita a mano en la ventana.
- **La zona automática se movía sin verse.** Su rectángulo solo se repintaba al
  cambiar de modo, así que el operador veía uno quieto, o ninguno si no había
  tocado el modo desde que arrancó. Se repinta con cada frame analizado.
- **Con el contorno oculto se daba la pieza por perdida.** Con la pose congelada
  no se segmenta nada, así que no hay contorno; eso se le pasaba al seguimiento
  como «no hay pieza» y a los dos frames se rendía con un «se dejó de ver la
  pieza» que era falso — la pieza estaba ahí, lo apagado era el dibujo. Ahora
  `AnalysisOverlay::analysed` distingue «se buscó y no había» de «no se buscó»,
  y solo lo primero alimenta al seguimiento. Dar algo por perdido sin haberlo
  mirado es afirmar lo que no se ha medido.

#### La zona LIBRE: lo que un rectángulo no puede separar

Un rectángulo obliga a elegir entre dejar fuera parte de la pieza o dejar dentro
lo que estorba, y en una mesa real lo que estorba —el borde del útil, la sombra
pegada a un lado, la pieza de al lado— casi nunca cae en un rectángulo que no
toque también a la pieza. Con dos piezas en diagonal es imposible por
construcción: sus envolventes se solapan, así que **ningún** rectángulo contiene
a una sin tocar a la otra.

`PipelineConfig::roiPolygon` es esa zona en forma libre. Dos decisiones:

- **El rectángulo no desaparece.** La envolvente del polígono se sigue usando
  para recortar, así que la ganancia de velocidad de la zona se conserva intacta
  y el polígono solo añade precisión encima. Las dos cosas suman en vez de
  competir.
- **La máscara se recorta DESPUÉS de segmentar, no antes.** Recortar la imagen
  metería un borde artificial —negro contra la pieza— que la segmentación
  tomaría por un contorno de verdad. Sobre la máscara ya segmentada, lo de fuera
  simplemente deja de existir.

Medido: sobre dos piezas diagonales, la zona libre devuelve exactamente la caja
de la pieza buena, y el desfase del fixture frente a analizar el frame entero es
de **0,000000 px** — la misma exigencia que ya se le hacía a la zona
rectangular, porque acotar dónde se mira no puede cambiar lo que se mide. Un
polígono de menos de tres vértices no encierra nada y se comporta como si no
hubiera zona, en vez de recortar a una línea y quedarse sin pieza.

##### El gesto: un solo modo para el pulso y para los clics

Se dibuja de dos maneras y las dos son el mismo modo. **Arrastrando** se traza a
pulso, para rodear rápido; **a clics** se marcan vértices y se cierra sobre el
primero o con un doble clic, para seguir un borde con exactitud. Se ofrecen las
dos porque resuelven casos distintos y elegir por el operador habría estorbado a
la mitad de ellos. El botón derecho deshace el último vértice y, sin vértices,
cancela: sin esa salida, un trazo mal empezado solo se podía terminar mal.

Qué es un clic y qué es un trazo se decide en **píxeles de pantalla**, no de
imagen. Al 800 % de zoom, tres píxeles de mano son veinticuatro de imagen, y con
el umbral en coordenadas de imagen el mismo gesto significaría dos cosas según
por dónde se estuviera mirando. Es la misma regla que ya gobierna las manijas y
la tolerancia de agarre.

Sobre el vídeo, **lo que queda fuera de la zona se oscurece**. En un rectángulo
el dentro y el fuera se leen solos; en un contorno irregular no, y confundirlos
es creer que se está midiendo algo que el programa ni mira.

##### Simplificar el trazo: la garantía es una distancia, no un área

Un trazo a pulso trae cientos de puntos separados por un píxel, y guardarlos
todos no añade precisión — la mano no tiene esa resolución.
`vision::zonePolygonFromTrace` los simplifica con una tolerancia **proporcional
al perímetro**, no absoluta: así una zona pequeña no se come sus esquinas y una
grande no se guarda con mil vértices. Medido, la misma forma a radio 200 y a
radio 2000 sale con **32 vértices las dos**.

El primer intento de probarlo exigía que el área se conservara dentro del 1 %, y
salía un 1,45 %. La explicación que se escribió entonces —«es el temblor de la
mano»— era **falsa**, y medirla por separado lo demostró: el temblor cuesta un
0,6 % y la simplificación un 1,5 %. El motivo es geométrico y no se arregla con
umbrales: los vértices que sobreviven están **sobre** el trazo, así que cada
cuerda corta por dentro y un polígono inscrito siempre encierra menos que la
curva. Es un sesgo en una sola dirección, no ruido.

Lo que sí está acotado —y es lo que el algoritmo garantiza de verdad— es
**cuánto se mueve el borde**. La tolerancia se expone (`zoneSimplifyTolerancePx`)
justo por eso: una garantía que no se puede consultar no se puede comprobar. Con
el 0,15 % del perímetro y suelo de un píxel, un círculo de 400 puntos se guarda
con 32 vértices, el borde se mueve **0,93 px como mucho** frente a una tolerancia
de 1,44 px, y pierde el **0,29 %** del área. Con la mano temblando, la zona
guardada discrepa un 0,628 % del círculo que se quería dibujar, frente al 0,597 %
que ya pone el pulso: simplificar no añade casi nada por encima del temblor.

El suelo de un píxel rompe a propósito la invariancia de escala en las zonas
diminutas. Por debajo del píxel no hay información que conservar, así que una
zona pequeña se simplifica relativamente más — y eso está apuntado en un test
para que no parezca un descuido cuando alguien vea que un círculo de radio 40
sale con menos vértices que uno de radio 400.

##### La zona guardada no puede recortar con su modo apagado

`WorkingZoneMode::Free` es un cuarto modo junto a «imagen entera», «automática»
y «fija», y `vision::effectiveWorkingPolygon` es quien decide si el polígono
guardado se aplica. Sin esa función, una zona dibujada otro día seguiría tapando
media imagen con el operador viéndola apagada en el panel: la peor clase de
fallo que puede tener una zona, porque el programa mide bien dentro de un sitio
que nadie eligió. Dibujar la libre la pone en uso y borrarla apaga su modo
(`modeAfterFreeZoneChanged`), igual que con la rectangular, y las dos conviven —
la del rectángulo sigue guardada mientras manda la libre.

**El cuarto lo encontró el banco de pruebas** (`tests/test_working_zone.cpp`),
y es el más instructivo porque el test que demuestra que recortar no cambia la
medida **no podía verlo**: lo que el recorte se lleva por delante no es la
precisión de la pieza mayor, son las otras cinco.

Con la zona en Automático el **recuento de piezas siempre daba 1**. El recorte
rodea a una pieza —la mayor— con un 35 % de margen, así que contar dentro de él
da 1 por construcción. Medido sobre una escena de seis piezas: el recorte ocupa
el 17,2 % del frame y dentro se ve **una**. Al operador le llegaba entero: abrir
*Configurar ▸ Piezas* le enseñaba «Se ven 1 pieza(s) y se esperan 6» con las
seis en la mesa, y «Usar detectadas» le ofrecía guardar el valor equivocado.

La regla que lo arregla vive en `vision::effectiveWorkingZone`, no en la
ventana: **cuando alguien va a leer el recuento, el modo automático suelta el
recorte**. La zona FIJA no cede, y la diferencia no es un descuido — el operador
la dibujó diciendo «mira solo aquí», así que ahí dentro está su respuesta; la
automática es una optimización, y una optimización que cambia una respuesta no
es una optimización, es un fallo.

El primer arreglo traía su propia trampa, y por eso la condición es la que es:
soltar el recorte «mientras el panel Configurar esté abierto» dejaba el panel de
*Rendimiento* —que es justo donde se enciende la zona automática— diciendo
«Procesando la imagen entera». El operador la encendía y la veía apagada por
estar mirándola. Ahora se cuenta cuando alguien **lee** el número: la pieza
espera varias, o la pestaña *Piezas* es la visible
(`ConfigureDialog::showingPieceCount`, resuelto por widget y no por índice).

Queda un caveat **documentado a propósito y sin tocar**: `min/maxAreaFraction`
se aplican sobre el área del *recorte*, no del frame, así que la zona cambia el
criterio de aceptación. Escalarlos al frame completo lo arreglaría y rompería
algo mejor: ese suelo de área es lo que permite ver el trozo de pieza que asoma
cuando se está saliendo, y sin él «se sale» degeneraría en «se dejó de ver» tres
frames más tarde.

### Los fps que se enseñan, y los que importan

La barra de estado decía `640x480 — 8.0 fps` y esos eran los fps de **captura**,
contados en el hilo de la cámara. El análisis descarta frames cuando no llega
—`maybeStartAnalysis` se salta el frame nuevo si el anterior sigue corriendo, y
el pendiente se pisa— y **eso no aparecía en ninguna parte**. Una cámara a 30
fps con el análisis a 8 se ve perfectamente fluida, porque el vídeo no depende
del análisis, y está midiendo uno de cada cuatro.

Ahora se cuentan tres cosas y se enseñan **solo cuando hacen falta**: forma
corta (`1280x720 — 30.0 fps`) mientras el análisis sigue el ritmo, y larga
(`30.0 fps · analiza 8.0 · descarta 22`) cuando no. Un indicador que enseña tres
números a todas horas se deja de leer, y entonces tampoco avisa el día que hay
algo que ver.

Dos detalles que no son cosméticos:

- **El umbral de descarte no es cero** (son 2/s). Dos contadores por ventana
  deslizante no dan lo mismo aunque el análisis vaya sobrado: basta con que un
  frame caiga al otro lado del borde. Un descarte suelto es aliasing de la
  medida, no un problema.
- **Congelar el contorno no es descartar.** Con el contorno oculto no se analiza
  a propósito; contar esos frames como descartados sería llamar avería a lo que
  el operador acaba de pedir. Se pasa un −1 para pedir la forma corta en vez de
  enseñar un cero que parecería una caída.

La contabilidad vive en `ui/rate_readout.h` (`FrameAccounting`) y no dentro de
la ventana, por la razón de siempre: `MainWindow` no tiene banco de pruebas y
aquí está lo único que puede estar mal. Con el reloj inyectado se simula una
cámara a 30 fps y un análisis de 125 ms sin cámara ni análisis, y sale **7
medidos y 22 descartados**. La invariante que se exige es que **cada frame que
llega o se mide o se descarta**: si esa suma no cuadra, el número que ve el
operador miente, y un indicador de rendimiento que miente es peor que no
tenerlo.

### Lo que cuestan las herramientas, que resultó ser el primer puesto

Todo el reparto de tiempos de más abajo se midió **sin herramientas dibujadas**.
Una plantilla real lleva diez o veinte y se ejecutan en cada frame, así que
faltaba el número más importante. Medido sobre 900×600 con herramientas del
tamaño de las que se dibujan de verdad —un Eje que cruza 620 px con 64 cortes,
una Rosca con 400, una Ranura con 200, una Región y un Máx./mín. sobre la pieza
entera—:

| Herramientas | ms por frame | % de un frame a 30 fps |
|---|---|---|
| 1 | 0,7 | 2 % |
| 5 | 7,0 | 21 % |
| 10 | 13,9 | 42 % |
| **20** | **27,8** | **83 %** |

Con veinte herramientas, `runTools` **es el mayor coste del frame con
diferencia** — más que la segmentación, el fixture y el recorte juntos, que
suman ~3,5 ms sobre esta imagen. Sumado al análisis, veinte herramientas no
caben en los 33,3 ms de un frame a 30 fps: el análisis empieza a descartar, y
por eso R1 existe.

**Crece lineal** (13,9 → 27,8 al doblar), y eso es la respuesta a la pregunta
que traía el asunto: **no hay trabajo repetido entre herramientas que quitar**.
Cada una escanea su propia región y lo que cuesta es escanearla. Si algún día
dejara de ser lineal, habría aparecido trabajo compartido y entonces sí valdría
la pena buscarlo — el test lo vigila.

Un aviso de método, porque estuvo a punto de salir la conclusión contraria: el
primer banco usó las geometrías de prueba que ya existían y dio **0,5 ms con
veinte herramientas**, que habría cerrado el asunto con un «despreciable». Eran
de juguete —un Eje de 50 px con 12 cortes cuando el real cruza 400 con 64— y el
coste de estas herramientas está dominado por el número de cortes. Medir el
tamaño equivocado no da un número impreciso: da la conclusión contraria.

### Las herramientas se reparten entre hilos, y por qué se podía

R3 dejó claro dónde estaba el tiempo: veinte herramientas cuestan 27-34 ms en
serie, o sea **el frame entero**. Y como crece lineal, no había trabajo repetido
que quitar — solo quedaba hacerlo a la vez.

Lo que hizo que fuera seguro no fue una revisión de cada herramienta, sino que
**la estructura ya lo garantizaba**. `runTools` avanza en **ondas**: una
herramienta solo entra en una onda cuando todas sus referencias se intentaron en
ondas *anteriores*. Por construcción, entonces, ninguna herramienta de una onda
lee lo que otra de esa misma onda va a producir. El orden de dependencia que ya
existía por corrección resultó ser también el permiso para repartir.

Lo único que había que cambiar era dónde se escribe: cada herramienta deja su
resultado en **su hueco** de un vector, y el mapa de referencias se actualiza
después, en serie. Escribir en el mapa dentro del bucle habría sido la carrera
de datos evidente, y no hacía falta para nada.

Se usa `cv::parallel_for_` y no hilos a mano: OpenCV ya está aquí, ya tiene su
reparto y respeta el número de hilos que se le haya puesto al proceso.

Medido sobre la misma pieza quieta y la misma plantilla, con 8 núcleos:

| Herramientas | En serie | Repartidas | Ganancia | % de un frame a 30 fps |
|---|---|---|---|---|
| 5 | 6,9 ms | 4,6 ms | 1,49× | 21 % → 14 % |
| 10 | 14,4 ms | 5,0 ms | 2,87× | 43 % → 15 % |
| **20** | **33,8 ms** | **9,1 ms** | **3,72×** | **102 % → 27 %** |

Con veinte herramientas pasa de **no caber en un frame** a ocupar un cuarto.

Las tres pruebas, en este orden a propósito: primero que **da exactamente las
mismas cifras** que ejecutándolas de una en una —medida, veredicto y detalle,
porque en el detalle van los avisos y uno que aparezca según qué hilo tocó sería
peor que no tenerlo—; después que las **referencias siguen resolviéndose**, con
veinte pasadas seguidas, porque una carrera de datos no falla a la primera; y
solo entonces el cronómetro. Una optimización que altera lo que se mide no es
una optimización, es un fallo más rápido.

**La escala de trabajo adaptativa se descarta definitivamente.** Aceleraba la
segmentación, que con herramientas dibujadas son ~1,6 ms de 31: el 1,33× que
prometía se quedaba en menos de un 2 % del frame. Se midió dónde estaba el
tiempo antes de optimizar, y estaba en otro sitio.

### El desglose de tiempos que la propia app puede dar

Los tiempos de la sección siguiente se midieron una vez, con un programa suelto.
Sirvieron para decidir entonces y no dicen si hoy sigue siendo verdad en otra
máquina, con otra resolución y con veinte herramientas dibujadas — que es justo
cuando alguien se pregunta dónde apretar.

`analyzeFrame` acepta un `StageTimings*` opcional. **Con puntero nulo no se
llama al reloj ni una vez**, y eso es lo que permite dejarlo apagado por defecto
en el camino más caliente del programa: un cronómetro por etapa que corriera
siempre sería pagar en cada frame para que nadie lo mire. Se enciende desde la
pestaña *Rendimiento* y se apaga al terminar.

`runTools` se cronometra aparte, en `buildOverlay`, porque corre **fuera** de
`analyzeFrame` —sobre su resultado— y en una plantilla real puede ser el mayor
coste de los cinco.

Tres propiedades que el desglose tiene que cumplir, y que los tests exigen:

- **Cronometrar no cambia lo medido.** Si tomar tiempos obligara a copiar algo o
  a tomar otra rama, el reparto describiría un análisis que no es el que corre
  en producción. Se compara el recorte canónico **píxel a píxel** entre la
  llamada con reloj y la llamada sin él.
- **Las etapas suman el total.** El total se mide de punta a punta y las etapas
  por separado, a propósito: así, lo que no cuadre aparece.
- **El hueco se enseña.** `unaccounted()` es el tiempo que el desglose no
  atribuye a nadie —trabajo entre etapas: rellenar la máscara limpia, llevar los
  resultados al marco completo—. Si crece, hay trabajo real fuera de lo medido y
  el reparto estaría señalando el sitio equivocado. Esconderlo convertiría el
  medidor en un adorno.

Media móvil de las últimas 30 ejecuciones (`StageStats`), con ventana fija y sin
reservar memoria: esto lo alimenta el camino más caliente, y un contenedor que
crece es exactamente el coste que un medidor de rendimiento no puede
introducir. Media y no último valor porque el coste de un frame varía con lo que
haya en la imagen, y quien abre la pestaña quiere el comportamiento, no una
muestra.

### Dónde se va el tiempo de un análisis (y por qué no hay escala adaptativa)

Se planificó una «escala de trabajo adaptativa»: segmentar una copia reducida
de la imagen cuando la pieza es grande. Se implementó, se midió y **se retiró**,
porque la premisa era falsa. Conviene dejar escrito el reparto real del coste
para no volver a intentarlo.

Medido sobre 2560×1440 con una pieza que ocupa casi todo el frame:

| Etapa | Tiempo | % |
|---|---|---|
| `analyzeFrame` completo | 35,4 ms | — |
| `segmentPiece` (suavizado + Otsu + morfología) | 8,3 ms | 23 % |
| `findLargestContour` | 2,0 ms | 6 % |
| `computeFixture` (momentos sobre la máscara) | **14,1 ms** | **40 %** |
| `normalizePiece` (recorte canónico) | **12,1 ms** | **34 %** |

Segmentar a 1/4 baja esos 8,3 ms a 2,1 ms contando el remuestreo de ida y
vuelta: **1,10× medido** sobre el total. Un 10 % no justifica un modo nuevo en
la interfaz ni una forma más de equivocarse.

Lo que sí quedó demostrado, y sirve si algún día hace falta: reducir **no mueve
las medidas** si el contorno se recupera a resolución completa. Segmentando a
1/4, subiendo la máscara con interpolación bilineal y volviendo a umbralizar, el
fixture salía a **±0,000 px** del de referencia y las cotas idénticas. Lo que no
vale es escalar los puntos del contorno, que son enteros: eso los cuantizaría al
factor.

El coste real estaba en **los momentos sobre la máscara completa** y en **el
recorte canónico**. Los dos se arreglaron:

- **`computeFixture` recorría la máscara dos veces** (tres con `autoOrient`),
  porque el centroide, la anisotropía y el ángulo se pedían por separado y cada
  uno llamaba a `cv::moments`. Ahora los momentos se calculan **una vez** y se
  comparten, y **sobre la envolvente de la pieza** en vez de sobre la máscara
  entera. Es exactamente equivalente: los momentos centrales son invariantes a
  la traslación, así que el ángulo y la anisotropía no cambian, y al centroide
  solo hay que sumarle la esquina del recorte.
- **`normalizePiece` hacía dos `warpAffine` de imagen completa incluso sin
  giro**, que es el caso **por defecto** (`autoOrient` es false: la pieza se
  deja vertical). Una rotación identidad aplicada a 3,7 millones de píxeles, dos
  veces. Ahora, sin giro, se recorta la envolvente y ya está. El camino con giro
  sigue igual.

Ambas son optimizaciones de código que ya funcionaba, así que la prueba que vale
no es "da un número razonable" sino **"da exactamente lo de antes"**: los tests
llevan una implementación de referencia con el código original y comparan contra
ella. El recorte canónico sale con **0 píxeles distintos**.

Resultado sobre la misma escena:

| Etapa | Antes | Ahora |
|---|---|---|
| `analyzeFrame` completo | 35,4 ms | **23,1 ms** (1,53×) |
| `computeFixture` | 14,1 ms | 5,2 ms |
| `normalizePiece` | 12,1 ms | 5,7 ms |
| `segmentPiece` | 8,3 ms | 7,8 ms |

Con eso, **la segmentación pasa a ser el mayor coste** (34 % de los 23 ms). Si
alguna vez hiciera falta más, ahí sí volvería a tener sentido mirar la escala de
trabajo: sobre el total de hoy valdría ~1,33× en vez del 1,10× que se midió
antes. Sigue sin justificar un modo nuevo en la interfaz.

### Una sola regla para el tablero, y una trampa de la fila entera

El tablero de referencia estaba **duplicado**: claves globales `board_*` en
`Settings` y columnas en `Pieces`. Al seleccionar pieza ganaba la pieza, pero
tocarlo desde el menú *Ver* escribía el global. La regla, ahora escrita y
cumplida: **el ajuste global es solo la plantilla para piezas nuevas**. Con una
pieza seleccionada, todo cambio va a sus columnas y **no se toca el global** —
pisarlo haría que la siguiente pieza naciera con el tablero de la anterior.

Y una trampa que costó un fallo real: **`saveMeasurement` escribe la fila
entera**. `persistBoardConfig` construía un `PieceMeasurement` nuevo para
cambiar solo el tablero, así que ponía a su valor por defecto todo lo que esa
función no tocaba — y cambiar el origen del tablero **borraba en silencio las
piezas esperadas** de la pieza, añadidas en C5. Ahora se lee, se modifica y se
escribe, y hay un test que lo fija. La regla general para esa fila: **cargar
antes de guardar, siempre**.

### El editor mide con lo que el operador puso

Fallo real encontrado en el inventario: `EditorWindow` llamaba a
`vision::analyzeFrame(image)` **sin `PipelineConfig`** en sus tres puntos de
análisis. El editor detectaba con Otsu, polaridad automática y sin zona, diera
igual lo que el operador tuviera configurado — así que **lo que se dibujaba
encima y lo que luego se inspeccionaba podían no ser la misma pieza**.

La configuración se pasa ahora en el constructor. El test lo fija con una escena
donde el ajuste **decide**: fondo oscuro, una pieza grande gris medio y una
pequeña muy clara. Con Otsu gana la grande (420×320); con umbral manual 180 gana
la clara (238×198). Si el editor volviera a ignorar la configuración, el test lo
diría.

Se intentó primero con la **polaridad** sobre una escena de tres niveles y se
descartó: con un histograma trimodal, dónde cae Otsu no es predecible y el test
medía la suerte en vez de la configuración.

### El panel «Configurar»: un solo sitio

`ui/configure_dialog.*` es un `QTabWidget` que aloja las páginas de ajuste.
Nació de un problema medido: los ajustes vivían en **siete diálogos colgados de
cuatro menús**, y para cambiar el enfoque y el umbral había que saber que uno
estaba en *Cámara* y el otro en *Inspección*.

Tres decisiones lo definen:

- **No es modal.** Ajustar un umbral o un enfoque consiste en mover y mirar: con
  un diálogo modal encima del vídeo no se ve el efecto de lo que se toca. Por
  eso se abre sin bloquear y trae *Aplicar* además de *Aceptar*. Solo puede
  haber uno abierto; volver a pulsar lo trae al frente en vez de apilar paneles.
- **El panel no aplica nada; avisa.** Emite `applied()` y la ventana lee las
  páginas. La excepción es la de cámara, que **aplica al instante** porque un
  deslizador de enfoque con efecto diferido sería inservible. Esa asimetría es
  deliberada y está escrita en la propia página.
- **Dos pestañas abren un asistente en vez de fingir ser un formulario.** La
  escala se calibra haciendo clic en dos puntos de una foto y los atajos son una
  tabla que se edita: meterlos a la fuerza aquí los haría peores, no mejores.

Los diálogos que eran formularios puros (`DetectionDialog`, `PreferencesDialog`,
`CameraControlsDialog`) pasaron a ser páginas (`DetectionPage`,
`PreferencesPage`, `CameraImagePage`): mismo cuerpo, sin barra de botones ni
título de ventana. No se dejaron cáscaras `QDialog` porque `MainWindow` era el
único que las abría y habrían quedado como código muerto.

Al parar la cámara el panel **se cierra**: sus deslizadores de cámara no harían
nada y quedarse abierto sería mentir. La pestaña que estaba visible se guarda
(`config_last_tab`) porque quien pelea con la iluminación vuelve diez veces a la
misma.

---

## 3. Detección de la pieza (visión clásica)

Aquí **no hay IA**. Es una cadena de visión clásica, elegida porque es rápida,
predecible y no necesita entrenar nada:

1. **Suavizado** gaussiano opcional para quitar ruido de sensor.
2. **Umbralización de Otsu** (umbral automático global) o umbral manual fijo. La
   polaridad —pieza oscura sobre fondo claro o al revés— se decide sola o se
   fuerza.
3. **Morfología matemática** (apertura y cierre) para cerrar huecos y borrar
   motas.
4. **Extracción de contornos** por seguimiento de bordes (`findContours`), y se
   toma el contorno de mayor área dentro de unas fracciones mínima y máxima del
   encuadre: así una mota no pasa por pieza, ni una sombra que ocupa todo.
5. **Momentos de imagen** para el centroide y el eje principal, y `minAreaRect`
   para el rectángulo mínimo que la contiene.

**Zona de detección (ROI)**: todo lo anterior puede restringirse a un rectángulo;
los resultados se devuelven en coordenadas de la imagen completa.

**Perfiles de detección**: el juego de ajustes (umbral, polaridad, kernels) se
guarda con un nombre ("luz brillante", "contraluz") y se asigna **a cada pieza**,
porque la misma línea puede tener piezas que necesitan iluminaciones distintas.

Límite real, medido: con un **degradado de sombra** fuerte, el Otsu global falla
—el "contorno" se come medio fondo— y hay que pasar a umbral manual. Está fijado
en un test para que no se olvide.

---

## 4. Sistema de coordenadas de la pieza (Position Fixture)

El concepto central. Cada pieza detectada define su propio sistema de
coordenadas: **origen en su centro y eje X sobre su eje principal**. En la
industria esto se llama *reference frame*, *datum* o *fixturing* (es el
vocabulario de Cognex VisionPro y MVTec Halcon).

Las herramientas **se guardan en coordenadas de pieza**, no de imagen. Por eso,
si la pieza llega desplazada o girada, las herramientas la siguen sin tocar
nada. Está verificado por tests: la misma barra medida con la pieza a 0°, 20°,
45°, 70° y 120° da el mismo número.

**Estabilizador temporal.** El fixture medido en cada frame se compara con el
mostrado en el anterior:

- Movimiento por debajo de la banda muerta → **la vista se clava** (el ruido de
  medición no mueve nada).
- Movimiento moderado → media exponencial, para seguir sin vibrar.
- Movimiento grande → salto inmediato, para no arrastrarse por detrás.
- **Piezas casi redondas**: por debajo de un umbral de anisotropía el eje
  principal es puro ruido, así que **el ángulo se congela**.
- **Giros espurios de 180°**: el signo del momento de tercer orden es inestable
  en piezas simétricas. Si el ángulo salta media vuelta, se conserva el sentido
  anterior y se avisa al llamador para que rehaga el recorte normalizado.

**Rasgo distintivo.** Para piezas realmente simétricas, el operador marca un
punto visualmente único (un agujero, una marca). Ese rasgo es la verdad: fija la
orientación en cualquier rotación, incluida la de 180° que los momentos no
distinguen.

---

## 5. Herramientas de medición

**32 herramientas**, todas ancladas al fixture. El motor común
(`tool_executor.cpp`) recibe imagen + fixture + configuración y devuelve un
resultado con la medida, el veredicto y los puntos para dibujarla.

La tabla va **por familias**, en el mismo orden que `allToolCategories()`, para
que se lea igual aquí que en la paleta.

| Herramienta | Qué mide | Técnica |
|---|---|---|
| **— Figuras básicas —** | | |
| Borde liso | Desviación máxima de un borde que debería ser recto | Ajuste de recta + máxima distancia de los puntos detectados |
| Blob | Conteo de manchas en un rectángulo | Umbral por polaridad + contornos externos filtrados por área mínima |
| Blob poligonal | Igual, en una región de forma libre | Máscara poligonal + los mismos contornos externos |
| Región | Área, perímetro, solidez, circularidad, relación de aspecto o agujeros | Contorno mayor dentro del recuadro + momentos y casco convexo; el perímetro por cadena **Vossepoel–Smeulders**, no `arcLength` |
| Simetría | Grado de simetría de la silueta, de 0 a 1 | Busca el **mejor** eje de simetría y compara la silueta con su reflejo. No es la simetría de GD&T, que se retiró de la norma en 2018 |
| Lados | Cuenta los lados de un perfil poligonal y mide cada uno y sus ángulos | Simplificación del contorno con **epsilon en milésimas del perímetro** (no en px, para que el recuento no cambie al acercar la cámara). Si el recuento no aguanta al doblar y partir ese valor, la figura no es un polígono claro y lo dice |
| Rebabas y mellas | Defectos de borde, con su tamaño y dónde están | Barrido perpendicular al contorno + rachas contiguas fuera de banda (una rebaba más ancha que la ventana también cuenta) |
| **— Medición en línea —** | | |
| Caliper | Distancia entre dos bordes | Perfil de intensidad promediado en banda + gradiente + refinamiento subpíxel parabólico; empareja bordes de **polaridad opuesta** |
| Círculo | Diámetro y redondez | Rayos radiales buscando el borde + ajuste de círculo por mínimos cuadrados |
| Punto-Línea | Distancia perpendicular de un borde a una recta | Escaneo perpendicular + proyección sobre la recta |
| Regla | Distancia directa entre dos puntos | Geometría pura (no busca bordes) |
| Línea-Línea | Ángulo entre dos rectas | Producto escalar de direcciones |
| Ángulo | Ángulo interior de una esquina | Ángulo entre dos vectores desde el vértice |
| Arco | Radio de una esquina o un redondeo | Círculo por tres puntos para situar el sector + barrido radial acotado a él + ajuste Taubin robusto |
| Holgura | La separación **más corta** entre dos figuras, y dónde está | Las dos figuras mayores del recuadro + distancia mínima entre contornos. Si solo se ve una, puede que se estén tocando, y se dice: cuánto solapan no está en la silueta |
| **— Construcciones —** (no miden: fabrican referencias) | | |
| Punto construido | Una intersección, un punto medio o un centro | Geometría sobre elementos que dan otras herramientas (`DerivedElements`) |
| Recta construida | Una bisectriz, una perpendicular o una paralela | Igual; la dirección se canonicaliza para que no dependa de cómo se trazó |
| Eje medio | La línea que va por el centro de una pieza alargada, y su rectitud | Punto medio entre los dos bordes **reales** corte a corte, no la línea que trazó el operador; avisa de la desalineación entre tramos |
| **— GD&T —** (siempre contra un marco de referencia declarado) | | |
| Posición | Desviación de un rasgo respecto al cero del tablero (radial, X o Y) o **posición verdadera** contra un marco de referencia | Lectura en el sistema del tablero; con datums declarados, el diámetro de la zona `2·√(dx²+dy²)` en ese marco, que es como lo acota el plano |
| Rectitud | Rectitud por **zona mínima** | `minimumZoneBand`: casco convexo + calípers giratorios (la banda más estrecha, no la de área mínima) |
| Redondez | Redondez por **zona mínima** | `minimumZoneCircle`: Nelder-Mead sembrado con Taubin (MZC, no mínimos cuadrados) |
| Orientación | Paralelismo/perpendicularidad/angularidad contra un datum | Ancho de banda, que es una **distancia** y no un ángulo — como lo define la norma |
| Desviación de centros | Cuánto se desplazan dos centros entre sí | Distancia entre los dos centros ajustados. **No es concentricidad**, y por eso no se llama así |
| Patrón de agujeros | Si un patrón de agujeros está donde toca | Ajuste del patrón nominal + el peor agujero identificado **por su posición angular**, no por un índice |
| Perfil de línea | Desviación de un borde respecto a un perfil nominal | Perfil nominal capturado del contorno de la pieza patrón, en coordenadas de pieza; comparación punto a punto **sin ICP** (el fixture ya alinea) |
| **— Máx./mín. y torneadas —** | | |
| Eje / Diámetro | Diámetro, conicidad y rectitud de una pieza de torno | Perfil axial a los dos lados + ajuste robusto de recta a cada borde; el diámetro es la separación entre las dos rectas |
| Rosca | Paso, Ø exterior, Ø de fondo y ángulo de flanco (con el sesgo de hélice cuantificado) | Perfil axial a los dos lados + periodo por autocorrelación (el paso) + plegado síncrono por ese periodo y ajuste de recta a los flancos |
| Engranaje | Dientes, Ø de cabeza y raíz, módulo, Ø primitivo, excentricidad | Perfil radial + periodo circular (los dientes) + ajuste de círculo a las puntas (la excentricidad) |
| Máx./mín. | Anchura mínima y diámetro máximo, **en cualquier dirección** | Casco convexo + calípers giratorios; las dos se dan siempre con su dirección |
| Chaflán | Ángulo del bisel y sus **dos catetos** | `decomposeContour` + ajuste de tres rectas e intersección para construir la **esquina virtual**, que es de donde acota el plano |
| Radio de acuerdo | Radio del redondeo **y si empalma tangente** | Arco de `decomposeContour` + ángulo entre su tangente en cada extremo y la recta vecina |
| Ranura | Ancho, profundidad y Ø de fondo de una entalla | El mismo perfil axial que el Eje pero **sin ajustar recta**: la ranura es justo donde el borde se sale de esa recta. Mínimo local del perfil crudo + cruce con el nivel de media profundidad en cada flanco. El ancho sale de **contar cortes**, así que se rechaza si la ranura no abarca al menos tres |

### Por qué zona mínima y no mínimos cuadrados en GD&T

Rectitud y Redondez podrían salir de un ajuste por mínimos cuadrados, que es lo
que ya hace el Círculo, y sería más corto de escribir. No se hace, y la razón no
es estética: **la norma define otra cosa**. Una tolerancia de forma es el ancho
de la **zona más estrecha que contiene todos los puntos**, no la dispersión
alrededor del elemento mejor ajustado. Mínimos cuadrados minimiza la suma de
cuadrados; la zona mínima minimiza el máximo. Son problemas distintos y dan
números distintos, y el de mínimos cuadrados sale **siempre igual o mayor** —
o sea que rechazaría piezas buenas.

Con la misma lógica se descartó `cv::minAreaRect` para la rectitud: minimiza el
**área** del rectángulo, no su **ancho**. Sobre un triángulo el rectángulo de
área mínima sale un 41 % más ancho que la banda de ancho mínimo, y sobre un
equilátero su diagonal se pasa un 32 % del diámetro real. Es una función que
existe y que casi encaja, que es exactamente el tipo de atajo que hay que
escribir por qué no se tomó.

De ahí `minimumZoneBand` (casco convexo + calípers giratorios) y
`minimumZoneCircle` (Nelder-Mead sembrado con Taubin) en `vision/fitting.*`. El
invariante que los guarda es barato y fuerte: **la zona mínima nunca puede dar
más que su equivalente por mínimos cuadrados**, porque la banda alrededor de la
recta de mínimos cuadrados es una candidata más entre todas las orientaciones y
la zona mínima es el mínimo sobre todas. Si un día diera más, hay un error de
implementación, y el barrido lo dice.

El barrido comprueba además que **no coinciden**: sobre 200 nubes de puntos la
zona mínima gana estrictamente en las 200, y sobre 100 perfiles circulares la
MZC gana en los 100. Sin esa segunda mitad, un test que solo exigiera «≤»
pasaría igual de verde con las dos funciones devolviendo lo mismo, que es
exactamente el error que buscaría.

### Quién decide qué herramientas llevan referencia

`referenceOperandsOf(const ToolGeometry&)`, en el modelo, y solo ahí. Antes lo
decidía el panel del editor por su cuenta, y la pregunta que se hacía —«¿es una
construcción?»— dejaba fuera a **Posición, Orientación y Desviación de
centros**: medían contra una referencia que desde el editor no había forma de
asignarles, así que solo se podían configurar editando la plantilla a mano.

Lo encontró el barrido de coherencia del cierre, y la forma de encontrarlo es
la parte reutilizable: recorrer **todas** las herramientas, ejecutarlas sin
referencias y exigir que **toda la que se queje de que le falta un datum lo
declare**. Dos sitios que tienen que estar de acuerdo —el ejecutor que se niega
a medir y el panel que ofrece los desplegables— no se mantienen sincronizados
por buena voluntad: o comparten la fuente o divergen.

`OperandKind::PointOrLine` existe por el datum secundario de Posición, que
admite las dos cosas: una recta corta a la primaria y un punto se proyecta
sobre ella, y las dos maneras fijan el origen. Etiquetarlo «recta» habría sido
una mentira pequeña en el sitio donde el operador mira para decidir.

**Las de forma no llevan datum, y eso también hay que decirlo.** Rectitud,
Redondez, Patrón de agujeros y Perfil de línea no toman referencia, y sus
descripciones lo dicen en voz alta desde este repaso. Quien viene de un plano
asocia GD&T con declarar datums: sin la frase, se queda buscando un desplegable
que no existe y lee el hueco como un fallo del programa.

### Referencias entre herramientas

`tools/derived_element.h` y las dos pasadas de `runTools` son el mecanismo del
que cuelgan las construcciones geométricas y todo el GD&T. Existe porque
paralelismo, perpendicularidad, angularidad y posición verdadera **no son
medidas absolutas**: son medidas respecto a una referencia declarada.

- Una herramienta puede producir, además de su medida, un **elemento derivado**
  (punto, recta o círculo) en **coordenadas de pieza**, para que la referencia
  siga a la pieza igual que quien la usa. Las que ya existían lo llenan casi
  gratis: la Regla ofrece su recta, el Círculo su circunferencia **ajustada**
  (no la trazada: el centro que vale para un datum es el que sale del borde
  real) y Posición su punto.
- `ToolConfig::reference` es el **nombre** de otra herramienta, no su id: el
  operador referencia lo que ve escrito en la lista, y una plantilla exportada
  e importada en otra pieza cambia de ids pero conserva los nombres. Se
  persiste dentro de `paramsJson` — esa columna existía sin usarse y es el sitio
  previsto para parámetros por herramienta, así que **no hizo falta migrar**.
- `runTools` ejecuta en **orden de dependencia** pero devuelve los resultados
  **en el orden de la lista del operador**. Si se reordenaran, la tabla de
  resultados bailaría cada vez que alguien añade una referencia.
- **Una referencia que no está no se sustituye por nada.** Si no existe, está
  desactivada o falló al medir, la herramienta **no mide** y lo dice. Nunca cae
  a una referencia implícita: un GD&T medido contra otro datum del que cree el
  operador es exactamente el fallo que este programa existe para evitar, porque
  el número sale creíble y es falso.
- Dos herramientas que se referencian en círculo **fallan diciéndolo** en vez
  de colgar el análisis.

**Ninguna herramienta muda.** Las cinco cadenas de `if constexpr` sobre la
variante terminan ahora en `static_assert(alwaysFalse<T>)`, con un mensaje que
dice qué se rompería: sin `referencePoints` el marco de selección no encuadra;
sin `handlePoints` no se puede editar; sin `setHandlePoint` las manijas se
dibujan pero no se mueven; sin `distanceToGeometry` no se puede seleccionar; sin
`paintTool` la herramienta es invisible.

**El inventario contaba de más y se comprobó.** Se añadió un tipo sonda a la
variante y se miró qué dejaba de compilar. Resultado medido: `referencePoints`,
`handlePoints` y `setHandlePoint` **ya** daban error (su `else` final usaba
miembros que el tipo nuevo no tiene). Las trampas silenciosas eran **dos**, no
cuatro: `distanceToGeometry`, cuya cadena no tenía `else` y dejaba la distancia
en 1e9, y `paintTool`, que tampoco lo tenía.

Y el experimento destapó un fallo ya entregado: **Eje, Rosca y Engranaje no
tenían rama en `paintTool`** desde T2–T4. Se veían solo cuando ya habían medido
—`paintResults` sí dibuja sus puntos— y antes de eso eran invisibles en el
lienzo. Nada lo dijo en su momento. Ya tienen su dibujo: el eje con su banda de
búsqueda a rayas, y el engranaje con los dos aros entre los que busca los
dientes.

Además del `static_assert`, que solo garantiza que la rama **exista**, un
barrido renderiza cada herramienta sobre un lienzo vacío y exige que **pinte
algo**. El compilador no puede comprobar eso.

**La paleta** (`canvas/tool_palette.*`) construye los botones desde
`toolsInCategory()` y la comparten las dos superficies, así que el orden, los
iconos y las descripciones son los mismos en las dos.

Es **un panel**: franja de familias arriba (iconos de `categoryIcon`, que vive
junto a `toolIcon` y con el mismo `switch` sin `default`), el nombre de la
activa, y **todas** sus herramientas en rejilla debajo. Hubo tres formas —una
fila con menús, un acordeón y este panel— y se retiraron las dos primeras al
quedarse sin usos: tres paletas mantenidas a la vez divergen, y este proyecto
ya pagó eso una vez con los botones.

Las dos formas viejas compartían el defecto de fondo: **las herramientas no se
veían**. Con la fila había que abrir un menú, que tapa el vídeo justo cuando
quieres mirar dónde vas a dibujar; con el acordeón, cada herramienta gastaba una
fila entera de alto y con 32 ya no cabía una familia. El panel enseña la familia
completa de un vistazo y se elige en un clic.

Las medidas, que son las que lo justifican:

| | Antes | Ahora |
|---|---|---|
| Fila 3 de la ventana principal | 1049 px de ancho mínimo | **439** (la paleta se fue al dock) |
| Columna del editor | 190 px (acordeón) | **176** |
| Ancho mínimo del panel | — | **176 px**, con reflujo de 4 a 9 columnas entre 180 y 400 |

**El reflujo tenía una pescadilla que se muerde la cola**, y es lo que hay que
saber si alguien lo toca: el mínimo de un `QGridLayout` es el de sus columnas,
así que con las ocho herramientas de una familia en una fila el panel pedía
324 px y Qt no le dejaba estrecharse por debajo — y como no se estrechaba, el
reflujo no llegaba a ocurrir nunca. Se rompe con `QSizePolicy::Ignored` en el
contenedor de la rejilla: acepta el ancho que le den y recoloca dentro. Y el
layout se **rehace** al cambiar de columnas, porque `QGridLayout` no encoge
nunca su número de columnas.

**La línea de ayuda** es lo que repone el nombre que la rejilla le quita a los
botones; sin ella el panel sería más bonito y peor. Su texto sale de
`toolTypeDescription`, no es una copia.

#### El texto se cortaba, y el arreglo anterior solo lo cortó con más educación

Tenía alto **fijo** de tres renglones, con una buena razón: si creciera y
menguara al pasar el ratón por la rejilla, la rejilla botaría bajo el cursor y
elegir sería un juego de puntería. El precio era que el resto del texto vivía
solo en el tooltip.

Medido: **29 de las 32 descripciones no cabían**, y la más larga tiene 901
caracteres. Es decir, casi toda la ayuda de la aplicación solo existía **al pasar
el ratón** — y lo que solo se ve con el ratón encima no lo ve quien navega con el
teclado. Un arreglo anterior había cambiado el corte mudo por unos puntos
suspensivos, que es mejor y sigue siendo un corte.

La regla se conserva y cambia **cómo se cumple**: el alto lo fija el sitio que
sobra en el panel, no el largo del texto. La ayuda vive en un `QScrollArea` que
se queda con ese hueco —que antes se iba en un `addStretch`, espacio vacío bajo
una ayuda truncada— y el texto se desplaza dentro. La rejilla sigue sin moverse
y cabe la descripción entera. El tooltip se retira: repetir en un globo lo que
ya está escrito debajo solo tapa el texto que se está leyendo.

Medido después: la descripción más larga necesita **338 px** y a 900 px de panel
el hueco da **706**, así que se lee entera sin desplazar; con el panel a 320 px
quedan 126 px de hueco para 351 de texto, y hay **225 px que desplazar** en vez
de 225 px perdidos.

Dos cosas que costaron un test cada una:

- **Una etiqueta con ajuste de línea dentro de un `QScrollArea` redimensionable
  no crece sola.** Qt le da el alto del visor y no consulta `heightForWidth`, así
  que el texto se recortaba exactamente igual — ahora sin puntos suspensivos, que
  es peor. Se corrige fijando el mínimo de la etiqueta a su `heightForWidth` cada
  vez que cambia el texto o el ancho.
- **El primer test no lo veía**, porque comprobaba `text()` y el texto sí estaba
  completo; lo que no estaba era *visible*. Lo destapó el que mira si hay algo
  que desplazar.

Y el test que protegía la estabilidad medía un **proxy** —que la etiqueta no
cambiara de alto— lo que ataba el diseño a una solución concreta. Ahora comprueba
lo que de verdad importa: que la rejilla no se mueva.

En la ventana principal el panel va en un `QDockWidget` (`toolsDock`) con lo que
**actúa sobre la herramienta seleccionada** —«Borrar» y el parámetro de
muestreo—; «Rasgo distintivo», «Fijar escala» y «Guardar plantilla» se quedan en
la barra porque actúan sobre la pieza y la plantilla. El reparto es por
significado, no por hacer sitio.

El `objectName` es estable porque `saveState`/`restoreState` guardan la
disposición por nombre. Un dock nuevo sobre un estado guardado **viejo** es el
caso que hay que probar contra el fichero de verdad, no contra un perfil limpio:
se hizo, la disposición real de esta máquina contiene solo `compareDock` y Qt 6
lo coloca visible igualmente. La salvaguarda que lo recoloca si quedara oculto
se mantiene, porque el fallo que evita es «el operador actualiza y se queda sin
paleta».

Hay un test que exige que **toda** herramienta siga siendo alcanzable a clics
—agrupar no puede esconder nada—, y como solo se instancian los botones de la
familia activa, el barrido tiene que abrirlas todas, igual que el operador.

Los **atajos** pasan a ser *familia + dígito* (`Ctrl+1..5` elige familia, `1..9`
la herramienta dentro) y se generan de las propias familias. La tabla escrita a
mano que había se quedó corta: con catorce herramientas y diez dígitos, Arco,
Eje, Rosca y Engranaje **no tenían tecla**.

**Familias de herramientas** (`ToolCategory`). Cinco: *Figuras básicas*,
*Medición en línea*, *Construcciones*, *GD&T* y *Máximos, mínimos y torneadas*.
Son un **dato**, no el orden en que se pintan los botones, y viven junto a
`allToolTypes()` por la misma razón: con la agrupación escrita en cada
superficie de interfaz, la fila de la vista en vivo y la columna del editor
acabarían agrupando distinto.

Aquí había un recuento herramienta por herramienta —«(3), (7), (2), (1),
(3)»— que quedó obsoleto en cuanto entraron las rondas siguientes y llegó a
estar a la mitad del número real. Se ha quitado a propósito: **el recuento vive
en `toolsInCategory()`**, y un número escrito a mano en un documento no tiene
quién lo desmienta. Lo que sí se escribe aquí es la regla, porque la regla no
caduca.

Un barrido exige que las familias sean una **partición**: cada herramienta en
una y solo una, y las cinco juntas reconstruyen exactamente `allToolTypes()`.
Una herramienta que faltara quedaría escondida en la paleta; una repetida
aparecería dos veces.

*Construcciones* nació vacía **a propósito**, con un test que exigía que lo
estuviera para que el hueco no se olvidara. `X1` la llenó, ese test saltó —que es
para lo que estaba— y la regla es ahora la contraria: **ninguna familia puede
estar vacía**, porque un cajón que se abre para nada es peor que no tenerlo.

### Construcciones geométricas (`X1`)

**Punto construido** y **Recta construida**. No miden: calculan un elemento a
partir de los que ofrecen otras herramientas, para que exista un **datum** que
declarar. Sin eso, paralelismo, perpendicularidad, angularidad y posición
verdadera no se pueden dar: no son medidas absolutas, son medidas *respecto a
algo*, y una herramienta que dijera "paralelismo = 0,08" sin decir *paralelo a
qué* estaría inventándose un número con nombre de norma.

Ocho construcciones, todas trigonometría sobre primitivas que ya se ajustan
—cero algoritmo nuevo—: punto medio, intersección de dos rectas, proyección de
un punto sobre una recta y centro de un círculo; recta por dos puntos,
bisectriz, paralela y perpendicular por un punto.

Decisiones que se tomaron ahí:

- **«Bisectriz» y «recta media» son UNA construcción, no dos.** Cuando las dos
  rectas se cortan, la bisectriz pasa por el corte; cuando son paralelas, pasa
  por el punto medio entre ellas — que es exactamente la recta media. No es un
  caso especial esquivado: es el mismo resultado por continuidad.
- **Las direcciones se llevan a una forma canónica antes de bisecar.** Un vector
  de dirección tiene sentido y depende de hacia dónde arrastró el operador; la
  recta que representa, no. Sin canonizar, la bisectriz de dos rectas
  **perpendiculares** salía a 45° o a 135° según el sentido del trazo. Las dos
  son igual de válidas —con 90° entre las rectas no hay ángulo agudo que
  partir— pero que cambie sola no lo es, porque el datum giraría 90° sin que
  nadie tocara nada. Lo destapó un test que afirmaba invariancia y falló.
- **Un círculo vale donde se pide un punto**: aporta su centro, que es el datum
  natural de un agujero. Exigir un "punto" literal sería una limitación
  inventada.
- **Un resultado informativo** (`ToolRunResult::informative`). Una construcción
  que sale bien **no es un OK**: no ha juzgado nada, así que la tabla escribe
  «—» y no un verde que no significaría nada. Que **falle** sí es NG, porque
  deja sin referencia a todo lo que la usaba.
- **Nada de NaN.** Rectas paralelas que no se cortan y dos puntos coincidentes
  que no definen recta fallan **con motivo escrito**. Un NaN es un número con
  toda la pinta de ser una medida.
- **El ancla no entra en ningún cálculo.** Es solo dónde se escribe el resultado
  y por dónde se agarra la herramienta con el ratón: el elemento construido lo
  dictan las referencias y puede caer fuera de la imagen.

`ToolConfig` gana **`reference2`**, que viaja junto a `reference` dentro de
`paramsJson` — sin migración de esquema, porque esa columna existía sin usarse.
Los params escritos por `X0` (con `ref` y sin `ref2`) se siguen leyendo: una
plantilla guardada antes no puede perder su datum al abrirse.

`runTools` ordena por dependencia con **las dos** referencias y detecta ciclos
que ahora pueden ser largos (A→B→C→A); como no hay un culpable único, el motivo
nombra **a quién espera cada una**.

**Eje medio de la silueta** (`X2`). La tercera de la familia, y la única que
**mira la imagen**: los flancos hay que encontrarlos. Reutiliza entera la
exploración del Eje torneado —dos perfiles axiales, uno por lado— y lo que
cambia es qué se hace con ellos: en vez de sumar los dos offsets para dar el
diámetro, se toma el **punto medio** de cada pareja y se les ajusta una recta
robusta.

Eso es lo que hace que **dé igual cómo de descentrado vaya el trazo**: el punto
medio entre los bordes reales no depende de por dónde pase la línea que dibujó
el operador. El test lo fija trazando el mismo eje en tres alturas distintas
(centrado, +25 px y −22 px) y exigiendo el mismo resultado dentro de **±0,3 px**.

Da dos números: la **rectitud** (desviación máxima respecto a la recta ajustada
— la banda mínima que contiene los puntos, que es como se define, y no la
desviación típica: una curvatura en un extremo tiene que salir, no diluirse) y
la **desalineación entre la primera mitad y la segunda**, que es lo que delata
dos tramos de distinto diámetro que no son coaxiales. Esa segunda solo se da si
cada mitad tiene al menos cuatro puntos; con tres, el «ángulo» sería ruido con
unidades.

Solo cuentan las estaciones donde se vieron **los dos** flancos. Con uno solo se
podría suponer el centro por simetría, y eso sería inventárselo justo en la
herramienta que existe para encontrarlo: si no llegan a cinco, no mide y dice
cuántas vio y cuál es el alcance actual.

**Flechas de dependencia en el lienzo.** Cada referencia declarada se dibuja
como una flecha punteada de quien **aporta** el dato a quien lo **consume**, por
debajo de las herramientas y en trazo fino: son estructura, no medida. Sin
ellas, en pantalla se ve una recta construida y las dos de las que sale sin nada
que las relacione, y borrar la equivocada rompe la medida sin aviso. Una
referencia rota **no dibuja flecha**: no hay a dónde llevarla, y una flecha
hacia la nada haría creer que el datum existe.

**Una sola lista de herramientas** (`allToolTypes()`). El repaso de coherencia
encontró que las cuatro herramientas de pieza torneada estaban en el editor de
plantilla y **no** en la fila "Dibujar" de la vista en vivo: existían, se
guardaban, se ejecutaban… y no había forma de dibujarlas sobre el vídeo. El
motivo era prosaico —la fila se construía con una lista escrita a mano que nadie
actualizó—, y la cura es que exista **una** lista: la usan el parseo por nombre,
las dos filas de botones y los propios barridos de coherencia. Lo mismo con
`toolTypeLabel`, que estaba duplicado en las dos ventanas.

Diez pruebas recorren esa lista y exigen a **toda** herramienta lo mismo:
manijas que responden al arrastre y no derivan al re-agarrarlas, puntos de
referencia dentro de su propia huella, distancia de clic que crece al alejarse,
geometría que sigue a la pieza al girar, traslación que la mueve entera, ida y
vuelta por el JSON de la plantilla, nombre corto y nombre interno únicos,
descripción que empieza nombrando la herramienta y explica cómo trazarla,
tolerancias sugeridas que aceptan la pieza con la que se midieron, e icono
propio y no en blanco. Los barridos son `switch` exhaustivos sin `default`: con
`-Werror`, una herramienta decimoquinta **no compila** hasta pasar por todos.

**Ajustes geométricos** (`vision/fitting.*`). El Círculo —y las herramientas de
pieza torneada que vienen detrás— acaban preguntando lo mismo: qué
circunferencia explica esta nube de puntos de borde. Dos decisiones ahí:

- **Taubin, no Kasa.** Kasa minimiza el residuo algebraico sin normalizar, lo
  que pesa de más los puntos lejanos al centro. Sobre una circunferencia
  completa da igual, pero sobre un **arco parcial** el radio sale corto, y
  cuanto más corto es el arco, peor. Medido en el test con un radio de 200 px:
  a 30° de arco Kasa se desvía **10,3 px (5 %)** y Taubin **0,92 px (0,46 %)**;
  a 90°, 0,23 frente a 0,12; con medio círculo o más los dos aciertan. Cuesta
  lo mismo, así que no hay razón para el sesgo.
- **Reponderación robusta.** El borde de una pieza real trae puntos que no son
  del círculo: una rebaba, un reflejo, un rayo que enganchó el borde
  equivocado. Se reponderan con la biponderada de Tukey midiendo la dispersión
  con la MAD, que no se deja arrastrar por los propios atípicos. El resultado
  dice **cuántos puntos acabaron contando**: un borde que descarta la mitad
  puede seguir dando un diámetro perfecto y estar midiendo media pieza, así que
  el Círculo lo escribe en el detalle en vez de callárselo.

Para las **rectas** hay dos decisiones equivalentes: se ajusta por **mínimos
cuadrados totales** (distancia perpendicular, recta descrita por punto y
dirección) en vez del clásico `y = mx + b`, que no puede representar una recta
vertical y se degrada mucho antes de llegar a ella; y la dirección se devuelve
en **forma canónica** para que el mismo conjunto de puntos recorrido al revés no
dé un ángulo girado 180°. Cuando la nube es redonda sí sale una dirección, pero
es ruido: en vez de esconder un umbral dentro del ajuste se devuelve la
**anisotropía** —la misma medida y la misma fórmula que `Fixture::anisotropy`,
0 = redondo, 1 = línea— y decide quien pregunta.

**Recorrer un borde** (`inspection_editor/execution/profiles.*`). Dos formas, y
las dos devuelven una señal **muestreada uniformemente**, que es lo esencial: la
rosca y el engranaje son señales periódicas —el perfil se repite cada paso, o
cada diente— y el paso y el número de dientes salen de su periodo, que un
muestreo irregular falsearía. Por eso una muestra sin borde se devuelve marcada
`found=false` en lugar de omitirla: quitarla desplazaría todas las demás.

- **Perfil radial** `r(θ)`: desde un centro, dónde está el borde en cada
  ángulo. Base del engranaje (una repetición por diente) y del arco.
- **Perfil axial**: a lo largo de un eje, a qué distancia perpendicular está el
  borde de cada lado. Base del eje torneado (el diámetro es la suma de los dos
  lados) y de la rosca (el rizado del perfil es el paso).

Su exactitud está medida contra un **semiplano**, cuyo borde cae en una
coordenada exacta sin rasterizado de curvas de por medio: **sesgo +0,000 px**, y
una barra de 80 px se lee 39,50 + 40,50 = 80,00. Conviene saber que
`cv::circle` con suavizado pinta el disco **0,6 px más grande** que el radio
pedido —desfase constante, comprobado de R=30 a R=150—, así que las pruebas de
exactitud usan bordes duros; el suavizado se queda donde sí aporta, que es
reducir la dispersión entre rayos a menos de la mitad (0,24 frente a 0,64 px).
Traducido: un borde suave da mejor **redondez** y peor **diámetro absoluto**.

**Periodo de una señal repetitiva** (`vision/periodicity.*`). La rosca y el
engranaje son el mismo problema visto de dos maneras, y el paso y el número de
dientes salen del mismo cálculo. Tres decisiones:

- **Autocorrelación, no contar picos.** Contar picos parece más simple hasta que
  la pieza tiene un diente mellado: aparece o desaparece un pico y el recuento
  se descuadra entero. La autocorrelación mira la señal completa — medido con un
  diente arrasado, el periodo pasa de 25,00 a 24,98 y lo que baja es la
  confianza (1,000 → 0,952), que es exactamente la señal que el operador
  necesita.
- **Modo circular frente a lineal.** El perfil radial de un engranaje recorre
  una vuelta y **cierra sobre sí mismo**, así que la correlación da la vuelta y
  usa todas las muestras en todos los desfases; el de una rosca a lo largo del
  eje no cierra. En el caso lineal se le quita además la **tendencia recta**,
  que es como se separa la conicidad de la pieza del rizado que interesa.
- **Corrección del error de octava.** La autocorrelación pica también en los
  múltiplos del periodo, y con un periodo fraccionario el múltiplo puede ganar:
  con periodo real 17,4 el máximo global cae en el desfase 35, porque 2,0115
  periodos alinean mejor que 0,977. Devolver eso sería el doble del paso, o la
  mitad de los dientes — un error grande y silencioso. Se parte del máximo
  global y se comprueban sus submúltiplos **buscando en una ventana**, no en el
  valor redondeado, porque el pico real cae al lado. Verificado con 17, 23, 31 y
  47 dientes (primos, que no dividen bien el muestreo): todos exactos.

**Tolerancias.** Cada herramienta tiene banda mínima y máxima; al crearla se
sugieren automáticamente a partir de lo que mide en la pieza buena (±10 % para
distancias, conteo exacto para blobs, ±2° para ángulos). La tolerancia decide el
OK/NG pero **nunca cambia la medida** — hay un test que lo fija.

**Ayudas de dibujo**: imán al borde detectado mientras trazas, manijas por
extremo para afinar sin redibujar, duplicar y copiar/pegar, y deshacer/rehacer
real.

Límite real, documentado tras encontrarlo en un test: el **Borde liso** solo ve
lo que cae dentro de su ventana de escaneo. Una muesca más profunda que esa
ventana pasa desapercibida y la desviación vuelve a salir baja; hay que subir el
parámetro si se esperan defectos grandes.

**Por qué el Arco existe si ya hay Círculo.** El Círculo pide un centro y un
contorno cerrado; una esquina redondeada no tiene ninguno de los dos. El Arco se
define por **tres puntos sobre el propio arco** —como se comprueba un radio con
una plantilla— y esos tres puntos solo *sitúan* el sector: el radio se mide
después sobre el borde real, barriéndolo y ajustando. Si se devolviera la
circunferencia de los tres puntos se estaría midiendo el pulso de quien dibuja,
no la pieza; hay un test que marca los puntos a ojo, desviados unos píxeles, y
exige la misma medida.

**El límite que tiene, medido.** Sobre un arco corto el radio y el centro son
casi indistinguibles, así que un error **sistemático** del borde —no ruido
aleatorio, que se promedia— se amplifica en el radio. Comprobado sobre el mismo
disco de radio 40 dibujado con borde duro: la circunferencia entera se mide
39,99 y un cuadrante del mismo disco, 38,69. No es un defecto del ajuste (F1
mide que Taubin apenas sesga a 90° con ruido aleatorio): es que la
discretización del borde introduce un error correlacionado que un tramo corto no
puede promediar. Con borde suavizado —lo que se parece a una pieza real
retroiluminada— la diferencia baja a unas décimas. De ahí dos consecuencias: las
pruebas del Arco usan borde suavizado, y **la herramienta avisa cuando el tramo
marcado baja de 30°** en vez de dar el número a secas.

**Por qué el Eje no es un preset del Calíper.** Un calíper mide en **un punto**,
y en un punto un cilindro y un cono son idénticos. El Eje explora **a lo largo**:
un perfil axial por cada lado, una recta robusta ajustada a cada borde, y de ahí
salen a la vez el diámetro, la conicidad y la rectitud — los tres números con
los que se acepta una pieza al salir del torno. Verificado con un tronco de cono
dibujado con 40 px de diferencia entre extremos: sobre el tramo explorado la
conicidad esperada es 32,0 y se mide **31,9**.

Un detalle que evita un error de uso frecuente: el diámetro es la **separación
entre las dos rectas ajustadas**, no la distancia a la línea que dibujó el
operador. Por eso descentrar el eje 25 px no cambia la medida (91,44 en los dos
casos, comprobado), que es justo lo que no se consigue encadenando calíperes.
Cuando la banda de búsqueda no llega al borde —pieza gruesa o eje descentrado—,
el aviso dice **el alcance actual y qué hacer**, en vez de un "bordes
insuficientes" que no orienta.

**Cómo se mide una rosca.** El perfil de un tornillo a lo largo de su eje se
repite una vez por vuelta: es una señal periódica, y de su periodo sale el
**paso** (F4). Los diámetros **exterior** y **de fondo** son las crestas y los
valles — media del decil superior e inferior, no el máximo suelto, para que una
rebaba no defina el diámetro — sumando los dos lados, igual que el Eje, para no
depender de que el eje esté centrado.

El **ángulo de flanco** costó dos intentos y merece contarse. El primero fue
estadística de pendientes (mediana de las más inclinadas): estaba mal, porque la
proporción de muestras que caen en el flanco depende de cuánto llano tenga la
cresta, así que el resultado cambiaba con el **paso** en vez de con el ángulo —
daba 42°, 57° y 118° para la misma rosca de 60° con tres pasos distintos. Lo
correcto es **plegar la señal por su periodo** (promediado síncrono, lo que hace
un perfilómetro): al superponer decenas de vueltas alineadas por fase, el ruido
de borde baja y queda la forma del filete; sobre ella se ajusta una recta a la
parte central de cada flanco, del 20 % al 80 % de la altura, para no contaminarla
con el redondeo de la punta ni el del fondo.

Un detalle que costó un tercer intento: el número de casillas del plegado lo
manda el **periodo**, no un valor fijo. Con un periodo de 46 muestras solo
existen ~46 fases distintas, así que pedir 72 casillas dejaba huecos vacíos por
construcción. Y el perfil se toma con **grosor de promediado 1**, no el 3
habitual: ese promediado va perpendicular al escaneo, o sea **a lo largo del
eje**, justo sobre el filete que se quiere medir.

**Hasta dónde llega, medido.** Con roscas sintéticas de ángulo conocido: con
**50 px de altura de filete el ángulo sale a ±1°** (60,01 y 54,98 medidos), con
25 px a ±2°, y con 12 px deja de distinguir 60° de 55° — da ~55 para cualquiera.
El paso y los diámetros aguantan mucho mejor, así que la herramienta **avisa solo
del ángulo** en vez de rechazar la medida entera. Y **sin calibración px→mm se
niega a proponer designación**: un paso en píxeles no identifica ningún tornillo.

**Cómo se mide un engranaje.** El perfil radial desde el centro se repite una
vez por diente, así que **el número de dientes sale del mismo cálculo que el paso
de una rosca**, en modo circular porque una vuelta cierra. Verificado con ruedas
sintéticas de 12, 17, 24, 31 y 48 dientes —incluidos primos, que no dividen bien
el muestreo— todas exactas.

Sobre las puntas detectadas se ajusta un **círculo** (F1): da un centro mejor que
el marcado a ojo y su dispersión **es** la excentricidad, que es lo que se busca
al medir una rueda desgastada. El **módulo** sale de Da = m·(z+2) y sólo con
calibración: es milímetros por diente, y sin escala real no existe.

Dos salvaguardas que se ganaron probando:

- **Doble recuento.** Además de la periodicidad se cuentan los dientes picando el
  contorno, y si los dos números no coinciden se dice. Con un diente rebajado al
  60 %, la periodicidad sigue dando 24 y el conteo por picos da 23: el resultado
  correcto es 24, y el aviso es justo lo que el operador necesita ver.
- **Comprobación cruzada del módulo** por altura de diente (2,25·m en la norma).
  La primera versión dividía la diferencia de **diámetros** por 2,25, pero esa
  diferencia es el **doble** de la altura del diente: el módulo cruzado salía
  exactamente al doble y el aviso de discrepancia saltaba en ruedas
  perfectamente normalizadas. Un aviso que salta siempre es un aviso que el
  operador aprende a ignorar, así que ahora hay un test que exige que **no**
  salte con una rueda normalizada.

### Lo que se dibuja encima de la foto

Un contorno de color sobre una FOTO no tiene contraste garantizado: depende de
lo que haya debajo, y debajo puede haber cualquier cosa. Medido sobre el banco,
el contraste del rojo del contorno contra los píxeles por los que pasa:

| foto | rojo p05 | rojo mediana | con halo p05 | mediana |
|---|---|---|---|---|
| arandelas-1 | 1,02 | 1,22 | 5,57 | 7,27 |
| arandelas-5 | 1,07 | 1,67 | 2,55 | 9,17 |
| engranaje-1 | 1,90 | 2,32 | 11,33 | 13,83 |
| tornillo-1 | 1,59 | 2,13 | 9,46 | 12,67 |
| tornillos-1 | 2,15 | 2,64 | 12,84 | 15,74 |

El color solo no llega ni al **3:1** que necesita un elemento gráfico. Con el
borde oscuro debajo pasa de 5 a 15. **Lo que hace visible el contorno no es su
color: es el halo.**

El lienzo del editor ya lo dibujaba. No lo hacían el vídeo en vivo —donde el
operador mira todo el día— ni el informe de inspección, donde se decide si una
pieza se rechaza. Ahora los tres usan `theme::withHalo`.

**Y no era cuestión de elegir mejor color.** Se probó cambiar el rojo por el de
veredicto (`kBadOnDark`, más claro y con su contraste medido sobre superficie
oscura) y sale PEOR en las siete fotos —1,17 contra 1,22 de mediana en la peor—
porque es más claro y las piezas son claras.

`tests/test_overlay_halo.cpp` lo fija con una sola cifra: lo más oscuro que
aparece dibujado encima de la imagen. Con halo baja a **101**; sin él, lo más
oscuro sería la propia línea —220 el verde, 255 el rojo— y sin nada dibujado,
el fondo, **245**. Entre 101 y 220 hay sitio de sobra para un tope que no sea
delicado.

### De una silueta a rasgos medibles

`vision/geometry_features.*` convierte "una lista de puntos" en "cuatro lados y
cuatro esquinas redondeadas de radio 38", que es lo que hace falta antes de poder
proponer medidas solo. El camino hasta que funcionó tiene cuatro decisiones que
salieron de medir, no de suponer:

- **Barrido voraz, no detección de esquinas.** En la unión tangente de una recta
  con un redondeo **no hay esquina**: la dirección no cambia, solo la curvatura.
  Un detector de esquinas se salta justo las transiciones de una pieza
  mecanizada. El barrido —extender un tramo mientras una primitiva lo explique y
  cortar donde se rompe— encuentra por igual la esquina viva y la tangente.
  También se probó partición recursiva por el punto de peor ajuste y se
  descartó: más difícil de razonar y dejaba tramos de 541 px con residuo 11 sin
  partir.
- **Suavizado antes de ajustar.** Sin él, un lado *perfectamente recto* daba
  residuo 1,2–1,4 px por el dentado de la rasterización — el suelo de ruido
  quedaba al nivel de la tolerancia, y con eso no se distingue un rasgo limpio de
  una mezcla. Con una media móvil corta baja a 0,1–0,3 y la tolerancia puede
  apretarse a 0,8.
- **Ajuste de fronteras.** El barrido corta *después* de la transición real
  (sigue creciendo hasta pasarse), así que cada tramo se lleva un trozo del
  siguiente y en un redondeo eso aplana el ajuste: los radios salían 45–57 para
  un radio real de 40. Moviendo cada frontera al punto que minimiza la suma de
  los residuos vecinos, pasan a **38,0–38,6** y los residuos caen un orden de
  magnitud.
- **La costura, fundida solo si de verdad es un rasgo.** El contorno cierra pero
  el barrido es lineal, así que al dar la vuelta deja un muñón: un disco salía
  como "un arco de 701 px y otro de 49". Se funden el primer y el último tramo
  **ajustando su unión y aceptando solo si sigue siendo una primitiva**. Se probó
  antes con heurísticas por clase ("dos rectas cuyos extremos se tocan") y era un
  error de bulto: en un contorno cerrado los extremos *siempre* se tocan en la
  costura, así que fundía dos lados perpendiculares de un rectángulo.

Verificado contra siluetas de geometría conocida: rectángulo → 4 rectas; pieza
en L → 6 rectas; disco → **un** arco (R = 119,5 para 120); rectángulo con
esquinas de radio 40 → **4 rectas y 4 arcos** de R ≈ 38,3.

Los **agujeros** salen aparte, de la jerarquía de `findContours`: un contorno con
padre es un hueco interno, y cada uno es candidato a un Círculo.

### La clave de color de fondo

Queja de uso: «en arandelas-1 el fondo es rojo, y solo detecta las piezas de
color gris o cromado, las demás no las toma en cuenta». Era literal, y el motivo
estaba en la primera línea de `segmentPiece`: `cvtColor(BGR2GRAY)`. **Lo primero
que hacía el programa con una foto en color era tirar el color.**

Sobre un cartón rojo eso pierde justo lo que separa una arandela de latón del
fondo — el tono. Ese rojo cae en **gris 116**, un gris medio. Medido sobre esa
foto, con una veintena de arandelas de acero, latón, cobre, caucho, fibra y
plástico:

| | piezas | área |
|---|---|---|
| por claridad | 7 | 11,4 % |
| por color | **20** | **22,9 %** |

Cómo funciona: se mide la distancia de cada píxel al color del fondo **en Lab**
—que separa la claridad del tono, que es el problema— y esa imagen de distancias
entra en la maquinaria de siempre. Mismo Otsu, misma morfología, misma
recuperación por histéresis. No hay un umbral nuevo que ajustar; lo único que
cambia es qué mide cada píxel. La polaridad deja de ser una pregunta: en una
imagen de distancias la pieza es siempre lo que está **lejos** del fondo.

**Dos caminos que se probaron y no valen**, porque el que quedó no es obvio:

1. **Cortar la distancia con Otsu a secas.** Otsu supone dos poblaciones, y en
   una bandeja de tuercas sobre fondo blanco hay tres: el fondo, el cuerpo
   cromado (cerca del blanco) y el nylon azul del inserto (lejos). El corte caía
   en medio y la máscara marcaba **solo los aros azules**, dejando todo el
   cromado del lado del fondo. Y esto es lo importante: **el recuento de piezas
   decía 100 en los dos casos.** El fallo solo apareció al dibujar las máscaras
   encima de la foto y mirarlas. Un recuento no mide la calidad de una silueta.
2. **Sacar el umbral del ruido del propio marco.** En una bandeja llena las
   piezas **llegan al marco**, así que ese «ruido» sale de las piezas y no del
   fondo: el p99 del borde subía a 161-210 sobre fondo blanco y tres de las cinco
   fotos se quedaban en **cero piezas**.

Lo que sí funciona es la histéresis, y tiene sentido que sea ella: es el mismo
problema que el brillo —parte de la pieza está al nivel del fondo— y para eso se
escribió.

El color del fondo se estima con la **mediana** del marco, no con la media ni con
un percentil: en la bandeja de cien tuercas las piezas tocan el borde, y la
mediana sale (248,244,243) —blanco, correcto— mientras cualquier estadístico que
mire la cola se contamina. Aguanta mientras menos de la mitad del borde sea
pieza. Y se puede **señalar** desde la pestaña *Detección*, que es lo sensato en
un puesto fijo.

#### Señalar el fondo en la imagen

`ui/background_patch_dialog.h`. El operador arrastra un recuadro sobre un trozo
de mesa vacío y `vision::sampleBackground` devuelve la **mediana** del parche.

Sustituye a la rueda de colores de Qt, que era la única forma de «decírselo tú» y
no servía: pedía un RGB que nadie sabe de su propia mesa. Medido sobre el cartón
rojo, señalar la mesa da **12 piezas / 23,9 %** frente a **11 / 22,9 %** con la
mediana del marco.

**La vista previa corre la segmentación de verdad**, con los ajustes que están
puestos, y pinta encima lo que saldría. Podría enseñarse «lo que se parece al
color elegido» con un umbral inventado en la ventana —más rápido y más bonito—,
pero entonces la ventana enseñaría una cosa y el programa haría otra, que es
exactamente el fallo que la ventana viene a evitar. Hay una prueba que compara
los dos números.

**Por qué hace falta enseñarlo.** Un recuadro que cae encima de una arandela en
vez de sobre la mesa deja la escena **del revés**: cero piezas y el 87,8 % del
cuadro marcado. No da ningún error — da detecciones peores durante meses.

`BackgroundSample` trae también la **dispersión** del parche: el p95 de la
distancia Lab de sus píxeles a su propia mediana. Barriendo el banco de fotos
entero con recuadros de 64x64:

| parche | dispersión |
|---|---|
| mesa de estudio, blanca y plana | 0 |
| cartón rojo real, con su veta | 4 |
| cualquiera que pilla pieza | 45 en adelante |
| bandeja de cien tuercas, **el mejor de todos** | 143 |

El corte en 25 no es delicado: hay un factor de diez entre los dos grupos. La
última fila es la que hay que enseñar — en esa foto las piezas llegan a los
cuatro bordes y **no existe ningún parche de fondo**, así que lo honrado es
decirlo y no dejar señalar tuercas creyendo que se señala mesa.

**Lo que se probó con la dispersión y se descartó.** La idea era restarla de la
distancia al fondo, para que la veta de la mesa colapsara a cero. Medido sobre
`arandelas-1` con cinco parches distintos y el pipeline entero: cuando el parche
es fondo de verdad la dispersión es pequeña y no cambia nada (23,9 % → 23,7 %);
cuando no lo es, restarla borra la escena (23,8 % → **2,2 %**). Una pieza de
maquinaria que en el mejor caso no hace nada y en el peor apaga la detección no
se queda «por si acaso». Si algún día aparece una mesa con veta de verdad
—madera, un tapete impreso— se mide entonces.

La dispersión se calcula con `distanceToBackground` sobre el propio parche, y no
con una fórmula parecida escrita al lado: dos definiciones de «lo lejos que está
esto del fondo» acabarían discrepando, y la que avisa dejaría de hablar de lo que
va a pasar.

Nace apagada, como todo lo que mueve una medida. Hay una prueba que comprueba que
con la clave apagada pasar la foto en color y en gris da exactamente la misma
máscara.

**Y por eso hay que avisar.** Una opción apagada que nadie sabe que existe es una
opción que no existe, y quien la necesita es justo el que no va a ir a buscarla:
está viendo que «no detecta bien» y no tiene por qué sospechar del color de su
mesa. La pestaña *Detección* mide la saturación del color de fondo que ya estima
y lo dice con la cifra, con un botón que lo enciende.

El umbral no es una elección delicada: medido sobre el banco, el cartón rojo da
**0,735** y las siete mesas blancas van de **0,000 a 0,020**. Un factor de
treinta y siete, así que el 0,15 cae en tierra de nadie.

Se calla en los dos casos en que estorbaría —mesa gris, y clave ya encendida— y
las dos mitades tienen prueba. La segunda importa tanto como la primera: un aviso
que aparece en todas las escenas se aprende a ignorar en dos días, y entonces
tampoco sirve donde sí hacía falta.

Un límite conocido: una arandela de **plástico traslúcido** sigue sin salir,
porque a través de ella se ve el fondo y no hay color que la separe.

### Medición automática: propuestas, no números

`inspection_editor/auto_measure.*` mira la pieza y propone qué medir. La
decisión que gobierna la función entera: **genera propuestas de herramientas, no
una lista de números**. Unos números sueltos serían un callejón sin salida —sin
tolerancia, sin veredicto, sin guardarse en la plantilla, sin seguir a la pieza
con el fixture, sin aparecer en el histórico—, y todo eso ya existe y funciona
para las herramientas. El operador pasa de *dibujar veinte herramientas* a
*revisar veinte propuestas*.

Qué propone, a partir de la descomposición del contorno y de los agujeros:
**largo y ancho** (rectángulo mínimo), un **Círculo** por agujero, un **Arco**
por redondeo, un **Calíper** por cada par de caras paralelas enfrentadas y un
**Ángulo** por esquina viva.

#### Y si la pieza se REPITE: rosca y engranaje

Salió de una queja del taller: «en unos tornillos, que tienen rosca, usa otras
herramientas en lugar de las correctas, o se pasa». Las dos mitades eran ciertas
y las dos se midieron sobre las dieciséis fotos del banco (la prueba vive en
`tests/test_tool_for_the_piece.cpp` y publica la tabla entera).

**Conocía siete de las treinta y dos clases de herramienta.** Ni la Rosca ni el
Engranaje estaban entre ellas, así que:

| Foto | Proponía | Le tocaba |
|---|---|---|
| `tornillo-2.png` (tirafondo) | 9 «Radio» y 3 reglas | el **paso de rosca** |
| `rosca-1.png` (varilla) | 6 ángulos, los seis de 102° | el **paso de rosca** |
| `engranaje-1.png` (z=20) | 9 «Lado» y 2 arcos | **contar los dientes** |

Los seis ángulos de 102° son el mismo flanco contado seis veces. Los nueve
«Lado» del engranaje son ocho de sus cuarenta flancos de diente, elegidos por
orden de lista — y el número del nombre es un índice interno: se llegaron a ver
**«Espesor 117», «Lado 30» y «Radio 42»**. Con el tope de doce propuestas, esa
muestra arbitraria se llevaba el presupuesto entero y las cotas que sí valen se
quedaban fuera.

Ahora, antes de descomponer el contorno, se pregunta **si la pieza se repite**:
se prueba el Engranaje en la corona y la Rosca a lo largo del eje. Si alguna
mide, se propone —y **se apagan las propuestas por primitiva**, porque esos
tramos ya están medidos donde corresponde: en «z=20 dientes, Ø cabeza, Ø raíz,
excentricidad» y en «paso, Ø exterior, Ø de fondo».

Resultado medido:

| Foto | Antes | Ahora |
|---|---|---|
| `rosca-1.png` | 12 cotas de ruido | Largo, Ancho, **Paso 66 px** |
| `tornillo-1.png` | 12 cotas de ruido | Largo, Ancho, **Paso 34 px** |
| `engranaje-1.png` | 12 cotas de ruido | Largo, **z=20 dientes** |
| arandelas y tuercas | (lo de siempre) | igual: ni rosca ni engranaje |

**Dónde trazar el eje de la rosca, probando.** Con el eje de punta a punta y la
banda al ancho entero, la Rosca no mide **ninguna** de las tres roscas del
banco: en un tornillo ese eje mete dentro la cabeza y el perfil deja de
repetirse. Un operador no lo trazaría así, lo trazaría sobre la caña. Así que se
prueban cinco colocaciones —la pieza entera y los dos extremos, con dos anchos
de banda— y se queda la primera que mida. Probar no es disparar a ciegas
*porque* la herramienta rechaza las que no valen.

**Por qué es seguro proponer una rosca a cualquier pieza:** el generador ya
ejecuta cada propuesta antes de ofrecerla y tira la que no consigue medir. Esa
red solo funciona si la herramienta sabe negarse — y la Rosca no sabía; ver la
sección siguiente.

**Un hexágono no es una rueda de seis dientes**, aunque su radio se repita seis
veces por vuelta exactamente igual. La primera versión de esto proponía
engranaje a los hexágonos y, al darlos por periódicos, les apagaba sus seis
lados y sus seis ángulos; lo cazaron quince pruebas que ya existían. La
distinción no hubo que inventarla: `classifyShape` dice «polígono» o «polígono
redondeado» y con cuántos lados, y se rinde con «irregular» justo donde vive un
engranaje. Se propone Engranaje solo si la forma no es un polígono, o si los
dientes pasan del techo de lados del clasificador (doce), porque por encima de
ahí ya no hay polígono que valga.

**Se apaga la pieza ENTERA, no solo el tramo roscado — y se intentó lo otro.**
Lo intuitivo sería apagar únicamente lo que cae dentro del tramo de eje donde la
Rosca consiguió medir, y así conservar las caras y esquinas de la cabeza de un
tornillo, que son cotas de verdad. Se implementó y se midió: **ese tramo no
delimita la rosca.** El buscador se queda con la primera colocación que MIDA, y
medir no es acotar. Descomponiendo `tornillo-1.png`, la rosca va del 0 % al 89 %
del eje —tramos de 0,6 a 0,9 pasos, uno cada 3,5 % del eje, que es justo el
paso— y la cabeza ocupa del 89 % al 100 %, con tramos de 2,5 a 3,8 pasos. La
colocación ganadora fue del 30 % al 100 %: metía la cabeza entera y dejaba fuera
el primer 30 % de rosca. Volvían **nueve arcos** al tornillo, sentados al 4, 8,
11, 15, 18, 22 y 25 % del eje y separados exactamente un paso, llamados «Radio
14», «Radio 15», «Radio 16»… que es palabra por palabra el problema del que
venimos. El precio de apagarlo todo es real y está dicho en el README: un
tornillo de cabeza hexagonal se queda sin las cotas de su cabeza. Se paga porque
la alternativa medida es peor.

**Dos ruedas que se solapan: la respuesta correcta es negarse.**
`engranajes-1.jpg` son dos ruedas dentadas vistas de cara, de unos treinta
dientes y nueve agujeros de aligeramiento cada una, y se solapan. Con la
detección por defecto la segmentación las funde en **una sola pieza de 210×406**
—una rueda sola daría una caja cuadrada— y el Engranaje se niega. Encendiendo
«Separar las piezas que se tocan» salen dos piezas, pero el separador corta por
donde se solapan y **se lleva dientes por delante**: una queda en 203×184 y el
recuento deja de ser estable (30 con el radio interior al 70 % del exterior, 31
al 88 %). Los dientes son la identidad de la rueda, así que uno de más o de
menos ya es otra pieza: publicar un número ahí sería peor que no publicar
ninguno. Hay una prueba que lo fija.

De paso, un límite de la geometría con la que se PROPONE el engranaje: el radio
interior sale al 55 % del exterior, y en una rueda con **agujeros de
aligeramiento** ese anillo cae justo encima de ellos y los rayos encuentran el
borde equivocado. En esas ruedas hay que dibujar la herramienta a mano con los
radios ajustados, que es lo que el propio aviso pide. No se puso una barrida de
varios radios porque el único caso que la motivaba es esa foto de las dos ruedas
solapadas, donde el recuento no se puede verificar — y una barrida convertiría
un «no» correcto en un número que nadie ha comprobado.

**Lo que NO se hizo, y por qué:** hubo una versión que colapsaba las propuestas
de valor repetido —seis ángulos de 102° en una— y rompió el octógono y el
dodecágono. Las N caras iguales de un polígono regular **sí** son N cotas: cada
una puede salirse de tolerancia por su cuenta, y fundirlas dejaría N−1 sin
comprobar. La diferencia con los dientes no es que se repitan, es que seis caras
son *todas* las que hay y ocho flancos de cuarenta son una *muestra*. El colapso
se borró en vez de ajustarle el umbral.

#### La Rosca aprende a decir que no

Medido con el eje trazado a lo bruto sobre las dieciséis fotos del banco: la
herramienta de Rosca decía que **sí a las dieciséis**. Arandelas, tuercas y
cáncamos incluidos, con perlas como «paso=1,3 px» en una bolsa de arandelas —un
paso de 1,3 píxeles no es una rosca, es la rejilla de la cámara. Con las
tolerancias abiertas que pone el generador de propuestas, cada una de esas se
habría llevado un OK verde.

Su hermana la del Engranaje, mismo módulo de periodicidad y mismo fichero, decía
que **no en quince de dieciséis** y explicando por qué. La diferencia no era de
información: la Rosca ya calculaba los dos números que hacían falta y los dejaba
en un aviso al final del texto, donde un aviso que sale en todas las piezas se
aprende a ignorar. Ahora deciden:

1. **Tiene que haber filete.** El perfil *plegado* por su periodo es la parte que
   de verdad se repite; si no se puede formar —porque el periodo se quedó clavado
   en el suelo del rango de búsqueda, que es lo que pasaba en las arandelas— o si
   su altura no llega a un píxel, no hay filete. Medido: en la rosca de verdad el
   plegado sube 28,9 y 30,1 px a cada lado del eje; en las arandelas y tuercas,
   0,00 por los dos.
2. **Los dos lados del eje tienen que decir lo mismo.** Cuando no lo dicen, lo
   típico no es ruido: es *aliasing* —el eje coge una cresta sí y otra no— y el
   paso sale al doble. Medido en `tornillo-2.png`: los dos lados discrepaban un
   49 % y el paso salía 48,9 px cuando contando crestas se ven unas veintidós en
   ese tramo, o sea la mitad. El 15 % ya estaba en el código como umbral del
   aviso.

Con las dos puertas: **16 de 16 rechazadas** con el eje ingenuo, y las dos
medidas verificadas siguen pasando.

**Cómo se comprobó que no se ha vuelto un «no» a todo.** `rosca-1.png` lleva
impreso «1 pulgada» con su flecha y numera 6 hilos dentro; midiendo la flecha por
sus píxeles cian salen 399 px, o sea **66,5 px de paso**. La herramienta mide
**65,9 px**: 0,9 % de error. Y en `tornillo-1.png`, contando las vueltas sobre la
foto ampliada con rejilla salen ≈34 px y mide **34,0 px**.

**«flanco=0,00°» no es un ángulo.** `flankAngleDeg` devuelve cero cuando se
rinde, y ese cero se escribía con dos decimales en **catorce de las dieciséis**
fotos. Un flanco de 0° sería una rosca de paredes verticales: no existe. Ahora
dice que no se puede medir.

**Y un segundo recuento, que avisa en vez de rechazar.** Contar crestas es otro
camino al mismo número, y cuando los dos discrepan hay algo que mirar. Aquí no
manda, y eso está medido: en `tornillo-1.png` el paso sale correcto mientras el
recuento de crestas da 2 de un lado y 12 del otro, porque en ese lado el borde de
la caña cae contra una sombra. Negarse ahí habría tirado una medida buena.

#### Y antes de todo eso, QUÉ FIGURA ES

Lo anterior mira el tamaño de los rasgos y nunca la **forma**, y eso se notaba
en cuanto la pieza no era un rectángulo con agujeros. Medido sobre figuras
sintéticas, esto es lo que proponía:

| Pieza | Proponía | Faltaba |
|---|---|---|
| Disco Ø300 | «Largo total» 301 y un arco de radio 151 | el **diámetro**, el perímetro, la **redondez** |
| Hexágono | largo, ancho y **5** ángulos | los **lados**, y un ángulo de seis |
| Triángulo | largo, ancho y **2** ángulos | los **lados**, y un ángulo de tres |

Las dos cosas mal son distintas. Sobre un disco, el largo y el ancho de la
envolvente **son el mismo número que el diámetro**, y el radio del arco es su
mitad: tres nombres para una cota, en una lista cuyo valor entero depende de que
se pueda revisar. Y a un polígono no se le medía ni una cara.

Así que `vision::classifyShape` (en `vision/shape_class.*`) pregunta primero qué
figura es: **círculo, arandela, polígono de n lados, polígono redondeado o
irregular**. Vive en `vision` y no en el editor porque es una propiedad de la
pieza, no de una pantalla.

Cómo decide, que es lo interesante: **compiten tres modelos.** Primero se
pregunta a la descomposición si hay rectas unidas por arcos; si no, compiten un
ajuste de polígono (`approxPolyDP`) y uno de circunferencia (Taubin) **con la
misma vara** —cuánto se separa el punto peor— y gana el que menos se desvía. Sin
esa competición habría que ordenarlos a mano —«primero mira si es círculo»— y
ese orden decidiría los empates en silencio; un dodecágono está a un pelo de ser
un círculo y quien tiene que resolverlo es la medida, no el orden de dos `if`.

Que la descomposición vaya **primero** tampoco es un detalle: un polígono de
muchos vértices también aproxima un rectángulo redondeado —basta con poner
vértices a lo largo de cada esquina— y entonces saldría «polígono de 12 lados»,
que es la discretización de la curva y no la descripción de la pieza. El filtro
es cuánto perímetro va en curva: medido, un rectángulo de 300×200 con redondeos
de 40 px lleva el **27 %** del contorno en arco y un hexágono o una L llevan
**0 %**.

Cuatro cosas que costaron una medida cada una, y las cuatro tenían la misma
forma: **un número absoluto en un mundo que escala**.

- **El barrido de epsilon tiene que empezar MUY fino.** Empezando en el 0,5 % del
  perímetro, un polígono de 14 lados se quedaba sin ajuste: ese epsilon ya vale
  más que la flecha de sus lados, así que `approxPolyDP` se comía vértices y nada
  pasaba la tolerancia. Salía «irregular», la peor respuesta posible —ni lados ni
  diámetro— y la causa no se ve mirando el resultado, solo barriendo.
- **Del barrido se saca la MESETA, no el primer acierto.** Con «el primero que
  cumple», un hexágono salía de 6 lados a 0°, de **7** a 10° y de **8** a 15°:
  al rasterizar un borde inclinado aparecen escalones y con el epsilon más fino
  se cuelan vértices de más. Medido, el ajuste de 6 vértices aparece en 29 de los
  30 epsilon del barrido y el de 7 u 8 en uno solo. La meseta dice cuál es la
  respuesta; el primer acierto dice cuál fue la casualidad. Y una clase que
  cambia al girar la pieza no sirve de nada, porque la pieza llega a la mesa como
  llega.
  **Y el orden dentro de esa regla también importa**: mirando primero la anchura
  de la meseta y después su desviación, un polígono de 12 lados salía «círculo»
  —su meseta más ancha era la de 6 vértices malísimos, se descartaba, y con ella
  se iba el ajuste de 12 que sí valía—. Una meseta que no explica el contorno no
  es una candidata peor: no es una candidata.
- **El paso de remuestreo de la descomposición se fija en MUESTRAS, no en
  píxeles.** Con los 2 px fijos que trae por defecto, el mismo hexágono daba sus
  6 lados con perímetro 1013 px y «4 rectas y 2 arcos» con perímetro 257: solo
  128 muestras para seis esquinas. Ahora `decomposeOptionsFor` mantiene ~500
  muestras sea cual sea el tamaño, con tope en los 2 px de antes para no tocar
  lo que ya funcionaba en piezas grandes. **La usan el clasificador y el
  generador de propuestas, y tienen que usar la misma**: si vieran contornos
  distintos, uno diría «hexágono» y el otro propondría cuatro lados.
- **La tolerancia tiene un suelo y una pendiente.** El dentado del rasterizado
  mide un píxel y no encoge cuando la pieza crece, así que hace falta un suelo
  absoluto (6 px, medido: un hexágono girado se separa hasta 3,9 px de sus
  propios lados, y un redondeo de 40 px se separa 16,6 px — el 6 cae en medio de
  ese hueco). Pero el error de **colocar** un vértice sí crece con la pieza: si
  cae dos píxeles antes de la esquina, el lado entero se inclina. Con 6 px a
  secas, un decágono de radio 400 salía «círculo»; con el 2,5 % del radio
  añadido, sale de 10 lados.

Y la tierra de nadie entre polígono y círculo, que se resuelve con la misma
lógica relativa: un contorno de 14 o 16 lados tiene más caras de las que merece
la pena medir una a una y todavía no cae dentro del ruido de una circunferencia.
Si se separa menos del **5 % del radio**, se mide como redondo —con la redondez
diciendo la verdad sobre las caras planas— y una estrella de veinte puntas, que
se separa un 40 %, se queda fuera.

Con la figura en la mano, lo que se propone cambia:

| Figura | Se propone | Se deja de proponer |
|---|---|---|
| Redonda | **Ø** y **Redondez** | largo/ancho y el arco: son la misma cota |
| Arandela | **Ø exterior**, **Ø interior** y Redondez | — |
| Polígono | **Lados (n)**, una regla **Lado i** por cara, los ángulos | — |
| Redondeado | una regla **Lado i** por tramo recto, los radios | **Lados (n)** |
| Irregular | lo de siempre | — |

**`Lados (n)` y las reglas `Lado i` no son lo mismo, y por eso van las dos.** La
herramienta de Lados mide el **recuento** de caras: vigila que no aparezca ni
falte una, que es una avería distinta de que un lado se salga de cota. Las
longitudes necesitan una regla por cara, cada una con su tolerancia y su
veredicto.

En un polígono **redondeado** no se propone el recuento, y no es un olvido: la
herramienta exige que el número de lados no cambie al mitad y al doble de
epsilon, y al afinar epsilon las esquinas redondeadas aparecen como vértices
nuevos. La herramienta tiene razón y la propuesta nacería muerta.

**Dos fallos más que salieron de medir esto**, ninguno visible mirando una pieza
cualquiera:

- **El recuento de esquinas siempre se quedaba uno corto.** El contorno es
  cerrado —la última cara hace esquina con la primera— y el bucle se paraba en
  `size()-1`. A un hexágono le proponía cinco ángulos y a un triángulo dos. Un
  contador que siempre falla por uno es de los peores: cuadra casi siempre y
  falla justo cuando cuentas.
- **Un «Espesor» que anunciaba 260 px y medía 81.** El calíper recorre su trazo y
  se queda con el primer par de bordes de polaridad opuesta, que no tiene por qué
  ser el par de caras que motivó la propuesta: en una pieza en L se topaba por el
  camino con una pared más cercana. Son dos fallos en uno —un motivo que miente y
  una cota repetida con otro nombre— y ahora la propuesta **se descarta** en vez
  de corregirle el texto: prometía medir esas dos caras y no las mide.

#### Hasta dónde aguanta, medido

El banco ensucia la escena a propósito (`tests/test_shape_proposals.cpp`) porque
una cámara no da máscaras impecables. Sobre siete figuras —disco, arandela,
hexágono, triángulo, cuadrado, rectángulo redondeado y una L— resegmentando con
Otsu en cada caso:

| Degradación | Aguanta hasta | Nota |
|---|---|---|
| Ruido gaussiano | **σ = 20** | sobre pieza 220 / fondo 30 |
| Desenfoque | **kernel 9** | |
| Contraste bajo | **120 / 90** | 30 niveles, con σ=5 encima |
| Gradiente de luz | **±60** | y viñeta del 60 % |
| Contraste bajo **+** gradiente | **falla** | y falla **Otsu**, no el clasificador |

El último caso queda escrito en el test tal cual, porque saber dónde falla vale
más que fingir que no falla: con 30 niveles de contraste y un gradiente de 15,
Otsu corta por donde no debe y la máscara se traga el encuadre entero. La clase
que sale describe fielmente esa mancha de 500 px. En la aplicación no llega a
pasar, porque el `maxAreaFraction` del pipeline rechaza antes una «pieza» que
ocupa casi todo el frame — y el test afirma justamente que el fallo está ahí, de
modo que si algún día se muda al clasificador, lo dirá.

Y los límites que **no son fallos sino física**, que por eso se afirman en vez de
perseguirse:

- **Cuantos más lados, más grande hay que ver la pieza.** Con 3 o 6 lados basta
  un radio de 35 px; con 10 o 12 hace falta llegar a 100. Por debajo salen
  «redondas», que a 24 px de ancho es la respuesta honesta *y* la útil: se les
  mide el diámetro. Un pentágono de radio 25 —50 px de ancho— se lee como
  cuadrado, porque a ese tamaño una esquina de 108° cabe en tres píxeles.
- **El ruido del borde tiene distinto precio según la forma.** Un disco aguanta
  dientes de sierra de 7,2 px de desviación radial sin dejar de ser un disco: el
  ruido no le cambia la forma. Un polígono aguanta 3,5 px, que es cuando el
  diente empieza a parecerse a una esquina.
- Un giro de 25° sobre un **dodecágono de radio 160** ya lo pasa a círculo, y
  está bien: a ese tamaño se separa 5,5 px de su propia circunferencia. Forzar
  una respuesta ahí sería fijar por contrato el ruido del rasterizado.

Tres reglas que hacen que la lista sea revisable:

- **Cada propuesta se ejecuta antes de proponerse.** Si la herramienta no
  consigue medir sobre esta pieza, se descarta en vez de ofrecérsela al
  operador. Lo que llega a la lista ya funciona, y su tolerancia sugerida sale
  de una medida real y no de una estimación geométrica.
- **Sin duplicados.** En un rectángulo salían "Ancho total = 279" y "Espesor 1 =
  280": la misma cota dos veces con dos nombres. Una propuesta de longitud se
  descarta si otra ya aceptada mide lo mismo en el mismo sitio.
- **Con el porqué y con tope.** Cada una lleva una frase explicando por qué se
  propone; sin eso, doce propuestas no se revisan, se aceptan todas o se
  descartan todas. Y se cortan en doce: cincuenta son tan inútiles como ninguna.

#### El recorte se llevaba una categoría entera

El orden con el que se cortaba era «primero las longitudes de mayor a menor y
los ángulos al final». El razonamiento de mandar los ángulos al final era bueno
—su medida está en grados y no se compara con una longitud— y la consecuencia,
desastrosa: un hexágono genera unas dieciocho propuestas y con el tope en doce
**perdía sus seis ángulos, todos**.

Ordenar por categoría y cortar por el final no recorta lo pequeño: **borra una
categoría entera**. Ahora se ordena dentro de cada clase de medida
(`MeasuredKind`) y se van tomando por turnos, así que el recorte se lleva lo más
pequeño de cada clase y ninguna desaparece. Medido sobre el hexágono con las
opciones reales: 6 longitudes, 5 ángulos y el recuento de lados, con 3
descartadas.

Y **lo que queda fuera se dice**. Descartar cotas en silencio deja al operador
creyendo que la pieza no tenía más, que es exactamente lo contrario de lo que
pasó.

El fallo era invisible para el banco de pruebas y merece la pena saber por qué:
todos los tests de propuestas usan `everything()` —tope de cien— justamente para
no medir el tope. El tope es lo que corre en producción. Ahora hay tres tests
que usan las opciones **reales**.

**El botón abre una revisión, no inserta a lo loco** (decisión confirmada con el
usuario). Insertar directamente es más rápido de programar y peor de usar: deja
al operador borrando lo que no pidió. El diálogo muestra por fila **qué mide,
cuánto da sobre esta pieza, su tolerancia sugerida y por qué se propone** —
quitar cualquiera de esos cuatro datos convierte la revisión en "aceptar todo".
Vienen todas marcadas, porque lo normal es querer casi todas y así revisar es
desmarcar; el botón dice **cuántas va a insertar**; y todas entran en **un solo
paso deshacible**, que quitar siete herramientas con siete Ctrl+Z sería peor que
haberlas dibujado a mano.

Verificado sobre piezas de medidas conocidas: agujeros de Ø70 y Ø100 se proponen
como Ø70,1 y Ø100,0; las cuatro esquinas de radio 45 salen como cuatro Arcos; un
pinchazo de 6 px no genera propuesta.

### «¿Cuánto mide esto?», contestado entero

Es la pregunta que más se hace delante de una pieza, y la aplicación obligaba a
un rodeo para responderla: abrir el editor de plantilla, pulsar «Medir
automáticamente», revisar propuestas y aceptarlas como herramientas. Eso está
bien para PREPARAR la vigilancia de una pieza en producción y es absurdo para
mirar una pieza y querer sus cotas.

Todo lo necesario existía suelto —`describeContour` saca perímetro, área y
agujeros; `classifyShape` dice qué figura es; `proposeTools` deduce qué cotas
tienen sentido para esa figura— y lo que faltaba era **quien lo juntara**. Eso es
`inspection::measureWholePiece`, y el botón **Medir pieza** de la barra.

El informe va en dos bloques, y la separación no es decorativa:

- **El contorno**: perímetro, área, largo y ancho de la envolvente mínima
  girada, agujeros, circularidad, tramos rectos y arcos. Van siempre, sea cual
  sea la figura, porque no dependen de haberla reconocido: son lo que el
  contorno **es**. Y son justo los que antes no se podían leer en ningún sitio
  salvo un rótulo en una esquina del editor.
- **Las cotas**, deducidas de la forma: Ø y redondez si es redonda, los dos
  diámetros si es una arandela, cada lado y cada ángulo si es un polígono.

Mezclarlos invita a buscarle tolerancia a un área que nadie ha declarado.

Tres decisiones:

- **No se corta.** El diálogo de propuestas se corta en doce porque es una lista
  que hay que revisar a mano; esto es un informe, y un informe cortado contesta
  a medias. Medido sobre un dodecágono: 8 hechos de contorno y 25 cotas.
- **Nada sale marcado «OK».** Una cota recién medida está dentro de su propia
  tolerancia por construcción —la banda se sugirió a partir de ella—, así que
  marcarla sería dar por comprobado lo que nadie ha comprobado todavía.
- **Medir y vigilar son dos decisiones.** El informe no toca la plantilla; hay
  un botón para convertir las cotas en herramientas vigiladas. Unirlas llenaría
  la plantilla de herramientas a cada consulta.

#### La pestaña de herramientas, en dos niveles

Petición de uso: *«que hubiera como dos partes en lo de herramientas, una de la
herramienta en general y otra de todas las secciones-medidas de esa
herramienta»*.

El hueco era real y llevaba ahí desde que existen las cinco clases con selector
de medida. **La Región elige entre seis** —área, perímetro, solidez,
circularidad, relación de aspecto, número de agujeros—, la Ranura y el Chaflán
entre tres, el Acuerdo y los Extremos entre dos. Al dibujar la herramienta se
escoge UNA y las demás quedan invisibles, **aunque salen de la misma figura, el
mismo trazo y el mismo fixture**: para ver el perímetro de la región que ya
tenías había que dibujar una segunda región encima de la primera.

Ahora cada herramienta se despliega en todo lo que su figura puede medir:

- **Arriba, la herramienta**: su nombre, lo que mide hoy, su veredicto y el
  interruptor de «cuenta / no cuenta».
- **Debajo, sus medidas hermanas**, cada una con su valor ya calculado. La que
  la herramienta mide hoy sale en negrita y **sin interruptor propio**: el suyo
  es el de arriba, y dos casillas para el mismo hecho obligarían a que una de
  las dos mintiera. Las demás llevan una casilla que las convierte en cota nueva
  sobre la misma figura, sin volver a dibujarla.

**Se ejecutan de verdad, no se enumeran.** Una lista de nombres sin sus valores
no da con qué decidir cuál merece vigilarse, que es justo para lo que está.
Cuesta 0,178 ms → 0,514 ms por región, **×2,88 y no ×6**: las seis comparten el
recorrido de la imagen y el contorno, y `runTools` las despacha en un lote.

**Una medida hermana nace sin tolerancia** (`[0, 1e9]`, la banda por defecto):
mide y no juzga hasta que alguien le declare la suya. Heredar la del padre sería
peor que no poner ninguna — un perímetro dentro de la banda de un área es una
conformidad inventada, y la fila lo dice en vez de callarlo.

Las herramientas de una sola medida —un calibre mide una distancia y nada más—
**no se despliegan**: un desplegable con un hijo que repite al padre hace dudar
de si falta algo.

#### El botón que duplicaba las cotas

Queja de uso: *«se duplicaron las herramientas»*. Era exacta. **«Vigilar estas
cotas» se llevaba TODAS las propuestas sin mirar si ya estaban**, y los nombres
que genera el proponedor son deterministas —«Ø», «Largo total», «Lado 1»,
«Espesor 2»—: pulsarlo dos veces sobre la misma pieza dejaba una segunda copia
de cada cota, con otro id y el mismo nombre.

Ahora se salta las que ya tienes dibujadas y lo dice: *«Se añaden 3; otras 5 ya
las tenías»*. Y si no quedara ninguna por añadir **no cierra**: cerrar sin haber
añadido nada y sin decir por qué se lee como que sí se añadió.

Y la unidad de longitud **se resuelve una vez para el informe entero**. En
automático cada medida elige mm o cm según su tamaño, y para una etiqueta suelta
sobre la pieza está bien porque se lee sola; en una tabla no, porque una tabla
existe para comparar filas y un perímetro en cm junto a un lado en mm obliga a
convertir de cabeza en cada renglón.

#### Los agujeros que la cadena real se comía

Sondeando una arandela de verdad salió clasificada como **«círculo»**, sin
diámetro interior y con cero agujeros. El motivo estaba tres capas más abajo:
`analyzeFrame` devuelve la máscara con el contorno exterior **relleno** —a
propósito, para que los blobs de ruido que sobreviven a la morfología no sesguen
el fixture— y los **tres** sitios que miden le pasaban esa máscara: la ventana
principal, el editor y `pci_probe`.

Con eso, el Ø interior de una arandela, el recuento de agujeros y la medida de
cada uno **no salían nunca en la aplicación real**. El banco de pruebas no podía
verlo: allí las máscaras se dibujan a mano y conservan su agujero. El comentario
del sondeo hasta lo decía —«la figura se pregunta con la MÁSCARA, sin ella una
arandela sale como disco»— y le pasaba la máscara equivocada.

`vision::pieceMaskWithHoles` vuelve a segmentar y cruza con la máscara rellena:
eso devuelve los agujeros y a la vez descarta cualquier mancha fuera de la pieza
elegida. Va aparte de `analyzeFrame` porque lo paga quien MIDE —un gesto
puntual— y no cada frame del vídeo. Si la segunda segmentación no ve lo mismo
que la primera, manda la máscara original: perder los agujeros es un
inconveniente, perder la pieza es no medir nada.

Verificado sobre una arandela dibujada con Ø380 y Ø160 px: medidos **379,9 y
159,8** (0,02 % y 0,13 %).

#### Las caras de una pieza de contorno libre

Los lados solo se proponían para el **polígono** y el **polígono redondeado**.
Una pieza de canto escalonado —un peine, una cremallera, un soporte con
rebajes— cae en «contorno libre» y se quedaba **sin una sola cota de sus
caras**, aunque la descomposición del contorno ya las tenía medidas y aunque sí
recibía sus ángulos y su envolvente.

Conviene decir de qué tamaño era el hueco, porque adivinarlo salió mal dos
veces: el clasificador es **más tolerante** de lo que parece, y llama
«redondeada» a bastante más de lo que su nombre sugiere.

Ahora se proponen para cualquier forma que no sea redonda. La pieza redonda
sigue fuera, y ahí la razón se mantiene: en un disco el «tramo recto» que
aparece es un trozo de circunferencia mal ajustado, no una cara.

Encontrar una pieza de prueba costó tres intentos, y el proceso vale más que el
resultado. La primera fue una escuadra con una entalla y un extremo redondeado:
el clasificador la llamó **«redondeada»**, así que ya recibía lados y el test no
probaba nada. La segunda, con un rebaje semicircular en medio de una cara,
también salió redondeada. Se sondearon nueve formas para ver **cuáles caen de
verdad** en contorno libre:

| Forma | Clase | Lados propuestos |
|---|---|---|
| Disco, arandela | redonda | 0 — correcto |
| Hexágono, L, estrella | polígono | 6, 6, 10 |
| Rectángulo redondeado | redondeado | 4 |
| **Elipse** | **contorno libre** | **0** — correcto: no tiene caras |
| **Canto escalonado** | **contorno libre** | **11** — antes 0 |
| **Gota** (círculo con punta) | **contorno libre** | **2** — antes 0 |

La elipse es la que impide que quitar la condición se convierta en inventar
cotas: también es de contorno libre y no tiene ni una cara recta, así que tiene
que seguir sin lados. Hay un test para cada lado de esa frontera.

### Catorce de quince cotas no podían fallar

Lo destapó una sonda que no buscaba esto. Se propusieron las cotas de un
hexágono de radio 160 y se ejecutó **cada herramienta propuesta sobre otra
figura**. De quince, **catorce devolvían exactamente el mismo número**:

| Cota | Sobre el hexágono r=160 | Sobre la otra figura |
|---|---|---|
| `Lado 1` (Regla) | 159,13 px | 159,13 px — el lado del hexágono r=200 mide 200 |
| `Ángulo 3` (Ángulo) | 120,26° | 120,26° — **sobre un cuadrado**, que tiene 90° |
| `Largo total` (Regla) | 322,00 px | 322,00 px |
| `Lados (6)` (Polígono) | 6 | 8 — la única que medía |

El contraste se hizo contra un **cuadrado** y no contra otro hexágono a
propósito: un hexágono regular tiene 120° a cualquier tamaño, así que un ángulo
que no se moviera podría estar midiendo bien y coincidir.

No es un error de precisión. La Regla, el Ángulo y la Línea-Línea guardan sus
puntos en coordenadas de **pieza** y calculan sobre ellos, y ni la distancia
entre dos puntos ni el ángulo entre dos rectas cambian al aplicar el fixture,
que es un giro más una traslación. El número es el mismo siempre, en cualquier
pieza. Esas herramientas salían con un **OK verde en cada inspección, para
siempre, sin haber comprobado nada** — la peor forma de fallar que tiene un
programa de inspección, porque el fallo se lee como conformidad.

La Regla lo dice de sí misma en su descripción —«mide exactamente lo que
trazas»— y como herramienta suelta está bien: sirve para medir al vuelo. Lo que
no puede es fingir un veredicto.

`inspection::remeasuresThePiece(ToolType)` lo escribe una sola vez, y
`baseResult` —el único punto donde nace todo resultado— marca **informativa** a
la que no vuelve a medir: da su número y escribe «—» donde iría el OK. El
veredicto global no cambia, porque estas herramientas siempre pasaban; lo que
desaparece es la afirmación de que se comprobó algo.

Por defecto se dice que una herramienta **sí** vuelve a medir, y también es a
propósito: afirmar que no mide cuando sí lo hace le quitaría un veredicto de
verdad. Solo se listan las tres comprobadas. La Posición y el Desplazamiento de
centro no miran la imagen y aun así miden —*dónde* ha caído la pieza respecto al
tablero, que cambia de una a otra—, así que siguen juzgando.

Las propuestas automáticas lo dicen en su porqué: *«Se mide ahora sobre esta
pieza; guardada, repite este valor: vale como cota de referencia, no como
comprobación.»* El número que enseñan **es** una medida de verdad —sale de la
descomposición del contorno de esa pieza— y por eso el informe de *Medir pieza*,
que se recalcula entero cada vez que se pide, sigue siendo exacto.

### Sacar las medidas

Se podían exportar los **puntos** del contorno a CSV y el historial de
veredictos, pero no las cotas: los números que el operador acaba de medir vivían
en una tabla que solo se podía mirar. Una medición que no se puede sacar no entra
en un informe de calidad, no se compara con la del turno anterior y no se manda a
nadie — que son las tres cosas para las que se mide.

`inspection::measurementRows` resuelve las filas una sola vez y de ahí salen las
dos formas: **CSV** para la hoja de cálculo y **texto alineado** al portapapeles
para un correo o un parte. Son dos porque sirven para cosas distintas; dar solo
una obligaría a la mitad de la gente a reformatear a mano.

Cuatro decisiones del formato, y las cuatro se pagan si se hacen al revés:

- **El valor es un número y la unidad va en su propia columna.** Escribir
  «50,00 mm (200,0 px)» en una celda convierte la columna en texto, y una
  exportación cuyas columnas no se pueden sumar ni promediar no es una
  exportación: es una captura de pantalla en letras.
- **Cada fila lleva su unidad**, porque las filas no son de la misma clase — en
  la misma tabla conviven longitudes, ángulos, recuentos y fracciones.
- **Los píxeles no se pierden**, van en su propia columna. La escala puede
  resultar estar mal más tarde, y con ellos se rehace la conversión sin volver a
  medir la pieza.
- **Los textos se entrecomillan.** Los detalles llevan comas a menudo, y una
  sola coma sin escapar desplaza todas las columnas siguientes de esa fila — un
  fallo que no se ve hasta que alguien abre la hoja y encuentra la tolerancia en
  la columna del estado.

Separador decimal con punto a la fuerza (locale clásico), igual que la
exportación del contorno: en un Windows en español el separador por defecto es
la coma, y un CSV con «12,50» en una columna separada por comas no lo abre nadie.
Sin tolerancias conocidas las columnas van **vacías** y no a cero, porque un cero
parece una tolerancia de cero, que es la más estricta que existe.

### Ver y exportar el contorno

La otra mitad de "medir la pieza y los contornos". `vision::describeContour(mask)`
devuelve **un solo informe** —contorno exterior, agujeros, descomposición,
perímetro, área y envolvente— y no cuatro funciones sueltas, porque las partes
tienen que ser coherentes entre sí: el área descuenta **estos** agujeros, que son
los hijos jerárquicos de **este** contorno. Calculadas por separado, una pieza
pequeña con su propio agujero al lado de la principal le restaría área a la
grande; hay un test que lo comprueba.

La superposición (`EditorCanvas::paintContourReport`) pinta cuatro cosas y cada
una tiene su motivo:

- El **contorno crudo en blanco tenue, por debajo**. Es la referencia contra la
  que se lee todo lo demás: sin él, un arco mal ajustado se ve como un arco
  perfecto y nadie nota que no sigue al borde.
- Los **tramos rectos en azul y los arcos en naranja**, dibujados **a partir de
  la primitiva ajustada**, no de los puntos del contorno — que es justo lo que
  hace visible dónde se despega.
- Un **punto blanco en cada corte** entre tramos, para que la descomposición se
  vea aunque dos tramos vecinos sean del mismo tipo.
- El **radio junto a cada arco**, que es el dato que se viene a buscar en un
  redondeo. Solo en los tramos largos: etiquetar los cortos tapa la pieza de
  números.

Va **por debajo de las herramientas** y no se puede seleccionar ni arrastrar: es
una capa de consulta y no debe competir con lo que sí se mide. Y se invalida al
recapturar desde la cámara, porque describe la foto anterior.

El **resumen** (perímetro, área, agujeros, envolvente, cuántos tramos) lo genera
el propio lienzo, en la unidad activa, y lo reutiliza la ventana para el panel de
estado: un solo sitio donde se decide el formato, o los dos acabarían diciendo
cosas distintas. El área se convierte con el **cuadrado** de la escala; hacerlo
linealmente daría un número plausible y cuatro veces mayor, y hay un test que
falla si alguien lo cambia.

#### El perímetro no se mide con `cv::arcLength`

Preparando `F1` se midió el sesgo del perímetro y salió algo peor de lo esperado.
`arcLength` mide el polígono que pasa por los **centros de los píxeles** del
borde, y eso lo hace **depender de cómo esté girada la pieza**. El mismo cuadrado
de 200 px, barrido por 0°, 15°, 30°, 45° y 60°:

| ángulo | `arcLength` | `digitalPerimeter` |
| --- | --- | --- |
| 0° | +0,00 % | −2,05 % |
| 15° | +7,06 % | +0,75 % |
| 30° | +7,71 % | −0,22 % |
| 45° | −0,30 % | −0,92 % |
| 60° | +7,71 % | −0,22 % |
| **dispersión** | **8,01 puntos** | **2,79 puntos** |

Ese **+7,7 %** es el problema, no el sesgo del círculo del que avisaba el plan:
la misma pieza leída un 7,7 % más larga solo por estar posada de otra manera. Una
medida de inspección no puede depender de eso.

Se usa **Vossepoel–Smeulders** (`vision::digitalPerimeter`), que pesa por
separado pasos rectos, pasos diagonales y esquinas: error por debajo del 2,3 % en
todas las figuras probadas y **2,9 veces menos dispersión** por orientación. Se
descartó la corrección de Kulpa (×0,948) porque arregla el círculo y estropea el
cuadrado alineado en un −5,2 %.

El precio, dicho claro: en un borde recto **alineado con los ejes** `arcLength`
era exacto y el estimador se queda un 2 % corto. Por eso el test de la placa
cuadrada tuvo que pasar de 1,5 % a 3 % — y esa placa es justo el caso donde el
método viejo era perfecto. Se acepta porque una pieza real no llega siempre
alineada, y un error acotado en todas las orientaciones vale más que uno perfecto
en una sola.

Nadie tenía tolerancias puestas sobre este número: solo se muestra en el panel de
contorno y se exporta al CSV. Si algún día alimenta un veredicto, el cambio de
estimador sí sería un cambio de criterio.

La **exportación a CSV** (`vision::contourToCsv`) existe para llevarse la pieza a
un CAD. Dos decisiones que parecen menores y no lo son:

- La **unidad va en el nombre de la columna** (`x_mm` / `x_px`), no en una línea
  de comentario: los importadores de CAD y las hojas de cálculo tragan cabeceras
  y no comentarios, y un archivo de coordenadas sin unidad no sirve para nada.
- El formato se escribe con el **locale clásico a la fuerza**. En un Windows en
  español el separador decimal es la coma, y un CSV con `12,50` en un archivo
  separado por comas no lo abre nadie. El test vuelve a leer el archivo partiendo
  por comas —igual que hará el CAD— y falla si aparece un campo de más.

Verificado con números: el CSV de una placa de 340×340 px releído encierra
114 921 px² (339², el contorno de una máscara pasa por el centro de los píxeles
del borde) y con calibración de 0,25 mm/px da 7 182 mm² frente a los 7 225
teóricos.

### Avisos de condiciones: el fallo que no se ve en el número

Estas herramientas dan resultados **creíbles** con datos malos, que es la peor
forma de fallar: un diámetro con la cámara inclinada sale corto y nada en el
número lo delata. Por eso, además de negarse cuando no puede medir, cada
herramienta de esta familia dice **en qué condiciones midió**:

| Aviso | Cuándo | Por qué importa |
|---|---|---|
| Cámara inclinada | `MarkerScale::quality` < 0,75 | Un círculo se ve como elipse y los diámetros salen cortos |
| Poco contraste | Gradiente medio del borde < 25 | El "borde" detectado no es el de la pieza |
| Arco corto | Tramo < 30° | Radio y centro son casi indistinguibles |
| Alcance corto | Un lado del eje sin bordes | La banda no llega a la pieza |
| Filete pequeño | Altura < 20 px | El ángulo de flanco deja de resolverse |
| Recuento discrepante | Periodicidad ≠ conteo de picos | Un diente de más o de menos cambia la rueda |
| Sin calibración | Se pide módulo o designación | En píxeles esos números no existen |

La regla que gobierna todo esto: **un aviso que salta siempre es un aviso que el
operador aprende a ignorar**, y entonces no avisa de nada. Se ha pagado dos veces
por no respetarla — el módulo cruzado del engranaje con el divisor a la mitad, y
la calidad de escala, que vale 0 cuando **no hay marcador** y habría hecho saltar
"cámara inclinada" en cada medición sin ArUco. Por eso hay un valor explícito de
**"no se sabe"** que no dispara nada, y por eso la mitad de las pruebas de avisos
comprueban que **NO** salten cuando no toca.

### El trazado: por qué las tolerancias van en píxeles de pantalla

La aritmética del lienzo vive en `canvas/canvas_geometry.*`, **fuera del
widget** y sin Qt, para poder probarla sin abrir una ventana: `ViewTransform`
(ajuste, zoom, límite del desplazamiento y las dos conversiones
pantalla↔imagen), las manijas de cada tipo de herramienta y la distancia de un
clic a una geometría.

La regla que gobierna todo lo que el operador *toca* es esta: **la zona de clic
se mide en píxeles de PANTALLA y se traduce a píxeles de imagen dividiendo por
la escala de la vista** (`pickTolerance`). Suena obvio y no lo era: las
tolerancias estaban fijadas en píxeles de imagen mientras las manijas se
dibujan siempre del mismo tamaño en pantalla, así que solo coincidían al 100 %
de zoom. Medido en el propio widget con una imagen de 1920×1080 en una ventana
de 900×640, al zoom máximo la escala es **9,375 px de pantalla por px de
imagen**, de modo que:

- una manija de 7 px dibujados se agarraba desde **84 px de distancia** — un
  clic en un sitio visiblemente vacío deformaba la herramienta;
- el punto de cierre del blob poligonal atrapaba desde ~112 px, así que poner
  un vértice cerca del inicio cerraba el polígono sin querer;
- un trazo intencionado de 30 px no llegaba al mínimo de 8 px de imagen (75 de
  pantalla) y la herramienta **no se creaba, sin aviso**.

Ese último caso destapó que ahí había **dos reglas confundidas en un solo
número**, y ahora están separadas porque responden a preguntas distintas:

| Mínimo | En qué se mide | Qué decide | Si no se cumple |
|---|---|---|---|
| 8 px de **pantalla** | movimiento de la mano | ¿fue un clic o un arrastre? | se ignora, sin ruido |
| 8 px de **imagen** | tamaño de la herramienta | ¿hay muestras para medir? | se avisa en la barra de estado |

Una herramienta de 3 px de imagen no tiene perfil que promediar ni rayos que
lanzar: no se puede crear. Pero el gesto fue deliberado, así que la señal
`traceRejected` lo dice con el número concreto en vez de tragárselo.

Con una imagen grande en una ventana pequeña pasaba lo contrario: la manija que
se ve no se podía agarrar. Los cuatro umbrales (selección, manija, cierre del
polígono y mínimo de arrastre) están ahora en píxeles de pantalla, y hay
pruebas que fijan que la zona de agarre sea la misma con cualquier zoom.

El **imán al borde** sigue la misma lógica, pero solo hacia abajo: su alcance se
acota por lo que se ve **sin superar nunca los 14 px de imagen originales** y con
un suelo de 6, para que la ventana de escaneo siga dando de sí. Al zoom de
ajuste no cambia nada; al máximo deja de arrastrar el extremo hasta un borde que
está a 112 px de pantalla de donde se soltó. Comprobado sobre el widget con un
borde sintético: soltando en x=991 con el ajuste, aterriza en 999,5 (subpíxel);
soltando en x=988 al máximo, se queda en 988.

**Las etiquetas de medida** también se colocan con geometría probable
(`placeLabel`): se busca hueco alejándose del ancla, primero hacia abajo y luego
hacia arriba, y **siempre dentro del área visible**. Antes solo se empujaba
hacia abajo sin mirar el borde, así que una medida anclada en la parte baja del
lienzo se empujaba fuera de la vista y el operador **no veía ninguna lectura** —
peor que un solape, porque no hay nada que sugiera que falta algo.

El alcance de la búsqueda está acotado a propósito (12 saltos por lado): una
etiqueta que se va al otro extremo deja de pertenecer visualmente a su
herramienta. Con una plantilla realista (doce medidas casi en el mismo punto) no
hay ni un solape; con cuarenta amontonadas, la banda alcanzable da para ~24 y
las demás se solapan a sabiendas, pero **ninguna desaparece**. Se probó añadir un
barrido fino para aprovechar huecos entre rejillas desalineadas y no mejoró el
reparto: el límite es el alcance, no el tamaño del salto, así que se quitó.

Dos contratos más que se cerraron aquí:

- **`setHandlePoint` con un índice fuera de rango no toca nada.** Antes caía en
  la rama por defecto de cada tipo y movía la última manija (cambiaba el radio,
  redimensionaba el blob).
- **Un arrastre de manija pertenece a la herramienta en la que empezó.** Si la
  selección cambia desde fuera a media faena —la lista del panel, un deshacer—
  el arrastre se cancela; era la vía real por la que llegaba un índice de manija
  a una geometría de otro tipo.

---

## 6. Tablero de referencia y modos de medición

El **tablero** es un sistema de coordenadas visible con el **cero declarado**,
al estilo del *datum* de la metrología: sirve para medir *posición* (cuánto se
desvía y cuánto gira la pieza) y no solo distancias sueltas. Convenios: +X a la
derecha, **+Y hacia arriba** (se invierte la Y de la imagen), ángulo en
(−180, 180] antihorario positivo.

El cero puede ser:

- **Centro del contorno** (recomendado): el centro geométrico de la pieza, el
  que se ve centrado.
- **Centro de masa**: el centroide. En piezas asimétricas cae visiblemente
  desplazado — en una pieza en L, 20 px por eje. Fue justo el origen de un fallo
  reportado: "no centra bien".
- **Centro de la imagen**: fijo en pantalla, para vigilar el centrado en un
  soporte.
- **Punto fijado a mano**, más un **ajuste fino** en X/Y aplicable a cualquiera
  de los anteriores.

**Dos modos de medición por pieza**:

- **Posición real (personalizada)**: lo de siempre; cada herramienta se juzga
  con sus tolerancias.
- **Especial (tablero centrado)**: además, la pieza se juzga por **dónde está**:
  desviación máxima del centro y giro máximo, que entran en el veredicto. La
  regla de giro **se salta sola** cuando la pieza es casi simétrica y su eje no
  es fiable, en vez de dar NG falsos.

La **regla graduada** (opcional) dibuja escalas en los bordes con números en la
unidad activa, una barra de escala y la marca del cursor.

---

## 7. Escala real: de píxeles a milímetros

Tres caminos, de menos a más robusto:

1. **Objeto de referencia**: mides algo de tamaño conocido y escribes sus
   milímetros. Simple y exacto en el plano de trabajo.
2. **Distancia de cámara + campo de visión** (modelo pinhole): estima mm/px a
   partir de la altura de la cámara.
3. **Marcador ArUco** (diccionario 4×4) de lado conocido: se detecta en cada
   frame y se calcula la **homografía del plano** imagen→mm. La escala se
   reajusta sola al acercar o alejar la cámara, y **corrige la perspectiva**:
   las herramientas de longitud convierten sus puntos por la homografía en vez
   de multiplicar por una constante.

La calibración se **sella con la cámara y la resolución** con las que se hizo: si
cambian, la app avisa de que la escala dejó de ser válida en vez de mostrar
milímetros silenciosamente equivocados.

El marcador ArUco reporta además un **indicador de calidad** (0–1) basado en la
uniformidad de sus lados y diagonales: mide cuán perpendicular está la cámara al
plano. Con la cámara muy inclinada, una escala única deja de ser fiable lejos
del marcador y el indicador lo dice.

### No todo lo que se mide es una longitud

Una escala px→mm solo sirve para longitudes, y durante mucho tiempo la
aplicación se la aplicó a todo. Cuatro pantallas distintas —el lienzo, el
diálogo de resultados, la ventana principal y el editor— decidían por su cuenta
cómo rotular una medida, y las cuatro aplicaban la misma regla equivocada: «todo
lo que no sea un ángulo va en milímetros». El resultado eran números falsos con
aspecto de buenos:

| Lo que se medía | Lo que se pintaba | Lo que era |
|---|---|---|
| Lados de un hexágono | `Lados (6): 6,00 mm` | un recuento |
| Área de una región | `mm` con la escala **lineal** | px², la escala entra al **cuadrado** |
| Circularidad, solidez, simetría | `0,93 mm` | adimensional |
| Dientes de un engranaje, agujeros, defectos | `mm` | recuentos |

La corrección no fue alargar la lista de excepciones de cada pantalla, porque el
problema no era la lista: era preguntárselo al **tipo de herramienta** en vez de
a la **medida**. La Región lo demuestra sola — mide seis cosas de tres clases
distintas con un solo tipo, así que el tipo no puede saberlo.

`ToolRunResult::kind` (`MeasuredKind`: longitud, ángulo, recuento, fracción,
área) lo decide **quien mide**, que es el único que lo sabe, y
`inspection::formatMeasure` es el **único sitio** donde se decide cómo se rotula.
Cuatro copias de una regla son cuatro sitios donde arreglar el mismo fallo.

Sin calibración se dan píxeles y se dicen; inventar milímetros sin escala sería
la peor salida de todas. Un recuento no depende de la escala y no cambia con
ella. Una fracción no lleva unidad, y esa ausencia es la respuesta, no un olvido.

De paso se arregló la columna `unit` de la tabla `Measurements`, que guardaba
`'px'` literal para toda medida —ángulos y recuentos incluidos—. Una columna que
siempre dice lo mismo no es un dato; esta además mentía, y el histórico existe
para poder releerse.

### La prueba que faltaba: la ida y vuelta

Hasta ahora no había ninguna prueba de que la calibración sirviera. Se comprobaba
la aritmética, que es exacta por construcción, y no lo único que importa: que una
pieza de N milímetros vuelva midiendo N milímetros.

Ahora se dibuja una figura de tamaño conocido en mm a una escala conocida, se
pasa por el pipeline y por la medición automática, y se exige el número de
vuelta. Barrido de 0,05 a 1,0 mm/px y de 80 a 800 px de pieza: **el peor error
del barrido es del 0,145 %**, y la mayoría de las combinaciones caen entre el
0,00 y el 0,05 %. Los dos métodos de calibración —objeto de referencia y
distancia+FOV— coinciden sobre la misma geometría, que es lo que había que
comprobar: si no coincidieran, uno de los dos mentiría.

**Y hay una trampa en el propio banco que merece quedar escrita**, porque quien
añada un test aquí se la va a encontrar. La primera versión daba errores de hasta
el **3,3 %** y parecía que la calibración perdía precisión con las piezas
pequeñas. No era eso: las figuras se dibujaban con `LINE_AA`, y OpenCV rasteriza
un disco antialiaseado **1,4 px más grande de lo nominal por cada lado**. Medido:
un Ø de 200 px sale con un contorno de 202,87. Ese sesgo es **constante** —no
escala con la pieza— así que sobre 800 px es un 0,3 % y sobre 80 px un 3,3 %, que
es exactamente la forma que tenía el «error». El mismo sesgo entraba por la
longitud de referencia y hacía que los dos métodos discreparan un 3,5 %.

Dibujando sin antialiasing el contorno mide exactamente lo nominal, y entonces se
ve lo que de verdad hace la herramienta: **0,06 px de error sobre un Ø de 600**.
La lección general es la de siempre en este proyecto —el error absoluto de un
borde no encoge con la pieza— y su consecuencia práctica sí es del operador: la
precisión relativa la fija **el tamaño en píxeles**, no la calibración.

**Dos fallos reales que salieron de las entradas degeneradas**, los dos de la
misma familia —una escala mala no falla, da números creíbles y equivocados—:

- `calibrationFromKnownLength` podía **desbordar a infinito** (px minúsculos y mm
  enormes), y `valid()` solo mira que la escala sea mayor que cero: infinito lo
  es. La aplicación se habría dado por calibrada y toda medida saldría `inf`.
  Ahora una escala que no es un número real deja la calibración inválida.
- Un **campo de visión de 180° o más** se aceptaba. La tangente de su mitad se
  dispara y a partir de ahí cambia de signo, así que salía una escala enorme —o
  negativa— dada por buena. Ahora se rechaza.

Con la interfaz de hoy no se llega a ninguno de los dos, porque el diálogo acota
lo que se puede teclear. Pero que se llegue o no depende de quién llame, y esta
es la única puerta por la que entra la escala a todo lo demás.

**Límite fundamental**: con una sola cámara 2D no se recupera la profundidad
punto a punto. Todo lo anterior vale para objetos **en el plano de trabajo**;
medir a distinta altura requeriría cámara de profundidad o estéreo.

---

## 8. Apariencia: el modelo y la detección de anomalías

Esta es la parte que la gente llama "la IA", así que conviene ser preciso con
los nombres.

### El modelo

- **EfficientNet-Lite4**, del zoo oficial de ONNX (~49 MB), ejecutado con **ONNX
  Runtime** en C++. El prompt original pedía Lite0, pero no hay un ONNX fiable
  publicado de Lite0; Lite4 es la misma familia y el mismo preprocesado.
- La herramienta `prepare_model` (se compila con el proyecto) **le recorta el
  clasificador**: en vez del softmax de 1000 clases, la salida pasa a ser el
  vector de características del *global average pooling* — un **embedding de
  1280 dimensiones**.
- Entrada 224×224, normalización `(x − 127) / 128`, disposición NHWC o NCHW
  autodetectada, salida **L2-normalizada** para que la similitud coseno sea
  comparable.
- Lo que entra al modelo no es el frame: es el **recorte canónico** de la pieza
  (256×256, sin fondo, orientado por el fixture), así que la comparación no
  depende de dónde ni cómo esté colocada.

### Qué NO se hace

- **No se entrena ni se reentrena nada.** El modelo va congelado. El nombre
  correcto es *transfer learning sin ajuste fino*, o "extractor de
  características congelado".
- No hay detección supervisada por *deep learning* (nada de YOLO ni segmentación
  semántica), ni *template matching* por correlación, ni OCR.

### Qué se hace

- **Registrar una pieza** es lo que en visión industrial se llama *teach-in* o
  *golden sample*: se le enseña al sistema cómo es la pieza buena. Se capturan
  ~30 muestras válidas y se acumula su estadística.
- La comparación es **detección de anomalías de una sola clase** (*one-class* /
  *unsupervised anomaly detection*, también *novelty detection*): solo se ven
  piezas buenas, nunca hace falta etiquetar defectos.
- El criterio: **similitud coseno** del embedding actual contra la media de la
  referencia, con umbral en `media − max(k·σ, 0.02)`. El mismo espíritu que el
  control estadístico de procesos. `k` es configurable (3 por defecto): más bajo
  = más estricto.
- Media y desviación se actualizan con el **algoritmo en línea de Welford**, en
  O(dimensiones) por muestra y sin guardar los embeddings anteriores. Eso es lo
  que el programa llama **aprendizaje incremental**: cada pieza buena confirmada
  produce una **versión nueva** de la referencia, y las anteriores nunca se
  borran.

### Con qué se parece esto en el mundo

La familia de métodos del *benchmark* MVTec AD —**PaDiM, PatchCore, SPADE,
FastFlow**— y los enfoques con autoencoders o GAN (*GANomaly*) comparten la idea
de comparar características de una red preentrenada contra las de piezas buenas.
Este programa es una versión deliberadamente ligera de esa idea: **un solo vector
por pieza** en lugar de un banco de parches, que es lo que permite que corra en
una PC modesta y sin GPU.

Sin el modelo, la aplicación **degrada a inspección solo con herramientas**: se
puede registrar y medir igual, avisando de que no habrá comparación de
apariencia.

---

## 9. Veredicto

`domain::combineVerdict` es lógica pura y combina tres cosas:

1. **Apariencia** (si hay modelo y referencia): anómala o no.
2. **Herramientas**: cuántas quedaron fuera de tolerancia.
3. **Posición** (solo en modo Especial): centrado y giro dentro de sus
   tolerancias.

OK exige las tres. El resumen explica **por qué** sale NG ("anomalía de
apariencia; 2 herramienta(s) fuera de tolerancia; pieza descentrada"), y cada
inspección guarda historial, miniatura y estadísticas del día.

---

## 10. Persistencia

SQLite en un solo archivo junto al ejecutable, con envoltura RAII propia
(`Db`/`Statement`), todo por `Result<T>`, claves foráneas activas, modo WAL y
`busy_timeout`. **Diez tablas** y migraciones versionadas por
`PRAGMA user_version`: el esquema va por la **v8** y cada salto está en su
propia migración, de modo que una base de datos vieja se actualiza sola al
abrirla.

Qué se guarda por pieza: referencias de embeddings **versionadas**, miniatura,
rasgo distintivo, ajuste de orientación, modo de medición y su tablero,
tolerancias de posición, perfil de detección y sus plantillas de herramientas
(una pieza puede tener varias, por ejemplo una por cara).

Aparte: ajustes de la aplicación (calibración, atajos, preferencias, controles
de cámara) e historial de inspecciones con estadísticas por día.

Todo lo que no está ligado a una pieza se puede **exportar e importar en JSON**
para clonar la puesta a punto a otra PC de la línea. Las plantillas de
herramientas se exportan aparte, para copiarlas entre piezas.

## 11. La interfaz

Estaba dentro del capítulo de persistencia, y no por ninguna razón: se fue
acumulando ahí. Los menús, el teclado, la barra de botones y qué significa
«restablecer» no tienen nada que ver con cómo se guardan las plantillas, y quien
buscara cualquiera de esas cuatro cosas no iba a mirar bajo «Persistencia».

### Los menús: dónde busca uno cada cosa

Dos entradas estaban colocadas por su implementación y no por su significado, y
juntas convertían la tarea más común en una búsqueda:

- **«Calibrar escala (mm)…»** vivía en *Fuente*, junto a «Buscar cámaras».
- **«Unidad de medida»** vivía en *Ver*, junto a «Mostrar contorno» — como si
  elegir milímetros o píxeles fuera cuestión de aspecto, cuando cambia el número
  que se apunta en el parte.

Para preparar una medición en milímetros había que visitar **dos menús que no
hablan de medir**. Lo que las une es la pregunta que contestan —con qué se
mide— así que ahora hay un menú **Medida** con la calibración, el marcador
ArUco, la unidad, «Medir pieza» y el modo de medición de la pieza. No quedan
duplicadas en su sitio anterior: dos caminos a lo mismo son dos cosas que
mantener, y a la primera divergencia una de las dos miente.

La otra mitad: **varias acciones existían solo en la barra**. «Inspeccionar»,
«Auto-inspección», «Medir pieza» y «Guardar plantilla» no estaban en ningún
menú, y una acción que solo vive en la barra no la encuentra quien navega con el
teclado — a los menús se va justo cuando no se reconoce el icono. Ahora todas
tienen su entrada, y la de auto-inspección es un **espejo en los dos sentidos**
del botón: si dijeran cosas distintas, el operador no sabría a cuál creer.

#### El conmutador que contestaba con un modal

Lo destapó el banco, y de la peor manera: el test que iba a comprobar ese espejo
**colgó la suite cinco minutos** hasta que hubo que matar el proceso. Encender la
auto-inspección sin cámara ni pieza abría un `QMessageBox` **modal**, y sin
pantalla eso bloquea para siempre.

El coste visible era peor que el del test. El operador encendía el conmutador, le
saltaba un diálogo diciendo que no, y el conmutador se apagaba solo: **tres pasos
para enterarse de algo que se podía ver antes de tocar nada**. Y la comprobación
vivía *después* de pulsar, que es el sitio equivocado.

Ahora está **apagado con su motivo en el tooltip** —«No se puede empezar todavía:
no hay ninguna pieza registrada seleccionada, no hay ninguna fuente en marcha»—
que es exactamente lo que este proyecto ya hace en los botones de borrar. Se lee
antes de pulsar, el botón y su acción de menú dicen lo mismo, y **se puede
probar**: un control que no se puede probar es un control que nadie prueba.

Con la auto-inspección **en marcha** el conmutador no se deshabilita aunque
falten condiciones, y eso es deliberado: si la fuente se cae, apagarla tiene que
seguir siendo posible o quedaría encendida sin forma de pararla.

Queda una red de seguridad dentro del slot, sin modal: si algo cambia entre que
se lee el estado y se pulsa, se revierte y se dice en la barra de estado.

### El teclado: dos huecos que solo se ven midiendo

Se contaron los botones visibles con texto y su política de foco. De 24, cinco
quedaban fuera del recorrido del tabulador, y **el lienzo estaba en `NoFocus`**.

El lienzo es el peor de los dos: era el **único sitio de la ventana al que el
teclado no podía llegar**, y es donde se trabaja. Sin foco no hay forma de saber
si está activo, y cualquier tecla que quisiera atender no le llegaría nunca. Pasa
a `StrongFocus` —por tabulador y por clic— y no a `ClickFocus`: a quien navega
con el teclado hay que dejarle llegar hasta ahí, no solo a quien usa el ratón.

El segundo era **«Mover/Elegir»**, y el detalle importa: es la forma de SALIR del
modo de dibujo. Dejarla fuera del recorrido deja atrapado dibujando a quien
navega así — una **trampa de foco** en el sentido literal de las guías. Estaba
puesto a `NoFocus` una línea después de crearse.

Los cuatro que **siguen fuera a propósito** son los de la barra de zoom.
Añadirían cuatro paradas al recorrido para acciones que ya tienen atajo, y un
recorrido largo se abandona. Hay un test que vigila que la excusa siga siendo
cierta: si alguien quitara esos atajos, las acciones se quedarían sin teclado y
el test lo diría. La misma lógica cubre los botones de la paleta, que se eligen
con *familia + dígito*.

### La barra de la ventana: trece botones sin jerarquía

La barra había ido creciendo hasta **trece botones repartidos en tres filas**,
todos del mismo peso y a la misma distancia unos de otros. Cuatro problemas, y
ninguno es de gusto:

- **Nada destacaba.** «Inspeccionar», que se pulsa cien veces al día, parecía tan
  importante como «Gestionar…», que se abre una vez al mes. Ahora hay **una**
  acción enfatizada, y solo una: dos o tres destacados no destacan ninguno.
- **Los desplegables se comían la fila.** Con factor de estiramiento, «Integrated
  Camera» ocupaba media ventana y empujaba los botones contra el borde derecho,
  lejos del combo al que se refieren. Un desplegable no se lee mejor por ser
  cuatro veces más ancho que su texto; los botones sí se encuentran mejor si
  están juntos. Ancho acotado.
- **No había grupos.** Trece cosas equidistantes se leen como una lista de trece
  cosas sin relación. Con un separador entre grupos se leen como tres
  decisiones: **qué miro** (fuente y zona), **qué mido** (pieza y plantilla) y
  **qué hago** (las acciones).
- **Dos botones para la misma decisión, cada uno cambiando de verbo.** «Zona de
  detección» pasaba a «Quitar zona» y «Zona libre» a «Quitar zona libre», así que
  en la barra podía leerse *«Zona de detección | Quitar zona libre»*: un botón
  diciendo lo que dibuja junto a otro diciendo lo que borra. Para saber qué había
  puesto había que leer los dos y deducirlo.

El último es el que más cambia. Ahora hay **un** control que dice siempre «Zona»
—o «Zona fija» / «Zona libre» cuando hay una— y un menú con las tres acciones por
su nombre: dibujar rectangular, dibujar libre, quitar. La activa va marcada y
«Quitar» está apagado, con su motivo, cuando no hay nada que quitar. El estado se
**lee**, en vez de deducirse de dos etiquetas que se mueven.

La barra **no tenía ni un test**, y es exactamente por eso por lo que fue
acumulando. Ahora hay tres, y no fijan el aspecto sino las decisiones: una sola
acción destacada, un solo control de zona con sus tres acciones, y los
desplegables sin comerse la fila.

### Restablecer es OLVIDAR, no escribir los valores de fábrica

La diferencia parece de matiz y es la que impide que la función se
desincronice. Cada sitio que **lee** un ajuste lleva su valor por defecto en la
propia llamada —`getInt("det_blur", 5)`—, así que borrando la clave el programa
vuelve exactamente a lo que hace en una máquina recién instalada.

Escribir en el restablecido una segunda copia de esos valores crearía **dos
listas que mantener a la vez**, y a la primera que alguien cambiara una sola,
«restablecer» dejaría la aplicación en un estado que no es ni el suyo ni el de
fábrica. La idea ya estaba escrita en `SettingsRepository::remove` —«olvidar un
ajuste NO es lo mismo que ponerlo a su valor por defecto»— y `forget()` la lleva
hasta el final: con prefijo vacío olvida todo; con un prefijo (`det_`, `cam_`)
solo esa familia, que es lo que permite restablecer una pestaña sin tocar la
calibración de la máquina.

Devuelve **cuántos** ajustes olvidó, porque «no había nada que restablecer» es
una respuesta distinta de «se restablecieron catorce cosas», y ninguna de las
dos es un error.

La confirmación dice las dos cosas que hacen falta para poder contestarla: **qué
se lleva** (calibración, detección, zona, preferencias, atajos, controles de
cámara, capas, tamaños de ventana) y **qué no toca** (piezas, plantillas,
historial). Un «¿está seguro?» a secas no se puede contestar — el operador no
sabe si va a perder sus piezas registradas. El botón por omisión es *Cancelar*:
en un diálogo destructivo, la tecla Intro no puede ser la que borra.

Y se dice qué **queda por aplicarse**: lo que se lee una sola vez al arrancar
—los atajos, la disposición de las ventanas— no puede rehacerse sin volver a
abrir, y callárselo dejaría al operador creyendo que el restablecido falló.

#### Y por pestaña, sin tocar el resto

El panel *Configurar* estrena **Restablecer** (el papel `RestoreDefaults` de Qt,
para que salga colocado donde el operador lo espera en su sistema y con el texto
de su idioma). Restablece **la pestaña que se está viendo**, no todo: quien viene
aquí a desenredar el umbral no quiere perder la calibración de la máquina — para
eso está la entrada de *Archivo*, con su propia confirmación.

Los valores de fábrica **no se escriben en la página**: salen de construir por
defecto las propias estructuras del modelo, `SegmentationOptions{}` y
`PipelineConfig{}`, que es donde ya vivían como inicializadores de miembro. Es la
misma regla que gobierna `forget()`, aplicada a la otra capa — escribir aquí una
copia crearía dos listas que mantener, y a la primera divergencia «restablecer»
dejaría la página en un estado que no es ni el suyo ni el de fábrica. El test lo
compara contra esas estructuras a propósito: si alguien cambia un valor por
defecto en el modelo, la página lo sigue o el test falla, nunca divergen en
silencio.

Un detalle que parece menor y no lo es: el umbral vuelve a **automático (−1)**,
no a un número. Son dos estados distintos — −1 significa que lo decide Otsu
mirando la imagen, y dejarlo en 128 clavaría la detección a un valor que nadie
eligió.

El botón está encendido **solo donde hay algo que restablecer**, y apagado con su
motivo en el resto: un botón vivo que no hace nada enseña a desconfiar de los
botones, y uno apagado sin explicación deja pensando qué falta.

### Volver a donde lo dejaste

*(Esto sí es persistencia y no interfaz: lo que se guarda es el estado de la
sesión. Se queda aquí a propósito.)*


Lo que el operador coloca una vez tiene que seguir colocado mañana. Lo contrario
no se percibe como un ajuste que falta, sino como que el programa **no se
acuerda de nada** — y a los dos días se deja de colocar.

Se recuerda, además de lo anterior:

- **La ventana**: tamaño, posición, monitor y si estaba maximizada
  (`saveGeometry`), junto a la disposición de paneles que ya se guardaba
  (`saveState`). Se restauran en ese orden y no al revés: `restoreState`
  reparte los paneles dentro del tamaño que tenga la ventana en ese momento.
- **Con qué se estaba trabajando**: la pieza, la plantilla y el tipo de fuente.
  Se recuerda la **elección**, no se reabre nada: la cámara guardada tampoco
  arranca sola, y un programa que al abrirse se pone a leer un fichero hace algo
  que nadie le ha pedido.
- **El contorno en vivo**, que era la única capa del menú *Ver* que no se
  recordaba, y **el desglose de tiempos por etapa**, que había que reactivar en
  cada sesión — justo cuando se está persiguiendo algo que tarda.
- **El tamaño de cada diálogo**, por nombre. Los diez abrían con un `resize()`
  fijo, y eso no es un valor por defecto sino una imposición.

Dos detalles que no son evidentes:

**No basta con guardar al cerrar.** La disposición de paneles ya se hacía así, y
un cierre que no pase por `closeEvent` —un corte de luz en la línea, un apagado
a lo bruto— se llevaba por delante justo lo que se coloca una vez. La geometría
se guarda además **dos segundos después** del último movimiento: un arrastre
entero cuesta una sola escritura, y un cierre brusco no pierde nada.

**Un tamaño guardado puede quedar inservible.** La sesión anterior corrió en un
monitor de 4K y hoy la máquina de línea tiene uno de 1366×768: el diálogo abriría
con los botones fuera de la pantalla y sin forma de alcanzarlos. Se acota a lo
que hay ahora, y por abajo a algo legible. Solo el tamaño, no la posición: un
diálogo se centra sobre su ventana padre, y recordar dónde estaba lo sacaría de
la pantalla en cuanto alguien mueva la aplicación.

### Los defectos que se encontraron y se arreglaron

Estaban aquí: **1 191 líneas** de bitácora dentro de un capítulo llamado
«Persistencia», donde la persistencia de verdad ocupa veintiuna. Ahora viven en
[BITACORA.md](BITACORA.md), que es donde alguien las buscaría.


## 12. Empaquetado

`.\run.ps1 -Package` genera un `.zip` que corre en una PC **sin MSYS2**: el
ejecutable, los plugins de Qt vía `windeployqt6`, el modelo, y las DLL restantes
resueltas **recorriendo los imports del binario con `objdump`** contra
`ucrt64\bin`.

Se usa `objdump` y no `ldd` a propósito: `ldd` resuelve según el PATH del shell
que lo ejecuta y llegaba a devolver DLL de otro runtime. Los imports, en cambio,
son propiedad del binario. Verificado arrancando el paquete con un PATH limpio.

---

## 13. Glosario: cómo se llama cada técnica

Para poder explicar el sistema con las palabras correctas, y no atribuirle
métodos que no usa:

| En el programa | Nombre técnico |
|---|---|
| Detección de la pieza | Umbralización de Otsu + morfología matemática + extracción de contornos |
| Centro y eje de la pieza | Momentos de imagen (centroide y eje principal) |
| Anclaje de herramientas | *Position fixture* / *reference frame* / *datum* / *fixturing* |
| Caliper | Detección de bordes por perfil de intensidad con interpolación subpíxel |
| Rectángulo mínimo | *Rotating calipers* (`minAreaRect`) |
| Escala en mm | Marcadores fiduciales (ArUco) + homografía de plano |
| "Entrenar" una pieza | *Teach-in* con *golden sample* (no hay entrenamiento de red) |
| Extracción de rasgos | *Transfer learning* sin ajuste fino / extractor congelado |
| Comparar apariencia | Detección de anomalías de una sola clase (*one-class*, *novelty detection*) |
| Umbral de anomalía | Similitud coseno con umbral tipo control estadístico (media − k·σ) |
| Aprendizaje incremental | Algoritmo en línea de Welford (estadística, no reentrenamiento) |

---

## 14. Cómo mejorarlo

Ordenado por relación entre lo que aporta y lo que cuesta. Son propuestas con
su contrapartida dicha en voz alta, y **las que se han ido haciendo quedan
tachadas y marcadas**, con lo que se midió al hacerlas.

Este párrafo decía «ninguna de estas cosas está hecha» y era falso en su propia
lista: tres de los ítems de abajo ya estaban tachados cuando se escribió. Lo
detectó la auditoría de documentación, y es el tipo de frase que envejece sola —
cierta el día que se escribe y mentira a la semana siguiente.

### Mejoras de precisión

1. **Iluminación antes que software.** El salto de calidad más grande y más
   barato no está en el código: es un aro de luz difusa y un fondo mate de color
   contrastado. La mitad de los problemas de segmentación desaparecen.
2. ~~**Segmentación adaptativa** (umbral local en vez de Otsu global)~~ —
   **probado y descartado**, con las medidas más abajo, en «Lo que se probó y no
   salió».
3. ~~**Corrección de distorsión de lente**~~ — **hecha**. Era, en efecto, la
   mejora de exactitud más seria que quedaba, y ahora tiene la cifra que le
   faltaba: con una lente de gama de consumo (k1 = -0,25), la misma pieza medía
   un **18,5 % menos** en una esquina que en el centro. Corregida, el peor error
   por posición baja de **14,01 % a 0,11 %**.

   Lo que no era evidente: **una calibración mal cubierta es peor que ninguna.**
   Con las tomas apiñadas por el centro, el ajuste sale con un error de
   reproyección de 0,404 px —indistinguible del de una buena— y deja el borde un
   **34,88 %** desviado. Por eso `calibrateLens` **rechaza** una cobertura pobre
   en vez de avisar, y por eso media ventana del asistente es una rejilla que
   dice qué zonas del encuadre faltan por cubrir.
4. **Subpíxel en el contorno**, no solo en las herramientas de borde: hoy el
   contorno es de resolución entera.

### Mejoras de inspección

5. **Banco de parches en vez de un vector** (estilo PatchCore/PaDiM): permitiría
   **localizar** el defecto en la pieza, no solo decir que algo cambió. Coste:
   mucha más memoria y CPU por pieza; habría que medir si sigue siendo viable
   sin GPU.
6. ~~**Múltiples referencias por pieza**~~ — **hecho el núcleo**. No era
   comodidad: mezclar dos acabados admisibles en una sola media no da falsos NG,
   deja **ciega** la referencia. La media se coloca entre los dos grupos, la
   banda se ensancha —medido, de **0,98 a 0,68**— y un defecto que se detectaba
   pasa. Separadas, cada variante conserva su media y su banda y una pieza es
   buena si alguna la reconoce.

   Falta la interfaz para registrar un acabado nuevo: hoy se guarda con
   `saveReference(pieza, referencia, "nombre")` y el motor ya juzga contra
   todas.
7. ~~**Mapa de calor de diferencia** contra el recorte de referencia~~ —
   **hecho**. Vive en `vision/difference_map.cpp` y lo consume el diálogo de
   resultado (`ui/inspection_result_dialog.cpp`), con su prueba en
   `tests/test_difference_map.cpp`. Seguía listado como pendiente: lo destapó
   la auditoría de documentación.
8. **Curva ROC con piezas malas reales**: hoy el umbral es estadístico. Con un
   puñado de piezas defectuosas etiquetadas se podría elegir el umbral que
   maximiza el acierto en lugar de asumir 3σ.

### Mejoras de operación

9. **Disparo por sensor** (fotocélula o señal externa) en vez de inspección
   continua por temporizador, para líneas con cadencia fija.
10. **Salida a PLC / señal digital** del veredicto: sin eso, la app informa pero
    no actúa sobre la línea.
11. ~~**Exportar el historial a CSV/Excel** y un informe por turno~~ —
    **hecho**. El informe de turno está en `domain/shift_report.cpp` y la
    exportación en `ui/history_dialog.cpp`, con `tests/test_shift_report.cpp`.
    También seguía listado como pendiente.
12. **Multi-cámara** (varias vistas de la misma pieza) con veredicto combinado.

### Mejoras técnicas

13. **Firmar el ejecutable y hacer un instalador** en vez del `.zip` portable.
14. **Integración continua** que compile y pase los tests en cada commit; hoy
    eso se hace a mano en la máquina de desarrollo.
15. **Traducciones**: la interfaz está en español y los textos van con `tr()`,
    así que falta solo generar los `.ts`/`.qm`.
16. **Pruebas con cámara real automatizadas**: hoy toda la batería usa imágenes
    sintéticas y el hardware se prueba a mano.

### Lo que se probó y no salió

#### Segmentación adaptativa contra los degradados de luz

Estaba en esta misma lista como la mejora número 2, y parecía evidente: Otsu
elige **un** corte para toda la imagen, así que da por supuesto que el fondo es
igual de claro en todas partes. Con una lámpara a un lado eso deja de ser
cierto: el corte queda bien en el centro y mal en los extremos, y el contorno se
desvía hacia el lado iluminado.

Se probaron **cuatro** formas de arreglarlo. Ninguna se puede enviar, y todas
fallan por el mismo motivo de fondo, que conviene decir primero:

> **Los métodos de libro para un degradado suponen que el objeto es pequeño
> comparado con el encuadre.** Todos necesitan estimar «cómo sería el fondo
> aquí», y para eso miran un entorno que tiene que contener fondo de verdad. En
> esta aplicación la pieza ocupa buena parte del encuadre **por diseño** —es una
> cámara mirando UNA pieza—, así que ese entorno está lleno de pieza y la
> estimación se contamina justo con lo que se pretendía separar.

Medido con un banco de cuatro escenas: un degradado sintético de 40 a 200 con la
pieza destacando 60 niveles sobre su fondo local; una escena fácil de contraste
alto; una pieza que ocupa media imagen; y cinco fotografías del corpus real. La
cifra de las tres primeras es intersección sobre unión contra la verdad conocida.

| intento | degradado | escena fácil | fotografía real |
|---|---|---|---|
| Otsu global (lo que hay hoy) | 0,23 | **1,000** | referencia |
| 1. `cv::adaptiveThreshold` | — | 0,178 | tres bolas **+268 %** |
| 2. Restar un desenfoque muy ancho | 0,64 | 0,603 | tuerca **−69 %**, y 19 s en cinco fotos |
| 3. Estimar el fondo excluyendo la pieza (dos pases) | 0,27 | 1,000 | — |
| 4. Estimar el fondo por morfología | **resuelto** | 1,000 | tuerca **+153 %** |

Lo que enseñó cada uno:

1. **Umbral local.** Un umbral local sobre el interior liso de una pieza no ve
   una zona lisa, ve grano: cualquier pieza más grande que el entorno de
   comparación se vacía por dentro. Y aquí el caso malo es el normal.
2. **Restar un desenfoque ancho.** El mismo fallo por otro camino: sobre una
   pieza grande el desenfoque se come a la propia pieza, así que restarlo le
   quita su señal y el interior se va a gris medio.
3. **Excluir la pieza del cálculo** (un Otsu de tanteo, estimar la luz solo con
   los píxeles de fondo, e interpolar por encima del hueco que deja la pieza).
   Arregla lo de vaciar las piezas —la escena fácil vuelve a 1,000— pero **no
   arregla el degradado**, que era para lo único que existía: de 0,23 a 0,27. El
   tanteo se equivoca justo cuando el degradado es fuerte, o sea cuando hace
   falta.
4. **Morfología** (una apertura con un elemento mayor que la pieza borra la
   pieza y deja el fondo). Es el único que resuelve el degradado, y además no
   necesita saber dónde está la pieza. Pero al aplanar una escena que **no**
   tenía degradado, el fondo queda pegado a un solo valor y ocupa el 96 % de la
   imagen: Otsu se queda sin dos poblaciones que separar y corta dentro del
   ruido. La tuerca del corpus pasaba de 18 095 a 45 828 px². Un guardián que
   solo aplique la corrección cuando el recorrido de la luz supere cierto umbral
   deja tres de las cinco fotografías **intactas**, pero las otras dos se siguen
   moviendo — y elegir ese umbral para que la tuerca quede fuera es ajustarlo al
   corpus, no medirlo.

**Qué haría falta para retomarlo.** Un corpus de fotografías con degradado real
y contorno de referencia conocido. Todo lo de arriba se midió contra escenas
sintéticas y contra fotos que *no* tienen el problema: eso alcanza para
descartar, y no alcanza para ajustar.

Mientras tanto, lo que hay para una iluminación desigual sigue siendo lo de
siempre, y es más barato: **arreglar la luz**, el umbral manual, o el pincel de
corregir el borde.

### Lo que NO conviene hacer

- **Entrenar una red propia** con las piezas de la línea: exige cientos de
  ejemplos etiquetados, incluidos defectos que casi nunca se tienen, y ata el
  sistema a un modelo que hay que mantener. El enfoque de una sola clase existe
  precisamente para evitarlo.
- **Medir profundidad con esta cámara.** No es cuestión de software: una sola
  vista 2D no la contiene. Si hace falta, es cambio de hardware (estéreo o
  cámara de profundidad).
