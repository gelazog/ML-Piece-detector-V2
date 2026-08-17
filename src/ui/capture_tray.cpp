#include "ui/capture_tray.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

#include <utility>

namespace pci::ui {

namespace {

// Un nombre de pieza puede traer barras, dos puntos o acentos, y de ahí sale un
// nombre de fichero que Windows rechaza. Se limpia en vez de fallar al guardar:
// el operador puso ese nombre por algo, y perder la captura por un carácter
// sería castigarle por escribir «Eje 3/4"».
QString safeName(const QString& raw) {
    static const QRegularExpression forbidden(QStringLiteral(R"([\\/:*?"<>|\s]+)"));
    QString clean = raw.simplified();
    clean.replace(forbidden, QStringLiteral("-"));
    while (clean.endsWith(QLatin1Char('-'))) {
        clean.chop(1);
    }
    return clean.isEmpty() ? QStringLiteral("pieza") : clean;
}

}  // namespace

void CaptureTray::add(QImage image, QString source, QDateTime taken) {
    if (image.isNull()) {
        return;  // una captura vacía no es una captura
    }
    captures_.push_back({std::move(image), std::move(taken), std::move(source)});
}

void CaptureTray::removeAt(int index) {
    if (index < 0 || index >= count()) {
        return;
    }
    captures_.erase(captures_.begin() + index);
}

void CaptureTray::clear() {
    captures_.clear();
}

QString CaptureTray::fileNameFor(const Capture& capture, const QString& piece, int index) {
    return QStringLiteral("%1_%2_%3.png")
        .arg(safeName(piece),
             capture.taken.toString(QStringLiteral("yyyyMMdd-HHmmss")),
             QStringLiteral("%1").arg(index + 1, 2, 10, QLatin1Char('0')));
}

core::Result<int> CaptureTray::saveAll(const QString& folder, const QString& piece) const {
    using ResultT = core::Result<int>;
    if (captures_.empty()) {
        return ResultT::ok(0);
    }
    QDir dir(folder);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        return ResultT::err("No se pudo crear la carpeta «" + folder.toStdString() + "»");
    }

    int written = 0;
    for (int i = 0; i < count(); ++i) {
        const Capture& capture = at(i);
        QString path = dir.filePath(fileNameFor(capture, piece, i));
        // No sobrescribir: perder una captura anterior por repetir un nombre es
        // el peor fallo posible aquí, porque no se nota hasta que se va a
        // buscar.
        int suffix = 2;
        while (QFileInfo::exists(path)) {
            const QFileInfo taken(path);
            path = dir.filePath(QStringLiteral("%1-%2.%3")
                                    .arg(QFileInfo(fileNameFor(capture, piece, i)).completeBaseName())
                                    .arg(suffix++)
                                    .arg(taken.suffix()));
        }
        if (!capture.image.save(path, "PNG")) {
            return ResultT::err("No se pudo escribir «" + path.toStdString() +
                                "». Se guardaron " + std::to_string(written) +
                                " de " + std::to_string(count()) + ".");
        }
        ++written;
    }
    return ResultT::ok(written);
}

}  // namespace pci::ui
