// UN TEMA OSCURO DE VERDAD, Y QUE EL CLARO SIGA SIENDO EL DE SIEMPRE.
//
// La aplicación solo sabía ser clara: `applyApplicationLook` fijaba una paleta
// clara porque, sin ella, el modo oscuro de Windows dejaba el texto a 1,26:1
// (`test_dark_mode_palette.cpp`). Eso arregló lo ilegible pero quitó la
// opción; un taller con poca luz o un operador que pasa el turno delante de la
// pantalla puede querer el oscuro. Ahora se elige en Preferencias → Tema:
// Claro (por defecto), Oscuro o Como Windows.
//
// Lo que se mide aquí, y por qué cada cosa:
//
//   - Las DOS paletas, con `theme::contrastRatio`: texto a 4,5:1 y lo que
//     distingue el estado de un control a 3:1. Al medir la clara salieron dos
//     fallos que ya estaban: la fila elegida se separaba del fondo de la lista
//     1,62:1, y un ENLACE —que es texto— se leía a 1,62:1 sobre blanco. Los
//     dos venían de usar `kChipChosen` (#7fd6ff) como color de selección.
//   - Cada token de la ventana en SU tema. Un token con un solo valor no puede
//     servir en los dos fondos: el rojo de «no cumple» claro (#b3261e) sobre el
//     fondo oscuro da 2,3:1.
//   - Que los tokens sigan al tema activo sin tocar los sitios que los usan
//     (`textStyle(kWarn)` tiene que dar el ámbar oscuro en el tema oscuro).
//   - Que la elección se guarde en `pref_theme` y vuelva al abrir Configurar,
//     y que «Como Windows» se resuelva por la señal del sistema.
//   - Que el tema NO cambie a media sesión: las hojas de estilo ya están
//     escritas, y mezclar tintas de un tema con fondos del otro es el 1,05:1
//     del principio.

#include <gtest/gtest.h>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QRegularExpression>
#include <QTableWidget>
#include <QTemporaryDir>

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "database/db.h"
#include "database/schema.h"
#include "repositories/settings_repository.h"
#include "ui/app_repositories.h"
#include "ui/configure_dialog.h"
#include "ui/inspection_result_dialog.h"
#include "ui/main_window.h"
#include "ui/preferences_page.h"
#include "ui/theme.h"

using namespace pci::ui;

namespace {

QApplication& theApp() { return *qobject_cast<QApplication*>(QApplication::instance()); }

// Deja la aplicación como la encontró: la QApplication la comparten todas las
// pruebas del binario.
struct RestoreLook {
    QPalette palette = QApplication::palette();
    theme::Scheme scheme = theme::activeScheme();
    ~RestoreLook() {
        theme::applyApplicationLook(theApp(), scheme);
        QApplication::setPalette(palette);
    }
};

const char* name(theme::Scheme scheme) {
    return scheme == theme::Scheme::Dark ? "oscuro" : "claro";
}

}  // namespace

TEST(ThemeChoice, LightIsTheDefaultAndSystemFollowsWindows) {
    // Por defecto se queda lo que había: una base sin el ajuste, o con un
    // número que no se reconoce, arranca en claro.
    EXPECT_EQ(theme::themeChoiceFromSetting(0), theme::ThemeChoice::Light);
    EXPECT_EQ(theme::themeChoiceFromSetting(1), theme::ThemeChoice::Dark);
    EXPECT_EQ(theme::themeChoiceFromSetting(2), theme::ThemeChoice::System);
    EXPECT_EQ(theme::themeChoiceFromSetting(7), theme::ThemeChoice::Light);
    EXPECT_EQ(theme::themeChoiceFromSetting(-1), theme::ThemeChoice::Light);

    // Claro es claro aunque Windows esté en oscuro: ese es el arreglo que ya
    // vigila `test_dark_mode_palette.cpp`, y elegir tema no puede deshacerlo.
    EXPECT_EQ(theme::resolveScheme(theme::ThemeChoice::Light, Qt::ColorScheme::Dark),
              theme::Scheme::Light);
    EXPECT_EQ(theme::resolveScheme(theme::ThemeChoice::Dark, Qt::ColorScheme::Light),
              theme::Scheme::Dark);
    EXPECT_EQ(theme::resolveScheme(theme::ThemeChoice::System, Qt::ColorScheme::Dark),
              theme::Scheme::Dark);
    EXPECT_EQ(theme::resolveScheme(theme::ThemeChoice::System, Qt::ColorScheme::Light),
              theme::Scheme::Light);
    // Un sistema que no dice nada no es un sistema oscuro.
    EXPECT_EQ(theme::resolveScheme(theme::ThemeChoice::System, Qt::ColorScheme::Unknown),
              theme::Scheme::Light);
}

