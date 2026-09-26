// EL MODO OSCURO DE WINDOWS ROMPÍA EL TEXTO.
//
// `src/main.cpp` no fijaba estilo ni paleta. El Qt instalado (≥6.7) trae
// `qmodernwindowsstyle`, que SIGUE el tema del sistema: con Windows en oscuro
// la ventana se pintaba con fondo oscuro -el `kSurfaceDark` de `ui/theme.h`,
// #1a1a1a, es justo el tono que la aplicación ya usa para sus propias
// superficies oscuras-, pero los tokens pensados para fondo CLARO, como
// `kInk` (#141414, medido a 18,42:1 sobre BLANCO), seguían pintándose
// encima sin cambiar.
//
// Medido: `kInk` (#141414) sobre `kSurfaceDark` (#1a1a1a) da 1,05:1. El
// mínimo de WCAG 2.2 para texto de cuerpo es 4,5:1. El texto de casi toda la
// aplicación quedaba prácticamente invisible en cuanto el operador tuviera
// el modo oscuro activado en Windows -algo que no controla la aplicación ni
// lo pide.
//
// LA PRUEBA NO PUEDE CAMBIAR EL TEMA DE WINDOWS DE VERDAD (no hay pantalla en
// el banco, y bajo la plataforma `offscreen` de Qt que usan las pruebas,
// `QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark)` no
// llega a tocar la paleta de la aplicación por sí solo -se comprobó: sin
// forzar nada más, la paleta por defecto ya sale clara, y una prueba que
// solo pusiera esa señal habría dado el mismo verde con o sin el arreglo).
//
// Así que se sigue la alternativa que la propia tarea deja escrita: «forzando
// una paleta oscura del sistema antes de aplicar la nuestra». Aquí se
// construye a mano la paleta rota tal cual la describe el fallo -fondo
// `kSurfaceDark`, texto `kInk` sin cambiar- y se comprueba que
// `theme::applyApplicationLook` la reemplaza por una clara. Sin la llamada a
// esa función, la paleta rota se queda tal cual y la prueba falla de verdad:
// se comprobó apagando la llamada antes de escribir este comentario.
//
// UNA TRAMPA A EVITAR: el contraste es SIMÉTRICO. Una paleta con fondo oscuro
// y texto claro también puede pasar `contraste >= 4,5:1` sin ser la paleta
// CLARA que pide la tarea. Por eso, además del contraste, se comprueba por
// separado que el fondo tiene luminancia alta y el texto luminancia baja.

#include <gtest/gtest.h>

#include <QApplication>
#include <QGuiApplication>
#include <QPalette>
#include <QStyleHints>

#include <cstdio>

#include "ui/theme.h"

using namespace pci::ui;

namespace {

// El binario de pruebas de interfaz arranca una sola QApplication en su
// `main` (ver `test_canvas_gestures.cpp`); aquí se recupera esa instancia en
// vez de crear una segunda, que Qt no permite.
QApplication& theApp() {
    auto* app = qobject_cast<QApplication*>(QApplication::instance());
    return *app;
}

}  // namespace

TEST(DarkModePalette, StaysLightEvenWhenWindowsIsDark) {
    QApplication& app = theApp();
    ASSERT_NE(QApplication::instance(), nullptr);

    // Se guarda para devolver el banco al estado en que lo encontró: esta
    // QApplication la comparten TODAS las pruebas del binario.
    const QPalette originalPalette = app.palette();
    const Qt::ColorScheme originalScheme = QGuiApplication::styleHints()->colorScheme();

    // La señal que Windows manda cuando está en modo oscuro.
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);

    // EL FALLO, MONTADO A MANO: la ventana en el fondo oscuro que ya usa la
    // aplicación para sus superficies, con el texto en el ink de fondo
    // claro sin tocar -que es justo lo que pasaba sin `applyApplicationLook`.
    QPalette broken;
    broken.setColor(QPalette::Window, theme::color(theme::kSurfaceDark));
    broken.setColor(QPalette::WindowText, theme::color(theme::kInk));
    broken.setColor(QPalette::Base, theme::color(theme::kSurfaceDark));
    broken.setColor(QPalette::Text, theme::color(theme::kInk));
    app.setPalette(broken);

    theme::applyApplicationLook(app);
    const QPalette applied = app.palette();

    const QColor windowBg = applied.color(QPalette::Window);
    const QColor windowText = applied.color(QPalette::WindowText);
    const QColor baseBg = applied.color(QPalette::Base);
    const QColor text = applied.color(QPalette::Text);

    const double windowContrast = theme::contrastRatio(windowText, windowBg);
    const double baseContrast = theme::contrastRatio(text, baseBg);
    std::printf(
        "  [paleta] tras el arreglo, con fondo de sistema oscuro forzado antes: ventana "
        "%.2f:1, texto sobre base %.2f:1\n",
        windowContrast, baseContrast);

    EXPECT_GE(windowContrast, 4.5)
        << "el texto de la ventana no llega al contraste mínimo aunque el sistema forzara "
           "una paleta oscura antes: es justo lo que rompía antes de fijar la paleta";
    EXPECT_GE(baseContrast, 4.5)
        << "el texto sobre fondos de control (listas, campos) no llega al mínimo";

    // LA TRAMPA: comprobar que además es la paleta CLARA, no una oscura que
    // también contrastara consigo misma.
    const double windowLuminance = theme::relativeLuminance(windowBg);
    const double textLuminance = theme::relativeLuminance(windowText);
    std::printf("  [paleta] luminancia fondo=%.3f texto=%.3f\n", windowLuminance, textLuminance);
    EXPECT_GT(windowLuminance, 0.5)
        << "el fondo de la ventana no es claro: una paleta oscura con texto claro también "
           "pasaría la cuenta de contraste de arriba, y NO es lo que pide la tarea";
    EXPECT_LT(textLuminance, 0.3)
        << "el texto no es oscuro: ver el comentario de arriba sobre la simetría del "
           "contraste";

    // Se devuelve el banco tal como estaba para no arrastrar estado a otras
    // pruebas de este mismo binario.
    app.setPalette(originalPalette);
    QGuiApplication::styleHints()->setColorScheme(originalScheme);
}
