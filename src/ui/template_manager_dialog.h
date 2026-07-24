#pragma once

#include <QDialog>
#include <QString>

#include <cstdint>

class QListWidget;

namespace pci::repositories {
class ToolRepository;
}

namespace pci::ui {

// Gestor de plantillas de una pieza: listar, crear, renombrar, duplicar y
// eliminar plantillas, y elegir cuál activar. Opera directamente sobre la BD a
// través de ToolRepository; la ventana principal recarga y activa la elegida al
// cerrarse.
class TemplateManagerDialog : public QDialog {
    Q_OBJECT

public:
    TemplateManagerDialog(repositories::ToolRepository* repo, std::int64_t pieceId,
                          QString activeTemplate, QWidget* parent = nullptr);

    // Plantilla que el operador dejó seleccionada para activar (vacía si cerró
    // sin elegir). La ventana principal la usa para posicionar el combo.
    [[nodiscard]] QString selectedTemplate() const { return selected_; }

private:
    void reload(const QString& select = QString());
    [[nodiscard]] QString currentName() const;
    void onNew();
    void onRename();
    void onDuplicate();
    void onDelete();
    void onExport();
    void onImport();

    repositories::ToolRepository* repo_ = nullptr;
    std::int64_t pieceId_ = -1;
    QListWidget* list_ = nullptr;
    QString selected_;
};

}  // namespace pci::ui
