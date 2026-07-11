#pragma once

#include <QDialog>
#include <QKeySequence>
#include <QString>

#include <vector>

class QAction;
class QTableWidget;

namespace pci::repositories {
class SettingsRepository;
}

namespace pci::ui {

// Un atajo configurable: la acción viva, su identificador persistente y su
// valor por defecto (para "Restaurar").
struct ShortcutSpec {
    QString id;           // clave en Settings: "key_<id>"
    QString description;  // texto de la guía
    QKeySequence defaultKey;
    QAction* action = nullptr;
};

// Guía de atajos editable: lista cada comando con su tecla actual; el usuario
// puede cambiarla (QKeySequenceEdit), restaurar los valores por defecto y
// guardar — los cambios aplican al instante y persisten en la BD.
class ShortcutsDialog : public QDialog {
    Q_OBJECT

public:
    ShortcutsDialog(std::vector<ShortcutSpec>* shortcuts,
                    repositories::SettingsRepository* settings, QWidget* parent = nullptr);

private slots:
    void onRestoreDefaults();
    void onSave();

private:
    std::vector<ShortcutSpec>* shortcuts_;
    repositories::SettingsRepository* settings_;
    QTableWidget* table_ = nullptr;
};

}  // namespace pci::ui