TEST(ThemeChoice, BothPalettesReachTheContrastWcagAsksFor) {
    for (const auto scheme : {theme::Scheme::Light, theme::Scheme::Dark}) {
        const QPalette p = theme::applicationPalette(scheme);
        struct Pair {
            const char* what;
            QColor ink;
            QColor ground;
        };
        const std::vector<Pair> texts{
            {"texto de ventana", p.color(QPalette::WindowText), p.color(QPalette::Window)},
            {"texto de campo", p.color(QPalette::Text), p.color(QPalette::Base)},
            {"texto de botón", p.color(QPalette::ButtonText), p.color(QPalette::Button)},
            {"ayuda emergente", p.color(QPalette::ToolTipText), p.color(QPalette::ToolTipBase)},
            {"texto seleccionado", p.color(QPalette::HighlightedText),
             p.color(QPalette::Highlight)},
            {"enlace", p.color(QPalette::Link), p.color(QPalette::Base)},
            {"texto de ejemplo", p.color(QPalette::PlaceholderText), p.color(QPalette::Base)},
            {"apagado en ventana", p.color(QPalette::Disabled, QPalette::WindowText),
             p.color(QPalette::Window)},
            {"apagado en campo", p.color(QPalette::Disabled, QPalette::Text),
             p.color(QPalette::Base)},
        };
        for (const auto& pair : texts) {
            const double ratio = theme::contrastRatio(pair.ink, pair.ground);
            std::printf("  [tema %s] %-20s %s sobre %s  %5.2f:1\n", name(scheme), pair.what,
                        qPrintable(pair.ink.name()), qPrintable(pair.ground.name()), ratio);
            EXPECT_GE(ratio, 4.5) << "tema " << name(scheme) << ": " << pair.what
                                  << " no llega al 4,5:1 que pide WCAG para texto";
        }
        // Lo que dice «esta fila está elegida» es el color de selección contra
        // el fondo que la rodea: es un componente, y pide 3:1.
        const std::vector<Pair> parts{
            {"selección / lista", p.color(QPalette::Highlight), p.color(QPalette::Base)},
            {"selección / ventana", p.color(QPalette::Highlight), p.color(QPalette::Window)},
        };
        for (const auto& pair : parts) {
            const double ratio = theme::contrastRatio(pair.ink, pair.ground);
            std::printf("  [tema %s] %-20s %s sobre %s  %5.2f:1\n", name(scheme), pair.what,
                        qPrintable(pair.ink.name()), qPrintable(pair.ground.name()), ratio);
            EXPECT_GE(ratio, 3.0) << "tema " << name(scheme) << ": " << pair.what
                                  << " no se distingue: la fila elegida parece una más";
        }
    }

    // Y que cada tema sea el que dice. El contraste es simétrico: una paleta
    // «clara» que en realidad fuera oscura pasaría todo lo de arriba.
    const double lightWindow =
        theme::relativeLuminance(theme::applicationPalette(theme::Scheme::Light)
                                     .color(QPalette::Window));
    const double darkWindow = theme::relativeLuminance(
        theme::applicationPalette(theme::Scheme::Dark).color(QPalette::Window));
    std::printf("  [tema] luminancia de ventana: claro %.3f, oscuro %.3f\n", lightWindow,
                darkWindow);
    EXPECT_GT(lightWindow, 0.5);
    EXPECT_LT(darkWindow, 0.05);
}

TEST(ThemeChoice, TheOldSelectionColourReallyFailed) {
    // Que la prueba de arriba no pase porque sí: la selección de antes,
    // `kChipChosen` sobre la lista blanca, tiene que SUSPENDER con la misma
    // cuenta. Si pasara, el cambio de color no habría arreglado nada.
    const double old = theme::contrastRatio(theme::color(theme::kChipChosen), QColor(Qt::white));
    std::printf("  [tema] selección de antes sobre blanco: %.2f:1\n", old);
    EXPECT_LT(old, 3.0);
    EXPECT_NEAR(old, 1.62, 0.01);
}

