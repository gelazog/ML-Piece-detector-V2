// LA PALETA SE COMPRUEBA SOLA.
//
// Petición de uso: «sigue haciendo las interfaces del usuario más estéticos, y
// cómodos a simple vista».
//
// Lo primero que apareció al medir no fue una cuestión de gusto: de los siete
// colores que la aplicación usaba para decir algo, CINCO no llegaban al
// contraste mínimo de WCAG 2.2. La pista que aconseja cambiar de método estaba
// a 1,53:1 sobre su panel — prácticamente invisible—, el verde de «copiado» a
// 2,15:1 y el gris de «no cuenta» a 2,85:1.
//
// Ninguno de los siete lo sabía, porque nadie lo había calculado nunca. Esta
// prueba lo calcula, y vuelve a hacerlo cada vez que alguien toque la paleta.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <vector>

#include "ui/theme.h"

using namespace pci::ui;

namespace {

// Luminancia relativa, tal y como la define WCAG 2.2.
double relativeLuminance(const char* hex) {
    const QColor color = theme::color(hex);
    const auto channel = [](int value) {
        const double c = value / 255.0;
        return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(color.red()) + 0.7152 * channel(color.green()) +
           0.0722 * channel(color.blue());
}

double contrast(const char* a, const char* b) {
    const double la = relativeLuminance(a);
    const double lb = relativeLuminance(b);
    const double hi = std::max(la, lb);
    const double lo = std::min(la, lb);
    return (hi + 0.05) / (lo + 0.05);
}

constexpr const char* kWhite = "#ffffff";

}  // namespace

TEST(Theme, EveryTextColourIsReadableOnTheWindowBackground) {
    struct Token {
        const char* name;
        const char* value;
    };
    const std::vector<Token> onWhite{
        {"ink (texto normal)", theme::kInk},
        {"inkMuted (secundario)", theme::kInkMuted},
        {"inkOff (apagado)", theme::kInkOff},
        {"bad (no cumple)", theme::kBad},
        {"warn (aviso)", theme::kWarn},
        {"good (cumple)", theme::kGood},
    };

    for (const auto& token : onWhite) {
        const double ratio = contrast(token.value, kWhite);
        std::printf("  [paleta] %-24s %s  %5.2f:1\n", token.name, token.value, ratio);
        // 4,5:1 es el mínimo de WCAG 2.2 para texto de cuerpo. Se exige a TODOS,
        // incluido el «apagado»: un texto apagado sigue teniendo que poder
        // leerse, o no está apagado, está escondido.
        EXPECT_GE(ratio, 4.5)
            << token.name << " (" << token.value
            << ") no llega al contraste mínimo sobre fondo claro: en una pantalla "
               "de taller con luz de nave eso no se lee";
    }
}

TEST(Theme, EachVerdictColourIsReadableOnItsOwnField) {
    // Un aviso se pinta con su color sobre su campo. Si ese par no contrasta, el
    // aviso más importante de la pantalla es el que peor se lee.
    struct Pair {
        const char* name;
        const char* ink;
        const char* field;
    };
    const std::vector<Pair> pairs{
        {"no cumple", theme::kBad, theme::kBadField},
        {"aviso", theme::kWarn, theme::kWarnField},
        {"cumple", theme::kGood, theme::kGoodField},
    };
    for (const auto& pair : pairs) {
        const double ratio = contrast(pair.ink, pair.field);
        std::printf("  [paleta] %-24s %s sobre %s  %5.2f:1\n", pair.name, pair.ink,
                    pair.field, ratio);
        EXPECT_GE(ratio, 4.5) << pair.name << ": el texto del aviso no contrasta con su "
                                             "propio fondo";
    }
}

TEST(Theme, TheVerdictColoursAreTellableApartFromEachOther) {
    // No basta con que cada uno se lea: «cumple» y «no cumple» tienen que
    // distinguirse ENTRE SÍ. Y como el rojo y el verde son justo el par que un
    // daltónico deutan confunde, se comprueba además que se separen por
    // LUMINANCIA — que es lo que sobrevive a cualquier daltonismo.
    const double bad = relativeLuminance(theme::kBad);
    const double good = relativeLuminance(theme::kGood);
    const double warn = relativeLuminance(theme::kWarn);
    std::printf("  [paleta] luminancias: no cumple %.3f, aviso %.3f, cumple %.3f\n", bad,
                warn, good);
    EXPECT_GT(contrast(theme::kBad, theme::kGood), 1.2)
        << "«cumple» y «no cumple» son casi el mismo tono: en pantalla, con "
           "daltonismo o con la luz de una nave, no se distinguen";
    EXPECT_GT(contrast(theme::kBad, theme::kWarn), 1.05);
}

TEST(Theme, TheNoticeStyleCarriesBothColoursAtOnce) {
    // El molde existe para que nadie vuelva a escribir «color:X; background:Y»
    // a mano, que es por donde se colaban las parejas sin contraste.
    const QString style = theme::noticeStyle(theme::kWarn, theme::kWarnField);
    std::printf("  [paleta] molde de aviso: %s\n", style.toStdString().c_str());
    EXPECT_TRUE(style.contains(QString(theme::kWarn)));
    EXPECT_TRUE(style.contains(QString(theme::kWarnField)));
    EXPECT_TRUE(style.contains(QStringLiteral("border")))
        << "sin borde, un campo de color claro se confunde con el fondo del panel";
}
