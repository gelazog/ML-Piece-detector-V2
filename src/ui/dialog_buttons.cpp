#include "ui/dialog_buttons.h"

#include <QDialogButtonBox>
#include <QObject>
#include <QPushButton>

namespace pci::ui {

namespace {

struct ButtonText {
    QDialogButtonBox::StandardButton role;
    const char* label;
    const char* help;
};

// LOS ACELERADORES VAN AQUÍ Y NO EN CADA DIÁLOGO.
//
// Es el mismo motivo por el que los textos están aquí: seis diálogos usan
// estos botones, y una letra elegida seis veces son seis ocasiones de que
// dos se pisen. Dos mnemónicos iguales no dan un error, dan un CICLO — Qt va
// saltando entre los candidatos en vez de activar—, y eso desde fuera se vive
// como «a veces hace otra cosa».
//
// «A&plicar» lleva la P y no la A porque la A es de «Aceptar», que es el que
// se pulsa siempre. Las dos juntas son además la duda clásica de cualquier
// ventana de ajustes, y darles la misma tecla la empeoraría.
const ButtonText kTexts[] = {
    {QDialogButtonBox::Ok, "&Aceptar",
     "Guarda los cambios y cierra la ventana."},
    {QDialogButtonBox::Apply, "A&plicar",
     "Guarda los cambios y DEJA LA VENTANA ABIERTA, para seguir probando.\n\n"
     "Es la diferencia con Aceptar, que guarda y cierra."},
    {QDialogButtonBox::Cancel, "&Cancelar",
     "Cierra sin guardar. Lo que hubieras cambiado se descarta."},
    {QDialogButtonBox::Close, "C&errar",
     "Cierra la ventana. Lo que ya hayas aplicado se queda aplicado."},
    {QDialogButtonBox::RestoreDefaults, "Valores de &fábrica",
     "Devuelve a los valores de fábrica lo que se ve en esta pestaña.\n\n"
     "No toca las demás pestañas ni las piezas registradas."},
    {QDialogButtonBox::Save, "&Guardar", "Guarda los cambios."},
    {QDialogButtonBox::Discard, "&Descartar", "Tira los cambios sin guardarlos."},
    {QDialogButtonBox::Yes, "&Sí", "Confirma y sigue adelante."},
    {QDialogButtonBox::No, "No", "No sigue adelante."},
    {QDialogButtonBox::Reset, "Restablecer",
     "Vuelve a poner los valores que había al abrir la ventana."},
    {QDialogButtonBox::Help, "Ayuda", "Explica qué hace esta ventana."},
};

}  // namespace

void nameButtonsInSpanish(QDialogButtonBox* buttons) {
    if (buttons == nullptr) {
        return;
    }
    for (const auto& text : kTexts) {
        auto* button = buttons->button(text.role);
        if (button == nullptr) {
            continue;
        }
        button->setText(QObject::tr(text.label));
        // La ayuda solo si nadie ha puesto una: quien construye el diálogo sabe
        // más de su propio botón que esta tabla, y pisársela sería cambiarle el
        // significado a un botón por el camino.
        if (button->toolTip().trimmed().isEmpty()) {
            button->setToolTip(QObject::tr(text.help));
        }
    }
}

}  // namespace pci::ui