TEST(ThemeChoice, EveryWindowTokenReadsOnTheSurfacesOfItsOwnTheme) {
    struct Token {
        const char* what;
        theme::Adaptive token;
    };
    const std::vector<Token> inks{
        {"tinta", theme::kInk},          {"secundario", theme::kInkMuted},
        {"apagado", theme::kInkOff},     {"no cumple", theme::kBad},
        {"aviso", theme::kWarn},         {"cumple", theme::kGood},
    };
    for (const auto scheme : {theme::Scheme::Light, theme::Scheme::Dark}) {
        const QColor window = theme::color(theme::kSurfaceSunken.in(scheme));
        const QColor base = theme::color(theme::kSurfaceBase.in(scheme));
        for (const auto& one : inks) {
            const QColor ink = theme::color(one.token.in(scheme));
            const double onWindow = theme::contrastRatio(ink, window);
            const double onBase = theme::contrastRatio(ink, base);
            std::printf("  [tema %s] %-11s %s  ventana %5.2f:1  campo %5.2f:1\n", name(scheme),
                        one.what, one.token.in(scheme), onWindow, onBase);
            EXPECT_GE(onWindow, 4.5) << "tema " << name(scheme) << ": «" << one.what
                                     << "» no se lee sobre la ventana";
            EXPECT_GE(onBase, 4.5) << "tema " << name(scheme) << ": «" << one.what
                                   << "» no se lee en una tabla o un campo";
        }
        const struct {
            const char* what;
            theme::Adaptive ink;
            theme::Adaptive field;
        } notices[] = {
            {"no cumple", theme::kBad, theme::kBadField},
            {"aviso", theme::kWarn, theme::kWarnField},
            {"cumple", theme::kGood, theme::kGoodField},
        };
        for (const auto& notice : notices) {
            const double ratio = theme::contrastRatio(theme::color(notice.ink.in(scheme)),
                                                      theme::color(notice.field.in(scheme)));
            std::printf("  [tema %s] aviso «%s» sobre su campo %5.2f:1\n", name(scheme),
                        notice.what, ratio);
            EXPECT_GE(ratio, 4.5) << "tema " << name(scheme) << ": el aviso «" << notice.what
                                  << "» no contrasta con su propio fondo";
        }
        // Los tres veredictos se separan también por claridad, en los dos temas:
        // es lo que sobrevive a un daltonismo deutan.
        const QColor bad = theme::color(theme::kBad.in(scheme));
        EXPECT_GT(theme::contrastRatio(bad, theme::color(theme::kGood.in(scheme))), 1.2)
            << "tema " << name(scheme) << ": «cumple» y «no cumple» tienen la misma claridad";
        EXPECT_GT(theme::contrastRatio(bad, theme::color(theme::kWarn.in(scheme))), 1.05)
            << "tema " << name(scheme) << ": «no cumple» y «aviso» tienen la misma claridad";
    }

    // El fallo que esto evita, con su número: el rojo claro sobre el fondo
    // oscuro. Un solo valor por token no podía servir en los dos temas.
    const double lightRedOnDark = theme::contrastRatio(
        theme::color(theme::kBad.in(theme::Scheme::Light)),
        theme::color(theme::kSurfaceSunken.in(theme::Scheme::Dark)));
    std::printf("  [tema] el rojo claro sobre la ventana oscura: %.2f:1\n", lightRedOnDark);
    EXPECT_LT(lightRedOnDark, 4.5);
}

TEST(ThemeChoice, TheTokensFollowTheActiveThemeWithoutTouchingTheirCallers) {
    RestoreLook restore;
    theme::applyApplicationLook(theApp(), theme::Scheme::Dark);
    EXPECT_EQ(theme::activeScheme(), theme::Scheme::Dark);
    // Por el mismo camino que usan las pantallas: la conversión a texto.
    EXPECT_EQ(QString(theme::kInk), QString(theme::kInk.dark));
    EXPECT_TRUE(theme::textStyle(theme::kWarn).contains(QString(theme::kWarn.dark)))
        << "una hoja de estilo escrita en el tema oscuro lleva la tinta del claro";
    EXPECT_EQ(QApplication::palette().color(QPalette::Window),
              theme::color(theme::kSurfaceSunken.dark));

    // Y las que no son de la ventana NO cambian: la muestra del color de la
    // mesa es clara por sí misma y necesita tinta oscura en los dos temas.
    EXPECT_STREQ(theme::kInkOnLight, "#141414");

    theme::applyApplicationLook(theApp(), theme::Scheme::Light);
    EXPECT_EQ(QString(theme::kInk), QString(theme::kInk.light));
    // Sin decir el tema, el claro: es el que la aplicación ha tenido siempre y
    // del que dependen `test_dark_mode_palette.cpp` y `test_theme.cpp`.
    theme::applyApplicationLook(theApp());
    EXPECT_EQ(theme::activeScheme(), theme::Scheme::Light);
}

