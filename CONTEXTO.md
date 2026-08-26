# Contexto — PC Inspector

**Empieza por aquí.** Este fichero es el mapa: qué es esto, cómo se trabaja en
él, y a qué documento ir según lo que busques. Los cuatro documentos grandes
responden a cuatro preguntas distintas y no hay que leerlos enteros.

---

## Qué es

Una aplicación de escritorio para **inspección visual industrial 2D**,
**completamente offline**. Un operador coloca una pieza bajo una cámara; la
aplicación la separa del fondo, le calcula un sistema de coordenadas propio
—el *fixture*, que hace que las cotas sigan a la pieza aunque llegue girada— y
ejecuta sobre ella las **herramientas de medición** que alguien dibujó una vez y
quedaron guardadas en una plantilla.

Cada cota lleva su tolerancia y devuelve un veredicto **OK / NG**. Todo queda en
una base SQLite local: piezas, plantillas, calibración píxel→milímetro e
histórico de inspecciones.

Es una **demo seria**, no un producto certificado: se parece conceptualmente a
VisionMaster de Hikrobot, a Cognex In-Sight o a HALCON, y hay que ser honesto
sobre en qué se les parece y en qué no (ver *lo que hay que mejorar*).

## Qué hay, en números

| | |
|---|---|
| Código fuente | 48 673 líneas, 205 ficheros |
| Pruebas | 42 272 líneas, 77 ficheros, **1 254 casos** |
| Clases de herramienta de medición | 33 |
| Commits | 300 |

Se compila con **MSYS2 UCRT64**, C++20, `-Werror`. `.\run.ps1` prepara todo
—verifica paquetes, descarga el modelo, compila y lanza—; `-Test` corre la
batería y `-Package` arma un zip portable.

## Los cuatro documentos

| Documento | Responde a | Para quién |
|---|---|---|
| [PROMPT_MAESTRO_PC_INSPECTOR.md](PROMPT_MAESTRO_PC_INSPECTOR.md) | **Lo que queremos**: la especificación original, el alcance y las fases | quien necesite saber qué se prometió |
| [README.md](README.md) | **Lo que hay**, desde fuera: manual de uso, pantalla por pantalla | el operador y quien evalúa la aplicación |
| [ARQUITECTURA.md](ARQUITECTURA.md) | **Lo que hay**, desde dentro: cómo funciona cada subsistema y **por qué se decidió así** | quien vaya a tocar el código |
| [MEJORAS.md](MEJORAS.md) | **Lo que hay que mejorar**: lo pendiente, lo medido y lo que se sabe que falla | quien planifique el siguiente paso |

## Seguir trabajando solo

`reanudar.ps1` retoma el trabajo cuando vuelve a haber cupo. Se instala una vez:

```powershell
.\reanudar.ps1 -Instalar      # tarea horaria; -Publicar si además debe hacer push
.\reanudar.ps1 -Desinstalar   # quitarla
```

Lo intenta **cada hora** en vez de adivinar la hora del reinicio, porque el cupo
va en ventanas móviles y esa hora cambia de un día para otro. Si no hay cupo,
sale en segundos y se anota en `.claude/reanudar.log`.

Cuatro barreras, cada una por un motivo: Visual Studio Code tiene que estar
abierto (es lo que se pidió, y es la señal de que alguien está delante); no
arranca si ya hay una sesión de Claude viva (la tarea salta a la hora en punto,
que es justo cuando puede haber alguien trabajando a mano); un cerrojo impide
que se pise a sí misma; y **no publica** salvo que se le diga.

Cada ejecución coge **una** tarea de `MEJORAS.md`, la hace entera y para.

## Las capas, y la regla que no se toca

```
core/        Result<T>, utilidades sin dependencias
domain/      las entidades: pieza, plantilla, inspección
camera/      enumerar y abrir cámaras, imágenes y vídeos
vision/      segmentar, contorno, fixture, formas, calibración
ml/          embeddings ONNX para el registro de piezas
database/    esquema SQLite y migraciones
repositories/  acceso a datos, uno por entidad
inspection_editor/  geometrías, ejecutor de herramientas, lienzo
engine/      el motor que junta visión + herramientas + veredicto
ui/          Qt Widgets: ventana principal, páginas, diálogos
```

**Las dependencias van hacia abajo y nunca al revés.** `vision/` no sabe que
existe Qt; `ui/` no calcula geometría. Esta estructura no se reorganiza: partir
un fichero grande **dentro** de su capa está bien, mover responsabilidades entre
capas no.

## Cómo se trabaja aquí

Estas reglas están puestas porque cada una viene de un fallo real:

1. **Medir, no suponer.** Toda decisión técnica se justifica con el número que
   la respalda. En los comentarios del código y en `ARQUITECTURA.md` verás
   frases del tipo «medido sobre siete fotos: por nivel salen seis piezas, por
   borde salen las siete». Eso no es adorno: es lo que permite revisar la
   decisión más tarde.
2. **Lo que se probó y no funcionó también se escribe**, y el experimento se
   **borra** en vez de dejarlo aparcado. Un módulo apagado que nadie mide se
   pudre.
3. **No se inventa la verdad de campo.** Si no se puede contar los dientes de
   una rueda a ojo con fiabilidad, no se fija un número: se dice que no se
   puede. Ya ha pasado inventarlo y salió caro.
4. **Antes de cerrar cualquier cosa**: compilación limpia con `-Werror`, `ctest`
   entero en verde, y **arrancar la aplicación**. Las tres, no dos.
5. **Una prueba por cada lógica nueva no trivial**, y la prueba explica en su
   cabecera *por qué existe*, no solo qué comprueba.
6. Actualizar `README.md` y `ARQUITECTURA.md` con el cambio, y **un commit
   atómico por asunto**.
7. Las fotos de prueba viven fuera del repositorio, en
   `C:\Users\furro\Pictures\IMG-MC`. Las pruebas que las usan **se saltan solas**
   si no están.

## Trampas conocidas de este repositorio

Anotadas porque han costado tiempo más de una vez:

- **`-Werror` convierte un aviso en un fallo de compilación.** Si una prueba
  deja una variable sin usar, el binario **no se regenera** y los resultados que
  imprime son los del binario viejo. Comprueba siempre que compiló.
- **`../src/ui` existe también dentro del árbol de compilación y está vacío.**
  Cualquier guardia que recorra directorios tiene que anclarse en un fichero
  conocido, no en la carpeta. Ha pasado dos veces.
- **Los finales de línea son LF y ahora git los impone.** Lo fueron mal mucho
  tiempo: `.gitattributes` declaraba LF y los dos documentos grandes estaban
  guardados con CRLF, así que cada edición automatizada tenía que convertir a
  mano. Ya está renormalizado y `*.md text eol=lf` le quita la decisión a la
  heurística de git — que con el README se equivocaba y lo tomaba por binario
  por culpa de un `
` doblado.
- **El ejecutable se queda bloqueado** si la aplicación sigue abierta, y el
  enlazado falla con «Permission denied». Se borra el `.exe` y se recompila.
- **Un recuento no mide la calidad de una silueta.** Hubo una segmentación que
  marcaba solo el aro de nylon de cien tuercas y seguía diciendo «100 piezas».
  Cuando cambies algo de visión, **dibuja la máscara sobre la foto y mírala**.
