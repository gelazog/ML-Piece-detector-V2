#pragma once

#include <QString>

class QDialog;

namespace pci::repositories {
class SettingsRepository;
}

namespace pci::ui {

// Recuerda el tamaño que el operador le dio a un diálogo.
//
// Los nueve diálogos de la aplicación abrían siempre con un `resize()` fijo, y
// eso no es un valor por defecto sino una imposición: el que agranda el
// historial para ver las columnas lo agranda otra vez cada vez que lo abre. El
// tamaño de fábrica sigue estando —es lo que se usa la primera vez— y a partir
// de ahí manda el del operador.
//
// Se guarda por NOMBRE y no por clase, para que dos diálogos que compartan
// clase (o uno que cambie de clase) no se pisen el tamaño.
//
// Solo el tamaño, no la posición: un diálogo se centra sobre su ventana padre, y
// recordar dónde estaba lo abriría fuera de la pantalla en cuanto alguien
// mueva la aplicación o cambie de monitor. La ventana principal sí recuerda su
// posición, porque no tiene un padre sobre el que centrarse.
//
// `defaultWidth`/`defaultHeight` son el tamaño de la primera vez. Si no hay
// nada guardado —o lo guardado es absurdo, de una pantalla que ya no está— se
// usan ellos.
void restoreDialogSize(QDialog& dialog, repositories::SettingsRepository* settings,
                       const QString& name, int defaultWidth, int defaultHeight);

// Apunta el tamaño actual. Se llama al cerrarse el diálogo.
void rememberDialogSize(const QDialog& dialog, repositories::SettingsRepository* settings,
                        const QString& name);

// Las dos cosas de una vez: restaura ahora y se apunta para guardar al cerrar.
// Es la forma en que se usa siempre, y tenerla evita que alguien haga la mitad.
void keepDialogSize(QDialog& dialog, repositories::SettingsRepository* settings,
                    const QString& name, int defaultWidth, int defaultHeight);

}  // namespace pci::ui