TEST(ThemeChoice, TheControlFramesCanBeSeenInBothThemes) {
    // El marco de un campo, un botón o un desplegable lo dibuja Fusion, no un
    // token: por eso se mide PINTADO. Es lo que dice «aquí se escribe» o «esto
    // se pulsa», y WCAG pide 3:1 para eso. Fusion lo saca de oscurecer la
    // ventana un 40 %, y medido así daba 2,01:1 en claro y 1,15:1 en oscuro.
    RestoreLook restore;
    for (const auto scheme : {theme::Scheme::Light, theme::Scheme::Dark}) {
        theme::applyApplicationLook(theApp(), scheme);
        const struct {
            const char* what;
            std::function<QWidget*(QWidget*)> make;
        } controls[] = {
            {"campo", [](QWidget* parent) -> QWidget* { return new QLineEdit(parent); }},
            {"botón", [](QWidget* parent) -> QWidget* {
                 return new QPushButton(QStringLiteral("Aceptar"), parent);
             }},
            {"número", [](QWidget* parent) -> QWidget* { return new QSpinBox(parent); }},
            {"desplegable", [](QWidget* parent) -> QWidget* {
                 auto* combo = new QComboBox(parent);
                 combo->addItem(QStringLiteral("Claro"));
                 return combo;
             }},
        };
        for (const auto& control : controls) {
            QWidget holder;
            holder.setAutoFillBackground(true);
            holder.resize(200, 60);
            QWidget* widget = control.make(&holder);
            widget->setGeometry(20, 15, 160, 30);
            QImage shot(holder.size(), QImage::Format_RGB32);
            holder.render(&shot);
            const QColor window = shot.pixelColor(5, 5);
            // El más contrastado de los píxeles del borde superior, por la
            // mitad izquierda (la derecha de un número o un desplegable lleva
            // flechas que aprobarían solas).
            double best = 1.0;
            QColor frame;
            for (int y = 14; y <= 18; ++y) {
                const QColor c = shot.pixelColor(60, y);
                const double ratio = theme::contrastRatio(c, window);
                if (ratio > best) {
                    best = ratio;
                    frame = c;
                }
            }
            std::printf("  [tema %s] marco del %-12s %s sobre %s  %5.2f:1\n", name(scheme),
                        control.what, qPrintable(frame.name()), qPrintable(window.name()), best);
            EXPECT_GE(best, 3.0) << "tema " << name(scheme) << ": el marco del "
                                 << control.what << " no se distingue de la ventana";
        }
    }
}

TEST(ThemeChoice, ThePreferencesPageOffersTheThreeAndGivesBackTheChoice) {
    PreferencesPage page(700, 3.0);
    auto* combo = page.findChild<QComboBox*>(QStringLiteral("themeCombo"));
    ASSERT_NE(combo, nullptr) << "no está el selector de tema en Preferencias";
    ASSERT_EQ(combo->count(), 3);
    EXPECT_EQ(combo->itemText(0), QStringLiteral("Claro"));
    EXPECT_EQ(combo->itemText(1), QStringLiteral("Oscuro"));
    EXPECT_EQ(combo->itemText(2), QStringLiteral("Como Windows"));
    EXPECT_EQ(page.themeChoice(), theme::ThemeChoice::Light) << "por defecto tiene que ser Claro";
    EXPECT_FALSE(combo->toolTip().isEmpty());

    for (const auto choice : {theme::ThemeChoice::Dark, theme::ThemeChoice::System,
                              theme::ThemeChoice::Light}) {
        page.setThemeChoice(choice);
        EXPECT_EQ(page.themeChoice(), choice);
    }
}

TEST(ThemeChoice, TheChoiceIsSavedAndComesBackButDoesNotRepaintTheSession) {
    RestoreLook restore;
    theme::applyApplicationLook(theApp(), theme::Scheme::Light);

    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    auto opened = pci::database::Db::open(
        QDir(dir.path()).filePath(QStringLiteral("tema.db")).toStdString());
    ASSERT_TRUE(opened.isOk()) << opened.error().message;
    auto db = std::move(opened.value());
    ASSERT_TRUE(pci::database::migrate(*db).isOk());
    pci::repositories::SettingsRepository settings(*db);
    // Guardado de una sesión anterior: «Como Windows».
    ASSERT_TRUE(settings.setInt("pref_theme", 2).isOk());

    AppRepositories repos;
    repos.settings = &settings;
    MainWindow window(repos);
    auto* configure = window.findChild<QAction*>(QStringLiteral("configureAction"));
    ASSERT_NE(configure, nullptr);
    configure->trigger();
    auto* dialog = window.findChild<ConfigureDialog*>();
    ASSERT_NE(dialog, nullptr);
    ASSERT_NE(dialog->preferencesPage(), nullptr);
    EXPECT_EQ(dialog->preferencesPage()->themeChoice(), theme::ThemeChoice::System)
        << "Configurar no enseña el tema que estaba guardado";

    dialog->preferencesPage()->setThemeChoice(theme::ThemeChoice::Dark);
    emit dialog->applied();
    EXPECT_EQ(settings.getInt("pref_theme", -1).valueOr(-1), 1)
        << "el tema elegido no queda guardado para el próximo arranque";
    // Y la sesión sigue en el tema con el que arrancó.
    EXPECT_EQ(theme::activeScheme(), theme::Scheme::Light)
        << "el tema cambió a media sesión: las hojas de estilo ya escritas se quedan con "
           "la tinta del tema viejo sobre el fondo del nuevo";
    EXPECT_EQ(QApplication::palette().color(QPalette::Window),
              theme::color(theme::kSurfaceSunken.light));
}

