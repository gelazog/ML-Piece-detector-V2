// EN EL VÍDEO, EL VEREDICTO SE LEE — NO SOLO SE COLOREA.
//
// Es la pantalla que el operador mira mientras trabaja, y hasta ahora que una
// cota cumpliera o no lo decía ÚNICAMENTE el color de la letra. Tres razones por
// las que eso no vale, y las tres están medidas:
//
//   - Un daltónico deutan o protán —uno de cada doce hombres— no distingue ese
//     verde de ese rojo.
//   - Sobre mesa blanca, que es el montaje industrial normal, la caja de fondo
//     al 67 % dejaba el rojo de «no cumple» en 2,21:1 de contraste, por debajo
//     incluso del 3:1 que pide un simple indicador. El estado que hay que ver
//     era el que MENOS se veía.
//   - En un parte impreso en blanco y negro el color desaparece entero.
//
// El proyecto ya escribía «OK» y «NG» en la tabla del informe y ponía glifos en
// la tira de estación: esto no inventa un criterio, aplica el que ya había.

#include <gtest/gtest.h>

#include <QApplication>

#include <cstdio>

#include "inspection_editor/canvas/editor_canvas.h"

using namespace pci;

namespace {

inspection::ToolRunResult reading(const char* name, bool ok, double measured) {
    inspection::ToolRunResult result;
    result.name = name;
    result.ok = ok;
    result.measured = measured;
    return result;
}

}  // namespace

TEST(OverlayVerdict, TheLabelSaysWhetherItPassesInWords) {
    inspection::EditorCanvas canvas;

    const QString good = canvas.overlayLabel(reading("ancho", true, 100.0));
    const QString bad = canvas.overlayLabel(reading("alto", false, 140.0));
    std::printf("  [vídeo] cumple: «%s»\n", good.toStdString().c_str());
    std::printf("  [vídeo] no cumple: «%s»\n", bad.toStdString().c_str());

    EXPECT_TRUE(good.contains(QStringLiteral("OK")))
        << "una cota que cumple no lo dice con palabras: el color es lo único que "
           "lo cuenta, y hay quien no lo ve";
    EXPECT_TRUE(bad.contains(QStringLiteral("NG")))
        << "una cota que NO cumple no lo dice con palabras. Es el estado que más "
           "importa ver y el que peor contraste tenía";
    EXPECT_NE(good, bad);
}

TEST(OverlayVerdict, TheLabelStillCarriesTheNameAndTheMeasure) {
    // El veredicto se AÑADE; no puede comerse lo que ya había, o para saber qué
    // cota es habría que ir a buscarla a otro sitio.
    inspection::EditorCanvas canvas;
    const QString label = canvas.overlayLabel(reading("Ø exterior", true, 42.0));
    std::printf("  [vídeo] etiqueta completa: «%s»\n", label.toStdString().c_str());
    EXPECT_TRUE(label.contains(QStringLiteral("Ø exterior")))
        << "la etiqueta ya no dice de qué cota habla";
    EXPECT_TRUE(label.contains(QStringLiteral("42")))
        << "la etiqueta ya no dice cuánto mide";
}

TEST(OverlayVerdict, TheTwoVerdictsAreTellableApartWithoutColour) {
    // La comprobación de verdad: quitar el color y ver si algo distingue los dos
    // casos. Si las dos cadenas fuesen iguales, el color sería otra vez lo único.
    inspection::EditorCanvas canvas;
    const QString good = canvas.overlayLabel(reading("cota", true, 50.0));
    const QString bad = canvas.overlayLabel(reading("cota", false, 50.0));
    EXPECT_NE(good, bad)
        << "con el mismo nombre y la misma medida, cumplir y no cumplir escriben "
           "exactamente lo mismo: en blanco y negro son indistinguibles";
}
