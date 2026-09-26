#include "ui/source_files.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace pci::ui {

namespace {

QString patterns(const QStringList& extensions) {
    QStringList globs;
    for (const auto& extension : extensions) {
        globs << QStringLiteral("*.") + extension;
    }
    return globs.join(QLatin1Char(' '));
}

QString normalized(const QString& path) {
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

bool samePath(const QString& a, const QString& b) {
#ifdef Q_OS_WIN
    constexpr Qt::CaseSensitivity kCase = Qt::CaseInsensitive;
#else
    constexpr Qt::CaseSensitivity kCase = Qt::CaseSensitive;
#endif
    return normalized(a).compare(normalized(b), kCase) == 0;
}

QString tr(const char* text) { return QCoreApplication::translate("SourceFiles", text); }

}  // namespace

const QStringList& imageExtensions() {
    static const QStringList kImages{QStringLiteral("png"), QStringLiteral("jpg"),
                                     QStringLiteral("jpeg"), QStringLiteral("bmp"),
                                     QStringLiteral("tif"), QStringLiteral("tiff")};
    return kImages;
}

const QStringList& videoExtensions() {
    static const QStringList kVideos{QStringLiteral("mp4"), QStringLiteral("avi"),
                                     QStringLiteral("mkv"), QStringLiteral("mov"),
                                     QStringLiteral("wmv")};
    return kVideos;
}

std::optional<camera::SourceKind> sourceKindForFile(const QString& path) {
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix.isEmpty()) {
        return std::nullopt;
    }
    if (imageExtensions().contains(suffix)) {
        return camera::SourceKind::Image;
    }
    if (videoExtensions().contains(suffix)) {
        return camera::SourceKind::Video;
    }
    return std::nullopt;
}

QString imageFileFilter() {
    return tr("Imágenes (%1);;Todos (*)").arg(patterns(imageExtensions()));
}

QString videoFileFilter() {
    return tr("Vídeos (%1);;Todos (*)").arg(patterns(videoExtensions()));
}

QString imageOrVideoFileFilter() {
    return tr("Imágenes y vídeos (%1 %2);;Imágenes (%1);;Vídeos (%2)")
        .arg(patterns(imageExtensions()), patterns(videoExtensions()));
}

QString describeExtensions(const QStringList& extensions) {
    return extensions.join(QStringLiteral(", ")).toUpper();
}

QStringList withRecentFile(const QStringList& recent, const QString& path, int max) {
    QStringList result{QDir::toNativeSeparators(normalized(path))};
    for (const auto& one : recent) {
        if (result.size() >= max) {
            break;
        }
        if (!samePath(one, path)) {
            result << one;
        }
    }
    return result;
}

QStringList withoutRecentFile(const QStringList& recent, const QString& path) {
    QStringList result;
    for (const auto& one : recent) {
        if (!samePath(one, path)) {
            result << one;
        }
    }
    return result;
}

QString encodeRecentFiles(const QStringList& recent) {
    return recent.join(QLatin1Char('\n'));
}

QStringList decodeRecentFiles(const QString& text) {
    QStringList result;
    for (const auto& line : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty() && result.size() < kMaxRecentFiles) {
            result << trimmed;
        }
    }
    return result;
}

}  // namespace pci::ui