// EL INFORME DE INSPECCIÓN CREÍA IR SOBRE FONDO OSCURO, Y NO.
//
// Su tabla y sus avisos usaban los tokens de fondo oscuro (`kGoodOnDark`,
// `kBadOnDark`, `kWarnOnDark`) con un comentario que lo justificaba: «esta
// tabla va sobre fondo OSCURO». Lo fue mientras el modo oscuro de Windows se
// colaba en la aplicación. Desde que la paleta es fija, la tabla es blanca, y
// el «OK» quedaba a 1,6:1 sobre ella; el aviso de posición, a 2,6:1.
//
// Se mide el diálogo construido de verdad, en los dos temas, leyendo el color
// que acaba puesto en cada sitio contra el fondo que tiene debajo.
TEST(ThemeChoice, TheInspectionReportReadsInBothThemes) {
    RestoreLook restore;
    static const QRegularExpression ink(QStringLiteral("color\\s*:\\s*(#[0-9a-fA-F]{6})"));
    for (const auto scheme : {theme::Scheme::Light, theme::Scheme::Dark}) {
        theme::applyApplicationLook(theApp(), scheme);
        pci::engine::InspectionEngine::Outcome outcome;
        outcome.verdict.ok = false;
        outcome.verdict.position.evaluated = true;
        outcome.verdict.position.radiusEvaluated = true;
        outcome.verdict.position.radius = 9.0;
        outcome.verdict.position.maxRadius = 4.0;
        outcome.verdict.position.ok = false;
        outcome.verdict.position.note = "giro no evaluado";
        outcome.persistError = "disco lleno";
        for (const bool ok : {true, false}) {
            pci::inspection::ToolRunResult result;
            result.name = ok ? "bien" : "mal";
            result.ok = ok;
            outcome.toolResults.push_back(result);
        }
        QImage frame(320, 240, QImage::Format_RGB888);
        frame.fill(QColor(30, 30, 30));
        InspectionResultDialog dialog(frame, outcome, nullptr, 1);

        const QPalette palette = QApplication::palette();
        auto* table = dialog.findChild<QTableWidget*>();
        ASSERT_NE(table, nullptr);
        for (int row = 0; row < table->rowCount(); ++row) {
            const QColor state = table->item(row, 2)->foreground().color();
            const double ratio = theme::contrastRatio(state, palette.color(QPalette::Base));
            std::printf("  [informe %s] estado «%s» %s  %5.2f:1\n", name(scheme),
                        qPrintable(table->item(row, 2)->text()), qPrintable(state.name()),
                        ratio);
            EXPECT_GE(ratio, 4.5) << "tema " << name(scheme)
                                  << ": el OK/NG de la tabla no se lee sobre su fondo";
        }
        int styled = 0;
        for (auto* label : dialog.findChildren<QLabel*>()) {
            const auto match = ink.match(label->styleSheet());
            // Las pastillas y los huecos llevan su propio fondo: se miden en
            // `test_theme.cpp`. Aquí solo el texto que va sobre la ventana.
            if (!match.hasMatch() || label->styleSheet().contains(QStringLiteral("background"))) {
                continue;
            }
            ++styled;
            const double ratio = theme::contrastRatio(QColor(match.captured(1)),
                                                      palette.color(QPalette::Window));
            std::printf("  [informe %s] «%s» %s  %5.2f:1\n", name(scheme),
                        qPrintable(label->text().left(30)), qPrintable(match.captured(1)),
                        ratio);
            EXPECT_GE(ratio, 4.5) << "tema " << name(scheme) << ": «"
                                  << label->text().toStdString()
                                  << "» no se lee sobre la ventana";
        }
        EXPECT_GE(styled, 2) << "no se ha medido ningún aviso: la prueba no comprueba nada";
    }
}
