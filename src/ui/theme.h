#pragma once

#include <QApplication>
#include <QColor>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPen>
#include <QProxyStyle>
#include <QStyleOption>
#include <QString>
#include <QStyleFactory>

#include <algorithm>
#include <cmath>

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

// --- CLARO U OSCURO: EL TEMA DE LA VENTANA ----------------------------------
//
// Hasta ahora la aplicación solo sabía ser clara, y a propósito: con el modo
// oscuro de Windows el texto se quedaba a 1,26:1 (ver `applyApplicationLook`).
// El operador puede pedir el oscuro en Preferencias; por defecto sigue claro.
//
// Los tokens que van sobre la VENTANA (texto, veredicto, campos de aviso)
// tienen dos valores, uno por tema. Los que van sobre una superficie que no
// cambia con el tema —las pastillas, las bandas del vídeo, lo que se dibuja
// encima de la imagen— siguen teniendo uno solo.
//
// EL TEMA SE ELIGE AL ARRANCAR Y NO CAMBIA HASTA EL SIGUIENTE ARRANQUE. Las
// hojas de estilo se escriben al construir cada widget: cambiar de tema a
// media sesión dejaría un rótulo con la tinta del tema viejo sobre el fondo del
// nuevo, que es exactamente el 1,05:1 que motivó todo esto.
enum class Scheme { Light, Dark };

// Lo que el operador elige. El número es el que se guarda en los ajustes
// (`pref_theme`), así que no se reordena.
enum class ThemeChoice { Light = 0, Dark = 1, System = 2 };

namespace detail {
inline Scheme gActiveScheme = Scheme::Light;
}  // namespace detail

[[nodiscard]] inline Scheme activeScheme() { return detail::gActiveScheme; }

// Un número guardado que no se reconoce (un ajuste de otra versión, una base
// tocada a mano) vuelve al claro: es el tema que se sabe que funciona.
[[nodiscard]] inline ThemeChoice themeChoiceFromSetting(int stored) {
    switch (stored) {
        case static_cast<int>(ThemeChoice::Dark): return ThemeChoice::Dark;
        case static_cast<int>(ThemeChoice::System): return ThemeChoice::System;
        default: return ThemeChoice::Light;
    }
}

// «Como Windows» se resuelve aquí, una vez: oscuro solo si el sistema dice
// oscuro. Un sistema que no dice nada (`Unknown`) se queda en claro.
[[nodiscard]] inline Scheme resolveScheme(ThemeChoice choice, Qt::ColorScheme system) {
    switch (choice) {
        case ThemeChoice::Dark: return Scheme::Dark;
        case ThemeChoice::System:
            return system == Qt::ColorScheme::Dark ? Scheme::Dark : Scheme::Light;
        case ThemeChoice::Light: break;
    }
    return Scheme::Light;
}

// UN COLOR CON UN VALOR POR TEMA.
//
// Se convierte solo a `const char*`, así que todos los sitios que ya escribían
// `textStyle(kWarn)` o `QString(kInk)` siguen igual y reciben el valor del tema
// activo sin tocarlos. `in()` pide el de un tema concreto, que es lo que usan
// las pruebas para medir los dos.
struct Adaptive {
    const char* light;
    const char* dark;

    [[nodiscard]] constexpr const char* in(Scheme scheme) const {
        return scheme == Scheme::Dark ? dark : light;
    }
    // NOLINTNEXTLINE(google-explicit-constructor): la conversión es el diseño.
    operator const char*() const { return in(activeScheme()); }
};

