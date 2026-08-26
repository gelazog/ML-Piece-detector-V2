# Las 32 herramientas de medición

Catálogo completo: qué mide cada una, cómo se traza y **qué NO puede medir**.
Estaba dentro del `README.md`, en medio del paso «dibujar sobre el vídeo en
vivo», y ocupaba 409 de sus 1.670 líneas — una cuarta parte del manual entero
metida dentro de un solo paso de nueve.

Para **usarlas** desde la aplicación, el manual sigue siendo
[README.md](README.md). Para saber **cómo están construidas por dentro**,
[ARQUITECTURA.md](ARQUITECTURA.md). El mapa de todo está en
[CONTEXTO.md](CONTEXTO.md).

---

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

  **Y ahora sabe decir que no.** Antes publicaba SIEMPRE: probada sobre las
  dieciséis fotos del banco con el eje trazado de punta a punta, decía que sí
  a las dieciséis —arandelas, tuercas y cáncamos incluidos—, con perlas como
  «paso=1,3 px» en una bolsa de arandelas. Un paso de 1,3 píxeles no es una
  rosca, es la rejilla de la cámara. Ahora se niega cuando **no hay filete**
  (plegando el perfil por su periodo, lo que se repite no llega a un píxel de
  altura) o cuando **los dos lados del eje dan pasos distintos**, que suele
  ser que el eje coge una cresta sí y otra no y el paso sale al doble. Los dos
  números ya se calculaban y se quedaban en un aviso al final del texto.

  Con eso: 16 de 16 rechazadas con el eje mal trazado, y las medidas buenas
  intactas — sobre la varilla que lleva impreso «6 hilos por pulgada», mide el
  paso con **0,9 % de error**.

  Otro detalle que se corrigió: cuando el flanco no se puede medir, antes
  escribía **«flanco=0,00°»**, que salía en catorce de las dieciséis fotos. Un
  flanco de cero grados sería una rosca de paredes verticales; ahora dice que
  no se puede medir.

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
