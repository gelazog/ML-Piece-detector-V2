#include "repositories/config_io.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QString>

namespace pci::repositories {

namespace {

// Marca de agua del archivo: sin esto, importar un JSON cualquiera dejaría la
// configuración a medio aplicar sin decir por qué.
constexpr const char* kAppTag = "pc-inspector";
constexpr int kConfigVersion = 1;

QJsonObject profileToJson(const DetectionProfile& profile) {
    QJsonObject object;
    object["name"] = QString::fromStdString(profile.name);
    object["manual_threshold"] = profile.options.manualThreshold;
    object["polarity"] = static_cast<int>(profile.options.polarity);
    object["blur_kernel"] = profile.options.blurKernel;
    object["morph_kernel"] = profile.options.morphKernel;
    return object;
}

}  // namespace

core::Result<ConfigSummary> exportConfig(const std::string& path, SettingsRepository& settings,
                                         DetectionProfileRepository& profiles) {
    using ResultT = core::Result<ConfigSummary>;

    auto entries = settings.listAll();
    if (!entries.isOk()) {
        return ResultT::err(entries.error().message);
    }
    auto stored = profiles.list();
    if (!stored.isOk()) {
        return ResultT::err(stored.error().message);
    }

    QJsonObject settingsObject;
    for (const auto& [key, value] : entries.value()) {
        settingsObject[QString::fromStdString(key)] = QString::fromStdString(value);
    }
    QJsonArray profilesArray;
    for (const auto& profile : stored.value()) {
        profilesArray.append(profileToJson(profile));
    }

    QJsonObject root;
    root["app"] = QString::fromLatin1(kAppTag);
    root["config_version"] = kConfigVersion;
    root["settings"] = settingsObject;
    root["detection_profiles"] = profilesArray;

    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return ResultT::err("No se pudo escribir '" + path + "': " +
                            file.errorString().toStdString());
    }
    // Indentado: el archivo se revisa a mano en la línea más de una vez.
    const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(json) != json.size()) {
        return ResultT::err("No se pudo escribir la configuración completa en '" + path + "'");
    }
    file.close();

    ConfigSummary summary;
    summary.settings = static_cast<int>(entries.value().size());
    summary.profiles = static_cast<int>(stored.value().size());
    return ResultT::ok(summary);
}

core::Result<ConfigSummary> importConfig(const std::string& path, SettingsRepository& settings,
                                         DetectionProfileRepository& profiles) {
    using ResultT = core::Result<ConfigSummary>;

    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly)) {
        return ResultT::err("No se pudo abrir '" + path + "': " +
                            file.errorString().toStdString());
    }
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    file.close();
    if (document.isNull() || !document.isObject()) {
        return ResultT::err("El archivo no es un JSON válido: " +
                            error.errorString().toStdString());
    }

    const QJsonObject root = document.object();
    if (root.value("app").toString() != QString::fromLatin1(kAppTag)) {
        return ResultT::err("El archivo no es una configuración de PC Inspector");
    }
    if (root.value("config_version").toInt(0) > kConfigVersion) {
        return ResultT::err("La configuración es de una versión más nueva de la aplicación");
    }

    ConfigSummary summary;
    const QJsonObject settingsObject = root.value("settings").toObject();
    for (auto it = settingsObject.constBegin(); it != settingsObject.constEnd(); ++it) {
        if (!it.value().isString()) {
            continue;  // clave corrupta: se salta, no se aborta el import entero
        }
        if (auto saved = settings.setString(it.key().toStdString(),
                                            it.value().toString().toStdString());
            saved.isOk()) {
            ++summary.settings;
        }
    }

    for (const QJsonValue& value : root.value("detection_profiles").toArray()) {
        const QJsonObject object = value.toObject();
        const QString name = object.value("name").toString();
        if (name.isEmpty()) {
            continue;
        }
        vision::SegmentationOptions options;
        options.manualThreshold = object.value("manual_threshold").toInt(-1);
        options.polarity = static_cast<vision::SegmentationPolarity>(
            object.value("polarity").toInt(0));
        options.blurKernel = object.value("blur_kernel").toInt(5);
        options.morphKernel = object.value("morph_kernel").toInt(5);
        if (auto saved = profiles.save(name.toStdString(), options); saved.isOk()) {
            ++summary.profiles;
        }
    }
    return ResultT::ok(summary);
}

}  // namespace pci::repositories