// --- Texto ------------------------------------------------------------------
// Tinta oscura SIEMPRE, sea cual sea el tema: para escribir sobre algo que es
// claro por sí mismo, como la muestra del color de la mesa.
inline constexpr const char* kInkOnLight = "#141414";
// 18,42:1 sobre blanco. En oscuro, 11,60:1 sobre el fondo de los campos.
inline constexpr Adaptive kInk{kInkOnLight, "#e8eaed"};
// Secundario: explicaciones, unidades, lo que acompaña. 6,05:1; en oscuro 7,08:1.
inline constexpr Adaptive kInkMuted{"#5f6368", "#b4b9bf"};
// Apagado: lo que existe pero no cuenta ahora mismo. 5,13:1 sobre blanco y
// 4,74:1 sobre la ventana; en oscuro 5,38:1 sobre los campos.
//
// Era #70747a, medido solo contra BLANCO (4,70:1). Sobre el gris de la ventana
// (#f5f6f7), que es donde va casi siempre, daba 4,34:1: por debajo del mínimo.
// Lo cazó la prueba que mide cada token contra las dos superficies de su tema.
//
// Es más oscuro de lo que parece necesario a propósito. El gris de antes
// (#999999, 2,85:1) se leía bien en la pantalla del que lo escribió y no en un
// taller con luz de nave; «apagado» tiene que seguir siendo LEGIBLE, o deja de
// ser apagado y pasa a ser invisible.
inline constexpr Adaptive kInkOff{"#6a6e74", "#9ca1a8"};

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
//
// En oscuro son los mismos tres de `k…OnDark` (más abajo), que ya estaban
// medidos para fondo oscuro, sobre campos oscuros de su mismo tono: 5,84:1,
// 7,28:1 y 8,25:1 contra su campo.
inline constexpr Adaptive kBad{"#b3261e", "#f2836b"};        // 6,54:1 sobre blanco
inline constexpr Adaptive kBadField{"#fce8e6", "#3d1f1b"};   // con kBad encima
inline constexpr Adaptive kWarn{"#a15c00", "#f0b26a"};       // 5,19:1
inline constexpr Adaptive kWarnField{"#fff4e0", "#3a2c14"};  // con kWarn encima
inline constexpr Adaptive kGood{"#14532d", "#7ddba0"};       // 9,11:1
inline constexpr Adaptive kGoodField{"#e8f5e9", "#173222"};  // con kGood encima

// --- EL AVISO ROJO FUERTE: tinta oscura sobre rosa, en los dos temas --------
//
// Es el de «el corte SÍ toca la pieza», y era el último color escrito a mano en
// `src/ui`. Se dejó así porque mide MEJOR que los tokens: #3a1010 sobre #ffd9d9
// da 12,83:1, y `kBad` sobre `kBadField` da 5,55:1. Aquí se le pone nombre sin
// cambiarle el valor. No depende del tema: es una caja clara con su propia
// tinta, y en el oscuro destaca todavía más, que para una alarma está bien.
inline constexpr const char* kAlarmInk = "#3a1010";
inline constexpr const char* kAlarmField = "#ffd9d9";
inline constexpr const char* kAlarmEdge = "#c04040";

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
// El contorno de la rejilla del calibrador de lente, que va sobre superficies
// OSCURAS fijas: por eso no cambia con el tema.
inline constexpr const char* kOutline = "#c9c9c9";
// El fondo de la ventana.
inline constexpr Adaptive kSurfaceSunken{"#f5f6f7", "#202124"};
// El fondo de campos, listas y tablas.
inline constexpr Adaptive kSurfaceBase{"#ffffff", "#2a2c30"};

// --- SELECCIÓN Y ENLACE -----------------------------------------------------
//
// La paleta clara usaba `kChipChosen` (#7fd6ff) para la selección y para los
// enlaces. Medido contra la fórmula de abajo: la fila elegida se separaba del
// fondo de la lista 1,62:1 —WCAG pide 3:1 a lo que distingue el estado de un
// control— y un enlace, que es TEXTO, se leía a 1,62:1 sobre blanco.
//
// La selección clara pasa a un azul medio: 3,49:1 contra el fondo de la lista
// y 3,22:1 contra la ventana, con tinta oscura encima a 5,28:1 (también es la
// tinta de los iconos de las herramientas, que se pintan sobre ella cuando la
// herramienta está marcada). En oscuro, el azul claro de siempre sí sirve:
// 8,64:1 contra la lista y su tinta a 9,28:1.
inline constexpr Adaptive kSelection{"#3a8fd0", "#7fd6ff"};
inline constexpr Adaptive kInkOnSelection{kInkOnLight, "#0b2a35"};
inline constexpr Adaptive kLink{"#1a5fa0", "#8cc8ff"};  // 6,59:1 / 7,87:1

