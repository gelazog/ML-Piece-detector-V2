// EL AVISO SE APAGABA JUSTO CUANDO IBAS A COMPROBARLO.
//
// El calibrador de lente enseña una rejilla de zonas: verde la que ya tiene una
// toma, gris la que falta, y las cuatro ESQUINAS con un borde ámbar porque son
// las que no se pueden dejar sin cubrir —una calibración sin las esquinas no
// corrige la distorsión, que es justo para lo que se hace—.
//
// Ese borde ámbar era `kWarn` (#a15c00), pensado para fondo claro. Sobre la
// celda verde da **1,20:1**. Es decir: la esquina se marca mientras está sin
// cubrir y la marca DESAPARECE al cubrirla, que es cuando el operador repasa si
// están todas. Un aviso que se apaga al mirarlo no es un aviso.
//
// Y no era el único. Medido sobre los tres fondos de este diálogo:
//
//     texto de la celda cubierta   #ddd sobre #2e7d32   3,77:1   (WCAG pide 4,5)
//     borde de la celda            #555 sobre #3a3a3a   1,53:1   (pide 3,0)
//     borde de la vista previa     #444 sobre #1a1a1a   1,79:1   (pide 3,0)
//     marcador de esquina          kWarn sobre el verde 1,20:1   (pide 3,0)
//
// El de la vista previa es literalmente el mismo par que la cabecera de la
// paleta ya documenta haber arreglado en otro sitio —«el #444 sobre el fondo
// #1a1a1a da 1,8:1, o sea que casi no se ve»—, sobreviviendo aquí.
//
// Lo difícil del caso es que hay TRES fondos y el borde tiene que verse en los
// tres. Se barrieron candidatos y solo dos los pasan, los dos ya existentes:
//
//     borde     kOutline      6,87 / 3,76 / 10,51
//     esquina   kWarnOnDark   6,11 / 3,35 /  9,35
//
// Esta prueba los fija. No comprueba tokens sueltos —eso ya lo hace
// `test_theme.cpp`— sino cada pareja tal y como se usa AQUÍ: un token correcto
// sobre el fondo equivocado deja la pantalla igual de ilegible, y las cuatro de
// arriba eran exactamente eso.

#include <gtest/gtest.h>

#include <QColor>
#include <QString>

#include <cmath>
#include <cstdio>

#include "ui/theme.h"

namespace {

double channel(double v) {
    v /= 255.0;
    return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
}

double luminance(const char* hex) {
    const QColor c(QString::fromLatin1(hex));
    return 0.2126 * channel(c.red()) + 0.7152 * channel(c.green()) +
           0.0722 * channel(c.blue());
}

double contrast(const char* a, const char* b) {
    const double la = luminance(a);
    const double lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

// Los tres fondos que hay en este diálogo.
struct Surface {
    const char* what;
    const char* colour;
};

const Surface kSurfaces[] = {
    {"celda sin cubrir", pci::ui::theme::kChipRest},
    {"celda cubierta", pci::ui::theme::kGoodChip},
    {"vista previa", pci::ui::theme::kSurfaceDark},
};

}  // namespace

TEST(LensGridContrast, TheCornerMarkerIsVisibleOnACoveredCellToo) {
    // La que motivó todo: se marca la esquina porque no se puede dejar sin
    // cubrir, así que la marca tiene que seguir viéndose CUBIERTA.
    for (const auto& surface : kSurfaces) {
        const double ratio = contrast(pci::ui::theme::kWarnOnDark, surface.colour);
        std::printf("  [rejilla] marcador de esquina sobre %-18s %.2f:1\n", surface.what,
                    ratio);
        EXPECT_GE(ratio, 3.0)
            << "el marcador de esquina no se ve sobre " << surface.what
            << ". Si desaparece al cubrir la zona, el operador no puede repasar si están "
               "las cuatro — y una calibración sin esquinas no corrige la distorsión";
    }
}

TEST(LensGridContrast, EveryCellHasABorderYouCanSee) {
    // Un borde de control pide 3:1, y aquí es lo único que separa una zona de la
    // de al lado.
    for (const auto& surface : kSurfaces) {
        const double ratio = contrast(pci::ui::theme::kOutline, surface.colour);
        std::printf("  [rejilla] borde sobre %-28s %.2f:1\n", surface.what, ratio);
        EXPECT_GE(ratio, 3.0) << "el borde no se ve sobre " << surface.what;
    }
}

TEST(LensGridContrast, TheTextInsideACellCanBeRead) {
    struct Pair {
        const char* what;
        const char* ink;
        const char* field;
    };
    const Pair pairs[] = {
        {"celda sin cubrir", pci::ui::theme::kInkOnChipRest, pci::ui::theme::kChipRest},
        {"celda cubierta", pci::ui::theme::kInkOnChip, pci::ui::theme::kGoodChip},
    };
    for (const auto& pair : pairs) {
        const double ratio = contrast(pair.ink, pair.field);
        std::printf("  [rejilla] texto en %-30s %.2f:1\n", pair.what, ratio);
        EXPECT_GE(ratio, 4.5) << "el texto de la " << pair.what << " no llega a 4,5:1";
    }
}

TEST(LensGridContrast, TheColoursItReplacedReallyDidFail) {
    // Que esto no sea una prueba que pasa porque sí: los cuatro valores que
    // había tienen que SUSPENDER con la misma fórmula. Si pasaran, el motivo
    // para cambiarlos era falso y hay que volver a mirarlo.
    struct Old {
        const char* what;
        const char* a;
        const char* b;
        double needs;
    };
    const Old previously[] = {
        {"texto de la celda cubierta", "#dddddd", "#2e7d32", 4.5},
        {"borde de la celda", "#555555", "#3a3a3a", 3.0},
        {"borde de la vista previa", "#444444", "#1a1a1a", 3.0},
        {"marcador de esquina sobre el verde", pci::ui::theme::kWarn, "#2e7d32", 3.0},
    };
    for (const auto& old : previously) {
        const double ratio = contrast(old.a, old.b);
        std::printf("  [rejilla] antes: %-36s %.2f:1 (pedía %.1f)\n", old.what, ratio,
                    old.needs);
        EXPECT_LT(ratio, old.needs)
            << old.what << ": el valor que había SÍ pasaba, así que esta prueba no "
                           "comprueba lo que cree";
    }
}

TEST(LensGridContrast, ACoveredCellStillLooksDifferentFromAnEmptyOne) {
    // Y que el cambio no se haya llevado por delante lo que la rejilla dice de
    // un vistazo. Los dos estados se separan por LUMINANCIA y no solo por tono,
    // que es la regla que esta paleta lleva escrita desde que dos veredictos
    // salieron con el mismo gris.
    const double apart = contrast(pci::ui::theme::kGoodChip, pci::ui::theme::kChipRest);
    std::printf("  [rejilla] cubierta contra sin cubrir: %.2f:1\n", apart);
    EXPECT_GE(apart, 1.26)
        << "las dos clases de celda tienen casi la misma claridad: de un vistazo, o para "
           "un daltónico, la rejilla no dice nada";
}
