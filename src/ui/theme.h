#pragma once

#include <QColor>
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

}  // namespace pci::ui::theme
