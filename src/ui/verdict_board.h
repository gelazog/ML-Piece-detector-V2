#pragma once

#include <QFrame>
#include <QString>

class QLabel;

namespace pci::ui {

// EL VEREDICTO, LEGIBLE A METRO Y MEDIO.
//
// Quien lo lee es el operador de pie delante de la máquina, a 1–1,5 m de la
// pantalla y con la siguiente pieza en la mano. Hasta ahora el OK/NG salía en
// una banda de 16 px durante la auto-inspección, o en la línea de veredicto del
// panel de medidas —un dock que puede estar cerrado—, o en la barra de estado,
// que se borra sola. A esa distancia 16 px no se leen: el mínimo razonable para
// texto a 1–1,5 m ronda los 20 px, y la palabra que decide si la pieza vale
// tiene que verse antes de enfocar la vista.
//
// Por eso son dos líneas: la PALABRA, grande, y debajo el MOTIVO en una sola
// línea («Ø interior: se pasa 0.15mm»). El color de fondo ayuda, pero no es lo
// que distingue los estados: cada uno lleva su palabra y su símbolo, porque un
// daltónico deutan no separa ese verde de ese rojo (WCAG 1.4.1).
//
// Los colores salen de las pastillas de veredicto de `theme.h`, que llevan su
// propio fondo: el tablero se lee igual sobre la ventana clara que sobre un
// panel oscuro, porque su contraste no depende de lo que tenga alrededor.
enum class VerdictState {
    Hidden,   // no hay nada que juzgar: sin herramientas, sin fuente
    Working,  // auto-inspección en marcha, todavía sin resultado
    Good,     // OK
    Bad,      // NG
    NoPiece,  // hay herramientas, pero no hay pieza delante
    Failed,   // se intentó medir y no se pudo: no es un NG
};

// Lo que se pinta para cada estado: fondo, tinta y palabra. Público para que la
// prueba mida el contraste de exactamente lo que se pinta.
struct VerdictLook {
    const char* field = nullptr;
    const char* ink = nullptr;
    QString word;
};
[[nodiscard]] VerdictLook verdictLook(VerdictState state);

class VerdictBoard : public QFrame {
    Q_OBJECT

public:
    explicit VerdictBoard(QWidget* parent = nullptr);

    // Pone el estado y su motivo. El motivo va en UNA línea: si no cabe se
    // corta, y el texto entero queda en el tooltip y en el nombre accesible.
    // Llamarlo con lo mismo que ya enseña no toca nada: se llama por fotograma.
    void showVerdict(VerdictState state, const QString& reason = {});

    [[nodiscard]] VerdictState state() const { return state_; }
    [[nodiscard]] QString word() const;
    [[nodiscard]] QString reason() const { return reason_; }

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void layoutReason();

    QLabel* word_ = nullptr;
    QLabel* reasonLabel_ = nullptr;
    VerdictState state_ = VerdictState::Hidden;
    QString reason_;
};

}  // namespace pci::ui