// EL MARCO DE LOS CONTROLES: lo que dice «aquí se escribe» o «esto se pulsa».
//
// Fusion no lo saca de ningún papel de la paleta: lo calcula como el fondo de
// la ventana oscurecido un 40 %. Medido pintado: 2,01:1 en el tema claro y
// 1,15:1 en el oscuro, donde oscurecer un gris casi negro no da nada. WCAG pide
// 3:1. Con estos: 3,43:1 en claro y 4,15:1 en oscuro contra la ventana.
inline constexpr Adaptive kControlOutline{"#80858c", "#7c828a"};

[[nodiscard]] inline QColor color(const char* token) { return QColor(QString(token)); }

// LA FÓRMULA DE CONTRASTE DE WCAG 2.2, EN UN SOLO SITIO.
//
// Antes vivía copiada dentro de `tests/test_theme.cpp`: si otra prueba
// necesitaba medir un contraste —como la que comprueba que el modo oscuro de
// Windows no rompe la paleta de la aplicación— la alternativa era copiarla
// otra vez, que es exactamente el defecto que motivó tener una paleta con
// nombre en primer lugar. Ahora la cuenta se hace aquí una vez y todo lo demás
// la llama.
[[nodiscard]] inline double relativeLuminance(const QColor& colour) {
    const auto channel = [](int value) {
        const double c = value / 255.0;
        return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(colour.red()) + 0.7152 * channel(colour.green()) +
           0.0722 * channel(colour.blue());
}

[[nodiscard]] inline double contrastRatio(const QColor& a, const QColor& b) {
    const double la = relativeLuminance(a);
    const double lb = relativeLuminance(b);
    const double hi = std::max(la, lb);
    const double lo = std::min(la, lb);
    return (hi + 0.05) / (lo + 0.05);
}

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

// El aviso rojo fuerte, de una pieza (ver `kAlarmInk`).
[[nodiscard]] inline QString alarmNoticeStyle() {
    return QStringLiteral("color:%1; background:%2; border:1px solid %3;"
                          " border-radius:4px; padding:6px; font-weight:bold;")
        .arg(QString(kAlarmInk), QString(kAlarmField), QString(kAlarmEdge));
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

// EL RESTO DE LO QUE SE DIBUJA SOBRE LA IMAGEN, con nombre.
//
// El lienzo del editor tenía 85 colores tecleados en su propio fichero, fuera
// del alcance de la guardia de paleta (que solo miraba `src/ui`). 54 tenían un
// papel y aquí se les pone nombre por lo que hacen. Los otros 31 eran la tabla
// que da a cada TIPO de herramienta su color: su blanco de reserva pasa a
// `kDrawSelected` y los 30 colores se quedan en el lienzo, en su única función,
// porque no es un papel sino una identidad —«el calibre es
// cian»— y este fichero no conoce los tipos de herramienta. Donde dos significaban lo mismo con dos
// valores casi iguales se queda uno: el fondo de la lectura del cursor
// (10,34,43) era el `kBandField` (#10222b = 16,34,43) de las bandas del vídeo
// escrito de memoria, y la chapa de la pieza medida del lienzo (0,190,0 con
// alfa 210) era la del mosaico (`kTileMeasured`, alfa 220).
//
// NINGUNO DEPENDE DEL TEMA DE LA VENTANA: van sobre la foto, que es igual de
// clara o de oscura se elija el tema que se elija. Lo que los hace legibles
// sobre cualquier foto es el halo (`strokeWithHalo`, `drawTextWithHalo`).
inline constexpr int kDrawPass[3] = {0, 220, 0};        // una cota que cumple
inline constexpr int kDrawFail[3] = {255, 70, 70};      // una cota que no cumple
inline constexpr int kDrawSelected[3] = {255, 255, 255};  // lo elegido, lo que se traza
inline constexpr int kDrawHandleFill[3] = {40, 40, 40};   // el cuerpo de una manija
inline constexpr int kDrawLink[3] = {150, 255, 255};    // de qué herramienta sale un dato
inline constexpr int kDrawSnap[3] = {255, 230, 0};      // el borde donde se pegará
inline constexpr int kDrawCloseHere[3] = {0, 220, 0};  // el vértice que cierra un trazo
inline constexpr int kDrawZone[3] = {255, 210, 0};      // la zona de detección
inline constexpr int kDrawAnchor[3] = {255, 0, 255};    // el rasgo que fija la pieza
inline constexpr int kDrawGrid[3] = {120, 200, 255};    // la rejilla del tablero
inline constexpr int kDrawGridAxis[3] = {0, 220, 255};  // sus ejes y su origen
inline constexpr int kDrawGridInk[3] = {170, 220, 255};  // sus números
inline constexpr int kDrawReadoutInk[3] = {160, 225, 255};  // la lectura del cursor
inline constexpr int kDrawStraight[3] = {90, 180, 255};   // un tramo recto del contorno
inline constexpr int kDrawArc[3] = {255, 165, 40};        // un tramo en arco
inline constexpr int kDrawHole[3] = {230, 110, 230};      // un agujero
inline constexpr int kDrawAddPiece[3] = {0, 210, 90};     // pincel: esto es pieza
inline constexpr int kDrawAddBackground[3] = {230, 60, 60};  // pincel: esto es fondo
inline constexpr int kDrawOtherPiece[3] = {70, 170, 90};  // las demás piezas del encuadre
inline constexpr int kDrawOtherPieceDim = 120;  // su verde cuando hay una elegida
inline constexpr int kDrawCaption[3] = {235, 235, 235};  // texto sobre un velo
inline constexpr int kDrawRulerTick[3] = {200, 200, 200};  // las marcas de la regla
inline constexpr int kDrawFrame[3] = {120, 130, 145};     // el marco del encuadre
inline constexpr int kDrawLabelAlpha = 225;  // la caja de una medida: tapa de verdad
inline constexpr int kDrawCaptionAlpha = 170;  // el velo de un resumen o de un número
inline constexpr int kDrawPanelAlpha = 205;  // la banda de la regla
inline constexpr int kDrawPanel[3] = {18, 18, 18};

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

// El color del halo: negro con el alfa medido arriba.
[[nodiscard]] inline QColor haloColor() { return QColor(0, 0, 0, kDrawHaloAlpha); }

// Dibuja `shape` con su halo debajo. `draw` recibe el pincel ya puesto.
template <typename Draw>
void withHalo(QPainter& painter, const QColor& colour, Draw&& draw) {
    QPen halo(haloColor());
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

// EL MISMO HALO, PARA CUALQUIER PLUMA Y PARA EL TEXTO.
//
// `withHalo` sirve cuando la línea tiene el grosor de siempre. El lienzo del
// editor dibuja herramientas de 1,8 y de 3 px, a rayas y continuas, y rótulos
// de texto; y NADA de eso llevaba halo. Solo el contorno de la pieza lo tenía.
// Con mesa blanca —el montaje normal del taller— el trazo blanco de lo que se
// está dibujando, el ámbar de la zona o el cian de la rejilla del tablero se
// quedaban entre 1,0:1 y 1,6:1 contra lo que tenían debajo.
//
// El halo es continuo aunque la línea vaya a rayas: una raya de color sobre
// una pista oscura se lee; una raya oscura con otra de color encima, con el
// patrón desfasado por el grosor, se ve como un borrón.
[[nodiscard]] inline QPen haloPenFor(const QPen& line) {
    QPen halo(haloColor());
    halo.setWidthF(std::max(line.widthF(), 1.0) + kDrawHaloWidth - 1.0);
    halo.setCosmetic(line.isCosmetic());
    halo.setCapStyle(Qt::RoundCap);
    halo.setJoinStyle(Qt::RoundJoin);
    return halo;
}

// Dibuja lo que haga `draw` dos veces: con el halo de `line` y con `line`.
template <typename Draw>
void strokeWithHalo(QPainter& painter, const QPen& line, Draw&& draw) {
    painter.setPen(haloPenFor(line));
    draw();
    painter.setPen(line);
    draw();
}

// Texto con su contorno oscuro, para rótulos que van sobre la foto sin caja.
inline void drawTextWithHalo(QPainter& painter, const QPointF& baseline, const QString& text,
                             const QColor& ink) {
    QPainterPath path;
    path.addText(baseline, painter.font(), text);
    QPen halo(haloColor());
    halo.setWidthF(kDrawHaloWidth);
    halo.setJoinStyle(Qt::RoundJoin);
    painter.save();
    painter.setBrush(Qt::NoBrush);
    painter.strokePath(path, halo);
    painter.fillPath(path, ink);
    painter.restore();
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

// Las dos chapas del número de pieza, con su alfa. Las usan el mosaico y el
// lienzo del editor, que tenían cada uno su propia copia: el lienzo pintaba la
// medida en (0,190,0) con alfa 210 y el número de las demás en (200,230,205),
// el mosaico en `kTileMeasured` con alfa 220 y `kInkOnChipRest`. La misma pieza
// con dos chapas distintas según dónde se mire.
[[nodiscard]] inline QColor tileBadge(bool measured) {
    QColor chip = measured ? QColor(QString(kTileMeasured)) : QColor(0, 0, 0);
    chip.setAlpha(measured ? kTileBadgeAlpha : kTileBadgeRestAlpha);
    return chip;
}

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

// El velo que va bajo un rótulo dibujado sobre la imagen.
//
// El alfa ya tenía nombre (`kDrawVeilAlpha`) y el color no, así que el negro
// seguía tecleado en cada sitio. Media cosa con nombre es como se acaba con dos
// velos de distinta opacidad.
[[nodiscard]] inline QColor veil(int alpha = kDrawVeilAlpha) { return QColor(0, 0, 0, alpha); }

[[nodiscard]] inline QString chipStyle(const char* background, const QString& extra = {}) {
    return QStringLiteral("background:%1; color:%2; border-radius:8px; padding:3px;%3")
        .arg(QString(background), QString(kInkOnChip), extra);
}

// EL MODO OSCURO DE WINDOWS ROMPÍA EL TEXTO.
//
// `src/main.cpp` no fijaba estilo ni paleta, así que Qt (≥6.7, con
// `qmodernwindowsstyle`) seguía el tema del sistema. Con Windows en oscuro la
// ventana se volvía oscura pero los tokens de arriba —pensados para fondo
// claro: `kInk` a 18,42:1 sobre BLANCO, no sobre `#1a1a1a`— seguían pintándose
// encima. Medido: `kInk` (#141414) sobre el gris oscuro que pone Windows por
// defecto (#2d2d2d) da 1,26:1; el mínimo de WCAG es 4,5:1. El texto no
// desaparecía del todo, pero se acercaba.
//
// El arreglo es fijar el estilo Fusion —que no seguía el tema del sistema— y
// una paleta explícita construida desde los mismos tokens, para que la app se
// vea IGUAL con Windows en claro o en oscuro. `tests/test_dark_mode_palette.cpp`
// fuerza un esquema de color oscuro con
// `QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark)` antes
// de llamar a esta función y comprueba que, después, el texto sigue siendo
// oscuro sobre fondo claro con contraste ≥4,5:1 — no basta con que contraste,
// porque el contraste es simétrico: una paleta invertida (fondo oscuro, texto
// claro) también pasaría esa cuenta y seguiría siendo el error.
// LA PALETA DE UN TEMA, construida desde los tokens.
//
// Aparte de `applyApplicationLook` para poder medirla sin tocar la aplicación:
// `tests/test_theme_choice.cpp` comprueba las dos con `contrastRatio` —texto
// a 4,5:1 y selección a 3:1— sin tener que instalar ninguna.
//
// Se parte de `QPalette(botón, ventana)` y no de `QPalette()`: el constructor
// vacío copia la paleta del SISTEMA, así que los papeles que no se fijan aquí
// (luces y sombras de los bordes en relieve) salían del tema de Windows. Con el
// sistema en oscuro y la app en claro, un borde podía salir con la sombra del
// tema contrario.
[[nodiscard]] inline QPalette applicationPalette(Scheme scheme) {
    const QColor windowBg = color(kSurfaceSunken.in(scheme));
    const QColor ink = color(kInk.in(scheme));
    const QColor inkOff = color(kInkOff.in(scheme));
    const QColor base = color(kSurfaceBase.in(scheme));
    const QColor highlight = color(kSelection.in(scheme));
    const QColor highlightedInk = color(kInkOnSelection.in(scheme));

    QPalette palette(windowBg, windowBg);
    palette.setColor(QPalette::Window, windowBg);
    palette.setColor(QPalette::WindowText, ink);
    palette.setColor(QPalette::Base, base);
    palette.setColor(QPalette::AlternateBase, windowBg);
    palette.setColor(QPalette::ToolTipBase, base);
    palette.setColor(QPalette::ToolTipText, ink);
    palette.setColor(QPalette::Text, ink);
    palette.setColor(QPalette::Button, windowBg);
    palette.setColor(QPalette::ButtonText, ink);
    palette.setColor(QPalette::BrightText, color(kBad.in(scheme)));
    palette.setColor(QPalette::Highlight, highlight);
    palette.setColor(QPalette::HighlightedText, highlightedInk);
    palette.setColor(QPalette::Accent, highlight);
    palette.setColor(QPalette::Link, color(kLink.in(scheme)));
    palette.setColor(QPalette::LinkVisited, color(kLink.in(scheme)));
    palette.setColor(QPalette::PlaceholderText, inkOff);

    // Deshabilitado: sigue siendo legible (`kInkOff` está medido a 4,74:1),
    // solo que apagado. Un control inactivo no debería volverse invisible.
    palette.setColor(QPalette::Disabled, QPalette::WindowText, inkOff);
    palette.setColor(QPalette::Disabled, QPalette::Text, inkOff);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, inkOff);
    return palette;
}

// Fija el estilo, el tema de los tokens y la paleta. Se llama UNA vez, antes de
// construir ninguna ventana (ver arriba por qué no se cambia a media sesión).
// Sin tema, el claro: es lo que la aplicación ha sido siempre.
// FUSION, CON EL MARCO DE LOS CONTROLES DEL TEMA.
//
// Fusion pinta el marco de campos, botones, desplegables y casillas con
// `ventana.darker(140)` (ver `kControlOutline` para las cifras). No hay papel
// de paleta que lo cambie, así que aquí, solo para esos controles, se le pasa
// una copia de la opción con una «ventana» elegida para que, oscurecida ese
// 40 %, salga justo `kControlOutline`. El resto del dibujo de esos controles
// usa la base y el botón, no la ventana, así que no cambia nada más.
class ThemedStyle : public QProxyStyle {
public:
    ThemedStyle() : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion"))) {}

    void drawPrimitive(PrimitiveElement element, const QStyleOption* option,
                       QPainter* painter, const QWidget* widget) const override {
        switch (element) {
            case PE_FrameLineEdit:
            case PE_PanelLineEdit:
                if (drawSeeded<QStyleOptionFrame>(option, [&](const QStyleOption* seeded) {
                        QProxyStyle::drawPrimitive(element, seeded, painter, widget);
                    })) {
                    return;
                }
                break;
            case PE_PanelButtonCommand:
            case PE_IndicatorCheckBox:
            case PE_IndicatorRadioButton:
                if (drawSeeded<QStyleOptionButton>(option, [&](const QStyleOption* seeded) {
                        QProxyStyle::drawPrimitive(element, seeded, painter, widget);
                    })) {
                    return;
                }
                break;
            default: break;
        }
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }

    void drawComplexControl(ComplexControl control, const QStyleOptionComplex* option,
                            QPainter* painter, const QWidget* widget) const override {
        const auto draw = [&](const QStyleOption* seeded) {
            QProxyStyle::drawComplexControl(
                control, static_cast<const QStyleOptionComplex*>(seeded), painter, widget);
        };
        if ((control == CC_SpinBox && drawSeeded<QStyleOptionSpinBox>(option, draw)) ||
            (control == CC_ComboBox && drawSeeded<QStyleOptionComboBox>(option, draw))) {
            return;
        }
        QProxyStyle::drawComplexControl(control, option, painter, widget);
    }

    // La «ventana» que, oscurecida como hace Fusion, da el marco del tema.
    [[nodiscard]] static QColor outlineSeed() {
        const QColor outline = color(kControlOutline);
        return QColor::fromHsv(outline.hsvHue(), outline.hsvSaturation(),
                               std::min(255, static_cast<int>(std::lround(outline.value() * 1.4))));
    }

private:
    template <typename Option, typename Draw>
    static bool drawSeeded(const QStyleOption* option, Draw&& draw) {
        const auto* typed = qstyleoption_cast<const Option*>(option);
        if (typed == nullptr) {
            return false;
        }
        Option seeded(*typed);
        seeded.palette.setColor(QPalette::Window, outlineSeed());
        draw(&seeded);
        return true;
    }
};

inline void applyApplicationLook(QApplication& app, Scheme scheme = Scheme::Light) {
    detail::gActiveScheme = scheme;
    app.setStyle(new ThemedStyle());
    app.setPalette(applicationPalette(scheme));
}

}  // namespace pci::ui::theme
