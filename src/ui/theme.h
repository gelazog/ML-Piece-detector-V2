#pragma once

#include <QColor>
#include <QPainter>
#include <QPen>
#include <QString>

namespace pci::ui::theme {

// LOS COLORES DE LA APLICACIÓN, POR LO QUE SIGNIFICAN.
//
// Antes de esto no había ninguno: DOCE ficheros distintos llamaban a
// `setStyleSheet` con el color escrito a mano, y el mismo significado salía de
// tres colores diferentes según por dónde entraras. Un operador que aprende que
// «lo rojo no cumple» tiene que poder fiarse en toda la aplicación.
//
// Los nombres dicen el PAPEL, no el aspecto: `kBad`, no `kRojo`. Un token
// llamado «rojo» invita a usarlo para decorar, y en cuanto un rojo decorativo
// convive con un rojo de alarma, el rojo deja de querer decir nada.
//
// TODOS ESTÁN MEDIDOS contra la fórmula de contraste de WCAG 2.2, y hay una
// prueba que lo vuelve a calcular: `tests/test_theme.cpp`. No es celo: de los
// siete colores que había antes, CINCO fallaban —la pista de escena estaba a
// 1,53:1, casi invisible sobre el panel—, y ninguno de los siete lo sabía.

// --- Texto ------------------------------------------------------------------
// 18,42:1 sobre blanco.
inline constexpr const char* kInk = "#141414";
// Secundario: explicaciones, unidades, lo que acompaña. 6,05:1.
inline constexpr const char* kInkMuted = "#5f6368";
// Apagado: lo que existe pero no cuenta ahora mismo. 4,70:1.
//
// Es más oscuro de lo que parece necesario a propósito. El gris de antes
// (#999999, 2,85:1) se leía bien en la pantalla del que lo escribió y no en un
// taller con luz de nave; «apagado» tiene que seguir siendo LEGIBLE, o deja de
// ser apagado y pasa a ser invisible.
inline constexpr const char* kInkOff = "#70747a";

// --- Veredicto --------------------------------------------------------------
// Cada estado va con su color Y con su campo de fondo, para poder pintar tanto
// una palabra suelta como un aviso entero sin mezclar dos rojos distintos.
// LOS TRES SE SEPARAN TAMBIÉN POR LUMINANCIA, no solo por tono.
//
// El primer intento puso «no cumple» en #b3261e y «aviso» en #8a5300, los dos
// con contraste de sobra sobre blanco — y con luminancias de 0,111 y 0,116. Es
// decir: casi el mismo gris. Para un daltónico deutan, que es el más común, ese
// par es indistinguible; en una foto en blanco y negro también. La prueba de la
// paleta lo cazó antes de que llegara a la pantalla.
//
// Ahora hay 1,26 entre «no cumple» y «aviso», y 1,39 entre «no cumple» y
// «cumple»: se distinguen aunque se pierda el color entero.
inline constexpr const char* kBad = "#b3261e";        // 6,54:1 sobre blanco
inline constexpr const char* kBadField = "#fce8e6";   // con kBad encima
inline constexpr const char* kWarn = "#a15c00";       // 5,19:1
inline constexpr const char* kWarnField = "#fff4e0";  // con kWarn encima
inline constexpr const char* kGood = "#14532d";       // 9,11:1
inline constexpr const char* kGoodField = "#e8f5e9";  // con kGood encima

// --- Los mismos papeles, sobre superficie OSCURA ----------------------------
//
// La aplicación no es de un solo tema y no lo era a propósito: el informe de
// inspección y el calibrador de lente se pintan sobre negro —porque encima
// llevan imagen, y un marco claro alrededor de una foto la falsea— mientras el
// resto va sobre el gris de ventana de Windows.
//
// Eso está BIEN, pero obliga a tener dos juegos. Poner el rojo de fondo claro
// (#b3261e, 6,54:1 sobre blanco) encima de #1a1a1a da 1,95:1: ilegible. Un
// token por papel no basta; hacen falta dos, y que se sepa cuál va dónde.
inline constexpr const char* kSurfaceDark = "#1a1a1a";
inline constexpr const char* kInkOnDark = "#e6e6e6";        // 13,94:1
inline constexpr const char* kInkMutedOnDark = "#a8adb3";   // 7,70:1
inline constexpr const char* kBadOnDark = "#f2836b";        // 6,82:1
inline constexpr const char* kWarnOnDark = "#f0b26a";       // 9,35:1
inline constexpr const char* kGoodOnDark = "#7ddba0";       // 10,37:1

// --- PASTILLAS DE VEREDICTO: fondo saturado con texto claro encima ----------
//
// Faltaban, y se notaba. Los chips de OK/NG y las luces de estación llevaban su
// color escrito a mano en cada sitio, y al verlos juntos salió el mismo desorden
// que motivó esta paleta: TRES verdes distintos para «bien» (#1e6f2f, #2e7d32 y
// el kGood de aquí), tres rojos para «mal» y tres ámbares para «aviso».
//
// Se conservan los valores que ya estaban en uso —no hay motivo para cambiar el
// aspecto— pero ahora hay uno solo de cada, y su contraste está calculado:
inline constexpr const char* kGoodChip = "#1e6f2f";  // 6,23:1 con kInkOnChip
inline constexpr const char* kBadChip = "#8f1f1f";   // 8,81:1
inline constexpr const char* kWarnChip = "#a15c00";  // 5,19:1
// El texto que va encima de las tres. Blanco puro y no kInkOnDark: sobre un
// fondo saturado, un gris claro pierde el poco contraste que hay en el ámbar.
inline constexpr const char* kInkOnChip = "#ffffff";

// --- Superficie -------------------------------------------------------------
inline constexpr const char* kOutline = "#c9c9c9";
inline constexpr const char* kSurfaceSunken = "#f5f6f7";

[[nodiscard]] inline QColor color(const char* token) { return QColor(QString(token)); }

// Texto de un color de la paleta, con lo que se le quiera añadir detrás.
//
// Existe para que el sitio de uso quede legible —`textStyle(kWarn)` en vez de
// una cadena con un `#a15c00` dentro— y, sobre todo, para que el color venga de
// AQUÍ. Doce ficheros escribiendo su propio hexadecimal es como se llegó a
// tener cinco colores que no contrastaban sin que nadie lo supiera.
[[nodiscard]] inline QString textStyle(const char* ink, const QString& extra = {}) {
    return QStringLiteral("color:%1;%2").arg(QString(ink), extra);
}

// Un aviso con su campo, su borde y su color de texto, de una pieza.
//
// Existe para que nadie vuelva a escribir a mano «color:X; background:Y;
// border:1px solid Z»: es donde se colaban las combinaciones sin contraste.
[[nodiscard]] inline QString noticeStyle(const char* ink, const char* field) {
    return QStringLiteral("color:%1; background:%2; border:1px solid %1;"
                          " border-radius:4px; padding:6px;")
        .arg(QString(ink), QString(field));
}

// Una pastilla de veredicto entera: fondo saturado, texto claro y esquinas
// redondeadas. Existe por lo mismo que `noticeStyle`: es donde se escribían a
// mano las parejas fondo/texto, y donde se colaban las que no contrastaban.
// --- LO QUE SE DIBUJA ENCIMA DE LA IMAGEN -----------------------------------
//
// Estos no van sobre una superficie de la aplicación sino sobre la FOTO, que
// puede ser de cualquier color. Por eso son saturados y por eso el contorno
// lleva halo: un verde fino sobre una pieza clara desaparece.
//
// Se nombran por lo que significan, igual que los demás. Antes eran
// `QColor(0, 220, 0)` repetido en cuatro ficheros, y ya había empezado a
// derivar — el `QColor(255, 60, 60)` del punto de origen es EL MISMO que el
// contorno de «no cumple» del informe de inspección. Un color con dos
// significados es el primer paso hacia dos colores con un significado.
//
// Los valores son exactamente los que había: aquí se les pone nombre, no se
// les cambia el aspecto. Unificar los que colisionan es la decisión siguiente y
// se toma aparte.
inline constexpr int kDrawFound[3] = {0, 220, 0};      // lo que se ha detectado
inline constexpr int kDrawAxis[3] = {0, 200, 255};     // el eje de la pieza
inline constexpr int kDrawOrigin[3] = {255, 60, 60};   // el punto de origen
inline constexpr int kDrawMissing[3] = {255, 120, 120};  // no hay pieza que medir
inline constexpr int kDrawBoard[3] = {255, 200, 0};    // las esquinas del tablero
inline constexpr int kDrawToolBad[3] = {255, 120, 0};  // una cota que no cumple
inline constexpr int kDrawVeilAlpha = 160;             // el velo bajo un rótulo

// EL HALO BAJO EL CONTORNO, y no es adorno.
//
// Una línea de color sobre una FOTO no tiene contraste garantizado: depende de
// lo que haya debajo, que puede ser cualquier cosa. Medido sobre el banco, el
// contraste del rojo del contorno contra los píxeles por los que pasa:
//
//     foto            rojo p05   rojo mediana   con halo p05   mediana
//     arandelas-1        1,02        1,22          5,57         7,27
//     engranaje-1        1,90        2,32         11,33        13,83
//     tornillos-1        2,15        2,64         12,84        15,74
//
// El color solo no llega ni al 3:1 que necesita un elemento gráfico; con el
// halo negro debajo pasa de 5 a 15. O sea que lo que hace visible el contorno
// NO es su color: es el borde oscuro que lleva pegado.
//
// Se probó a cambiar el rojo por el de veredicto (`kBadOnDark`, más claro) y
// sale PEOR en las siete fotos —mediana 1,17 contra 1,22 en la peor— porque es
// más claro y las piezas son claras. El color no era el problema.
inline constexpr double kDrawHaloWidth = 3.0;
inline constexpr double kDrawLineWidth = 2.0;
inline constexpr int kDrawHaloAlpha = 150;

// Dibuja `shape` con su halo debajo. `draw` recibe el pincel ya puesto.
template <typename Draw>
void withHalo(QPainter& painter, const QColor& colour, Draw&& draw) {
    QPen halo(QColor(0, 0, 0, kDrawHaloAlpha));
    halo.setWidthF(kDrawHaloWidth);
    halo.setCosmetic(true);
    painter.setPen(halo);
    draw();
    QPen line(colour);
    line.setWidthF(kDrawLineWidth);
    line.setCosmetic(true);
    painter.setPen(line);
    draw();
}

[[nodiscard]] inline QColor drawColor(const int (&rgb)[3], int alpha = 255) {
    return QColor(rgb[0], rgb[1], rgb[2], alpha);
}

// --- PASTILLAS DE ESTADO DE LA VENTANA --------------------------------------
//
// Distintas de las de veredicto: aquellas dicen OK/NG sobre una pieza, estas
// dicen en qué estado está la ventana —qué pieza se mide, en qué modo, si el
// borde lleva una corrección a mano—. Tienen dos posiciones: EN REPOSO, que es
// lo que la aplicación hace por su cuenta, y ELEGIDA, que es cuando el operador
// ha tocado algo. La segunda destaca porque destacar significa «esto lo has
// decidido tú».
//
// Salen de recoger una deriva pillada en el acto. Tres pastillas escritas en
// tres sitios usaban DOS azules casi iguales para el mismo significado —#7fd1ff
// y #7fd6ff— y DOS tintas oscuras casi iguales encima —#08243a y #0b2a35—.
// Nadie eligió tener dos: se copió el estilo, se tecleó de memoria, y la copia
// salió una cifra distinta.
//
// Aquí el problema no era el contraste —los dos pares pasan de sobra, 8,4:1 el
// de reposo y 9,3:1 el elegido— sino que un significado tuviera dos colores. Es
// el mismo desorden que motivó la paleta entera, en pequeño.
inline constexpr const char* kChipRest = "#3a3a3a";
inline constexpr const char* kInkOnChipRest = "#dddddd";  // 8,37:1 sobre kChipRest
inline constexpr const char* kChipChosen = "#7fd6ff";
inline constexpr const char* kInkOnChipChosen = "#0b2a35";  // 9,28:1 sobre kChipChosen
// Y el verde, que NO es otra pastilla elegida: dice que hay una corrección a
// mano encima del borde. Es un estado distinto y por eso lleva otro color.
inline constexpr const char* kChipEdited = "#8ce99a";

// --- LAS BANDAS QUE SE PINTAN SOBRE EL VÍDEO -------------------------------
//
// Son tres: el aviso de puesta en marcha, la lectura continua del tablero y su
// estado de fuera de tolerancia. Van sobre la imagen, así que llevan fondo
// propio y oscuro; los tokens de superficie clara no sirven aquí.
//
// Estaban escritas a mano, y con el desorden de siempre. El estilo de la lectura
// —`color:#7fd6ff; background:#10222b; padding:3px; border-radius:3px;`— estaba
// tecleado DOS VECES palabra por palabra, en el sitio donde se crea la banda y
// en el sitio donde se restaura al salir de la alarma. Y había dos azules de
// fondo, `#1b2b38` y `#10222b`, que se distinguen **1,13:1**: es decir, el mismo
// color. Nadie eligió tener dos.
//
// AQUÍ SE NOMBRA Y SE UNIFICA EL FONDO, NO SE CAMBIA EL ASPECTO. Se comprobó
// antes de tocar nada, porque la sospecha era otra: que el estado de alarma se
// distinguiera solo por TONO, que es el error que esta paleta lleva escrito
// arriba —dos veredictos con la misma luminancia son el mismo gris para un
// daltónico deutan—. Medido, los dos fondos se separan **1,59:1**, por encima
// del 1,26 que la propia paleta ya acepta entre «no cumple» y «aviso». Así que
// no era un defecto y no se cambia: la alarma además va en negrita, que es la
// señal que no depende del color.
inline constexpr const char* kBandField = "#10222b";       // el fondo de las tres
inline constexpr const char* kInkOnBand = "#7fd6ff";       // 10,08:1 — la lectura en vivo
inline constexpr const char* kProseOnBand = "#d7ecff";     // 12,74:1 — un aviso con frases
inline constexpr const char* kBandAlarm = "#7a1f1f";       // 1,59:1 contra kBandField
inline constexpr const char* kInkOnBandAlarm = "#ffdede";  // 8,20:1

// Una banda de una pieza, para que el estilo no se vuelva a teclear dos veces.
[[nodiscard]] inline QString bandStyle(const char* ink, const char* field,
                                       bool bold = false) {
    return QStringLiteral("color:%1; background:%2; padding:3px; border-radius:3px;%3")
        .arg(QString(ink), QString(field),
             bold ? QStringLiteral(" font-weight:bold;") : QString());
}

// --- LA BALDOSA DEL MOSAICO QUE SE ESTÁ MIDIENDO ---------------------------
//
// El mosaico marca con verde la pieza que se mide, y la pastilla de estado de la
// ventana marca ESA MISMA COSA con `kChipChosen`, que es azul. Un significado
// con dos colores es el desorden que motivó esta paleta, y aquí está otra vez.
//
// Se le pone nombre CONSERVANDO el verde a propósito. Unificarlos cambia lo que
// el operador ve todo el día en la pantalla que más usa, y eso se decide con la
// pantalla delante y no de paso; lo que sí se arregla ahora es que el color deje
// de estar tecleado —el mismo verde estaba escrito DOS veces en el mismo
// fichero y en dos formatos, `QColor(0, 190, 0)` y `#00be00`— porque así es como
// se acaba teniendo tres verdes.
//
// Contraste medido: 6,95:1 la tinta sobre su chapa, y 4,53:1 el marco elegido
// contra el de reposo, que es su vecino. La baldosa en reposo reusa
// los tokens de pastilla en reposo, que ya existían para lo mismo.
inline constexpr const char* kTileMeasured = "#00be00";
inline constexpr const char* kInkOnTileMeasured = "#0a1e0a";  // 6,95:1 sobre kTileMeasured
inline constexpr int kTileBadgeAlpha = 220;      // la chapa de la que se mide
inline constexpr int kTileBadgeRestAlpha = 170;  // la de las demás

// La pastilla en reposo: la aplicación decide, y no llama la atención.
[[nodiscard]] inline QString chipRestStyle() {
    return QStringLiteral("color:%1; background:%2; border-radius:8px; padding:1px 6px;")
        .arg(QString(kInkOnChipRest), QString(kChipRest));
}

// La pastilla elegida: lo ha decidido el operador, y se ve.
[[nodiscard]] inline QString chipChosenStyle(const char* background = kChipChosen) {
    return QStringLiteral("color:%1; background:%2; border-radius:8px; padding:1px 6px;"
                          " font-weight:bold;")
        .arg(QString(kInkOnChipChosen), QString(background));
}

// EL HUECO DE «AQUÍ TODAVÍA NO HAY IMAGEN».
//
// Estaba escrito TRES veces, idéntico, en el informe de inspección, la ventana
// principal y el gestor de piezas:
//
//     "background:#1a1a1a; color:#888; border:1px solid #444;"
//
// Tres copias de la misma decisión son tres sitios que se pueden cambiar por
// separado, y es como se llegó a tener tres verdes distintos para «bien».
//
// De paso se arregla el borde. El `#444` sobre el `#1a1a1a` del fondo da
// **1,8:1**, o sea que casi no se ve: WCAG pide 3:1 para el contorno de un
// control, que es lo que hace que el hueco se lea como una caja y no como un
// agujero. Con `kInkMutedOnDark` el marco queda tan visible como su propio
// texto, que para un hueco vacío es exactamente lo que hace falta.
[[nodiscard]] inline QString placeholderStyle() {
    return QStringLiteral("background:%1; color:%2; border:1px solid %2;")
        .arg(QString(kSurfaceDark), QString(kInkMutedOnDark));
}

[[nodiscard]] inline QString chipStyle(const char* background, const QString& extra = {}) {
    return QStringLiteral("background:%1; color:%2; border-radius:8px; padding:3px;%3")
        .arg(QString(background), QString(kInkOnChip), extra);
}

}  // namespace pci::ui::theme
