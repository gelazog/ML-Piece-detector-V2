# La foto como fuente, y las herramientas que se explican enteras

Dos frentes independientes. El segundo es corto y el primero tiene más fondo del
que parece.

---

## Lo que se ha medido antes de planificar

**El texto de las herramientas se corta, y se corta por código, no por espacio.**
`tool_palette.cpp` tiene una función `firstLine()` que trunca la descripción en el
primer salto de línea. Las 32 descripciones son de 181 a 915 caracteres y todas
son multilínea: la primera línea es el resumen y el resto explica **cómo se
traza**, que es justo lo que hace falta para usarla.

Y hay una incoherencia encima: la línea de ayuda reserva alto para **dos**
renglones (`setFixedHeight(lineSpacing * 2 + 2)`) y solo pinta uno. Se está
pagando el espacio y no se usa.

El resto del texto vive únicamente en el **tooltip**, o sea que solo aparece si
pasas el ratón por encima y esperas. La auditoría de interfaz ya dejó dicho que
lo que se enseña al pasar el ratón es un extra, nunca el canal principal.

**La foto no existe.** Se puede abrir una imagen *de archivo*, pero no se puede
decir «hazme una foto AHORA con la cámara y trabajo sobre ella». Y es lo que se
quiere de verdad: con el vídeo en vivo la pieza tiembla, la segmentación late y
dibujar una herramienta sobre algo que se mueve es puntería. Congelar un frame
bueno permite trazar, calibrar y medir con calma.

**Dónde ya hay foto y dónde no** (inventario hecho):

| Sitio | Estado |
|---|---|
| Calibración | Ya trabaja sobre una instantánea |
| Registro de pieza | Ya tiene «Agregar imágenes…» |
| Editor de plantilla | Ya pregunta: frame actual o fichero |
| Ventana principal | **Falta**: no hay forma de congelar |
| Auto-inspección | Pide `streaming_`, que una fuente de fichero ya cumple |

O sea que el hueco real es **uno**: capturar. Lo demás es que la foto capturada
aparezca como opción en los sitios que ya preguntan.

---

## T1 · Que la descripción se lea entera

Usar los dos renglones que ya se están reservando, envolviendo el texto, y
recortar **solo** si de verdad no cabe — y entonces con puntos suspensivos, que
es la forma honesta de decir «hay más». El texto completo sigue en el tooltip,
pero deja de ser el único sitio donde está.

Hay que **medir** antes de fijar nada: cuántas de las 32 descripciones caben en
dos renglones al ancho real del panel, y cuánto habría que recortar de las que
no. Si salen muchas cortadas, el arreglo no es apretar más el texto sino darle
una tercera línea o repensar las descripciones.

## T2 · Auditar las OPCIONES de cada herramienta

Aparte del texto: comprobar que los parámetros que cada herramienta ofrece son
los que de verdad usa. Lo que hay que cruzar, herramienta por herramienta:

- los campos de su `…Geometry`,
- los que el ejecutor lee de verdad en `tool_executor.cpp`,
- los que la interfaz deja tocar,
- y lo que su descripción promete.

Los fallos que se buscan son los dos que hacen daño: un parámetro que se puede
mover y **no hace nada** (el operador cree que ajusta y no ajusta) y un parámetro
que **falta** aunque la herramienta dependa de él. Este proyecto ya encontró uno
de cada: las herramientas de referencia sin desplegable, y la banda de búsqueda
del arco calculada a partir de un radio absurdo.

Es un barrido de coherencia sobre 32 herramientas: **va a un agente**, con el
encargo de traer una tabla y no un juicio.

## T3 · La foto: capturar el frame actual

Un botón **Capturar foto** en la barra, y a partir de ahí la fuente es esa foto.
Encaja sin inventar nada: ya existe `FrameSource`, así que la foto es una fuente
de imagen que en vez de leer de disco lleva el `QImage` dentro.

Lo que hay que resolver bien:

- **Volver a la cámara** tiene que ser tan fácil como congelar, y el botón tiene
  que decir en cuál de los dos estados está.
- **El indicador de la barra** dice `Foto`, no `Cám`: mientras miras una foto no
  estás mirando la cámara.
- **La calibración no se invalida al congelar.** La foto sale de la misma cámara,
  la misma óptica y la misma distancia, así que la escala sigue valiendo — y
  avisar aquí sería un aviso que se aprende a ignorar. Ojo: esto es lo contrario
  de lo que pasa al abrir un fichero, y por eso hay que distinguir el origen de
  la imagen y no solo que «es una imagen».
- **Al soltar la foto, la cámara vuelve donde estaba**, sin resondear controles
  ni relanzar el perfil de exposición.

## T4 · La foto disponible donde ya se ofrece la cámara

Con T3 hecho, repasar los sitios que ya preguntan por la imagen y añadir la foto
capturada como opción, con el **mismo vocabulario en los tres**: registro, editor
de plantilla y calibración. Hoy cada uno lo dice a su manera.

## T5 · Pruebas con imágenes diversas

El banco actual usa figuras sintéticas limpias y las dos imágenes de muestra del
repo. Falta variedad real: piezas descentradas, tocando el borde, con sombra,
sobre fondo claro y sobre fondo oscuro, y a distintas resoluciones. Con
`pci_probe` esto se puede correr desde un script y comprobar sin abrir la
aplicación.

---

## Orden y reparto

1. **T2 a un agente** (barrido de las 32 herramientas, solo informe) y **T5 a
   otro** (banco de imágenes diversas), en paralelo desde el principio: ninguno
   depende del otro ni de mí.
2. **T1 lo hago yo**, midiendo primero cuánto cabe de verdad.
3. **T3 y T4 los hago yo**, en ese orden, porque T4 solo tiene sentido con la
   foto ya existiendo.

Skills: `qt-ui-design` para T1 y T3 (ya usada en la barra de fuente),
`cpp-testing` para T5 si hace falta afinar el arnés.

**Regla de cierre**: cada punto con su test, su documentación (README +
ARQUITECTURA) y su commit atómico. Cuando estén todos, este fichero se borra.
