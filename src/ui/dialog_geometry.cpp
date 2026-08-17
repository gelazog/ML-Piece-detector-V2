#include "ui/dialog_geometry.h"

#include <QDialog>
#include <QGuiApplication>
#include <QScreen>

#include <algorithm>

#include "repositories/settings_repository.h"

namespace pci::ui {

namespace {

std::string widthKey(const QString& name) {
    return ("dlg_" + name + "_w").toStdString();
}

std::string heightKey(const QString& name) {
    return ("dlg_" + name + "_h").toStdString();
}

// Un tamaño guardado puede haber quedado inservible: la sesión anterior corría
// en un monitor de 4K y hoy la máquina de línea tiene uno de 1366×768, y el
// diálogo abriría más grande que la pantalla, con los botones fuera. Se acota a
// lo que hay ahora, y por abajo a algo que se pueda leer.
QSize sane(QSize size) {
    constexpr int kMinimum = 240;
    QSize limit(4096, 4096);
    if (const QScreen* screen = QGuiApplication::primaryScreen(); screen != nullptr) {
        limit = screen->availableGeometry().size();
    }
    return {std::clamp(size.width(), kMinimum, limit.width()),
            std::clamp(size.height(), kMinimum, limit.height())};
}

}  // namespace

void restoreDialogSize(QDialog& dialog, repositories::SettingsRepository* settings,
                       const QString& name, int defaultWidth, int defaultHeight) {
    QSize size(defaultWidth, defaultHeight);
    if (settings != nullptr) {
        const auto width = settings->getInt(widthKey(name), 0);
        const auto height = settings->getInt(heightKey(name), 0);
        if (width.isOk() && height.isOk() && width.value() > 0 && height.value() > 0) {
            size = QSize(width.value(), height.value());
        }
    }
    dialog.resize(sane(size));
}

void rememberDialogSize(const QDialog& dialog, repositories::SettingsRepository* settings,
                        const QString& name) {
    if (settings == nullptr) {
        return;
    }
    settings->setInt(widthKey(name), dialog.width());
    settings->setInt(heightKey(name), dialog.height());
}

void keepDialogSize(QDialog& dialog, repositories::SettingsRepository* settings,
                    const QString& name, int defaultWidth, int defaultHeight) {
    restoreDialogSize(dialog, settings, name, defaultWidth, defaultHeight);
    // Al `finished` y no al destructor: en el destructor de QDialog el widget ya
    // está a medio desmontar y su tamaño no tiene por qué seguir siendo el que
    // el operador veía.
    QObject::connect(&dialog, &QDialog::finished, &dialog,
                     [&dialog, settings, name](int) {
                         rememberDialogSize(dialog, settings, name);
                     });
}

}  // namespace pci::ui
