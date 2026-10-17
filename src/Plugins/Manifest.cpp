#include "Manifest.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace Qord {
namespace Plugins {

namespace {
bool readStringList(const QJsonObject &obj, const char *key, QStringList &out, QString &error)
{
    const QJsonValue v = obj.value(QLatin1String(key));
    if (v.isUndefined()) {
        error = QStringLiteral("missing field \"%1\"").arg(QLatin1String(key));
        return false;
    }
    if (!v.isArray()) {
        error = QStringLiteral("\"%1\" must be an array").arg(QLatin1String(key));
        return false;
    }
    for (const auto &item : v.toArray()) {
        if (!item.isString()) {
            error = QStringLiteral("\"%1\" must only contain strings").arg(QLatin1String(key));
            return false;
        }
        out.append(item.toString());
    }
    return true;
}

bool readString(const QJsonObject &obj, const char *key, QString &out, QString &error, bool required = true)
{
    const QJsonValue v = obj.value(QLatin1String(key));
    if (v.isUndefined() && !required)
        return true;
    if (!v.isString()) {
        error = QStringLiteral("\"%1\" must be a string").arg(QLatin1String(key));
        return false;
    }
    out = v.toString();
    return true;
}

bool parseSetting(const QJsonValue &value, SettingSpec &spec, QString &error)
{
    if (!value.isObject()) {
        error = QStringLiteral("settings entries must be objects");
        return false;
    }
    const QJsonObject obj = value.toObject();
    if (!readString(obj, "key", spec.key, error) || !readString(obj, "label", spec.label, error) ||
        !readString(obj, "description", spec.description, error, false))
        return false;

    QString typeName;
    if (!readString(obj, "type", typeName, error))
        return false;
    auto type = settingTypeFromString(typeName);
    if (!type) {
        error = QStringLiteral("setting %1 has unknown type %2").arg(spec.key, typeName);
        return false;
    }
    spec.type = *type;

    if (spec.type == SettingType::Choice) {
        for (const auto &c : obj.value(QLatin1String("choices")).toArray()) {
            const QJsonObject co = c.toObject();
            if (!co.value(QLatin1String("value")).isString()) {
                error = QStringLiteral("choice values of %1 must be strings").arg(spec.key);
                return false;
            }
            spec.choices.append({ co.value(QLatin1String("value")).toString(),
                                  co.value(QLatin1String("label")).toString() });
        }
    }
    if (obj.contains(QLatin1String("min")))
        spec.minimum = obj.value(QLatin1String("min")).toDouble();
    if (obj.contains(QLatin1String("max")))
        spec.maximum = obj.value(QLatin1String("max")).toDouble();
    spec.multiline = obj.value(QLatin1String("multiline")).toBool();
    spec.emojiPicker = obj.value(QLatin1String("emojiPicker")).toBool();
    spec.hidden = obj.value(QLatin1String("hidden")).toBool();
    spec.secret = obj.value(QLatin1String("secret")).toBool();
    if (obj.contains(QLatin1String("showWhen"))) {
        const QJsonObject when = obj.value(QLatin1String("showWhen")).toObject();
        spec.showWhenKey = when.value(QLatin1String("key")).toString();
        for (const QJsonValue &v : when.value(QLatin1String("values")).toArray())
            spec.showWhenValues.append(v.toVariant().toString());
        if (spec.showWhenKey.isEmpty() || spec.showWhenValues.isEmpty()) {
            error = QStringLiteral("showWhen of %1 needs a key and values").arg(spec.key);
            return false;
        }
    }
    if (spec.emojiPicker && (spec.type != SettingType::String || spec.secret)) {
        error = QStringLiteral("emojiPicker setting %1 must be a string").arg(spec.key);
        return false;
    }
    if (spec.secret && (spec.type != SettingType::String || spec.multiline)) {
        error = QStringLiteral("secret setting %1 must be a one-line string").arg(spec.key);
        return false;
    }

    if (!obj.contains(QLatin1String("default"))) {
        error = QStringLiteral("setting %1 has no default").arg(spec.key);
        return false;
    }
    spec.defaultValue = obj.value(QLatin1String("default"));
    const QString defaultError = spec.validate(spec.defaultValue);
    if (!defaultError.isEmpty()) {
        error = QStringLiteral("default of setting %1 is invalid: %2").arg(spec.key, defaultError);
        return false;
    }
    return true;
}
} // namespace

bool PluginManifest::parse(const QByteArray &json, PluginManifest &out, QString &error)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        error = parseError.errorString();
        return false;
    }
    if (!doc.isObject()) {
        error = QStringLiteral("manifest must be a JSON object");
        return false;
    }
    const QJsonObject obj = doc.object();
    PluginManifest m;

    if (!readString(obj, "id", m.id, error) || !readString(obj, "equicordName", m.equicordName, error) ||
        !readString(obj, "equicordPath", m.equicordPath, error) || !readString(obj, "name", m.name, error) ||
        !readString(obj, "description", m.description, error) || !readString(obj, "target", m.target, error) ||
        !readString(obj, "notes", m.notes, error, false) ||
        !readString(obj, "settingsNote", m.settingsNote, error, false))
        return false;

    static const QRegularExpression idPattern(QStringLiteral("^[a-z0-9][a-z0-9_]*$"));
    if (!idPattern.match(m.id).hasMatch()) {
        error = QStringLiteral("invalid plugin id \"%1\"").arg(m.id);
        return false;
    }

    QStringList permissionNames, hookNames;
    if (!readStringList(obj, "categories", m.categories, error) || !readStringList(obj, "tags", m.tags, error) ||
        !readStringList(obj, "authors", m.authors, error) ||
        !readStringList(obj, "permissions", permissionNames, error) ||
        !readStringList(obj, "hooks", hookNames, error) ||
        !readStringList(obj, "dependencies", m.dependencies, error))
        return false;
    if (obj.contains(QLatin1String("gaps")) && !readStringList(obj, "gaps", m.gaps, error))
        return false;
    if (obj.contains(QLatin1String("contributors")) && !readStringList(obj, "contributors", m.contributors, error))
        return false;
    if (obj.contains(QLatin1String("notApplicable")) &&
        !readStringList(obj, "notApplicable", m.notApplicable, error))
        return false;
    if (m.authors.isEmpty()) {
        error = QStringLiteral("authors must credit the original plugin authors");
        return false;
    }

    for (const auto &name : permissionNames) {
        auto p = permissionFromString(name);
        if (!p) {
            error = QStringLiteral("unknown permission \"%1\"").arg(name);
            return false;
        }
        m.permissions |= *p;
    }
    for (const auto &name : hookNames) {
        auto h = hookFromString(name);
        if (!h) {
            error = QStringLiteral("unknown hook \"%1\"").arg(name);
            return false;
        }
        m.hooks |= *h;
        if (!m.permissions.testFlag(requiredPermission(*h))) {
            error = QStringLiteral("hook %1 needs permission %2, which is not declared")
                            .arg(name, permissionToString(requiredPermission(*h)));
            return false;
        }
    }

    const QJsonValue enabled = obj.value(QLatin1String("enabledByDefault"));
    if (!enabled.isBool()) {
        error = QStringLiteral("\"enabledByDefault\" must be true or false");
        return false;
    }
    m.enabledByDefault = enabled.toBool();

    const QJsonValue settings = obj.value(QLatin1String("settings"));
    if (!settings.isArray()) {
        error = QStringLiteral("\"settings\" must be an array");
        return false;
    }
    QStringList keys;
    for (const auto &s : settings.toArray()) {
        SettingSpec spec;
        if (!parseSetting(s, spec, error))
            return false;
        if (keys.contains(spec.key)) {
            error = QStringLiteral("duplicate setting key %1").arg(spec.key);
            return false;
        }
        keys.append(spec.key);
        m.settings.append(spec);
    }

    const QJsonValue commands = obj.value(QLatin1String("commands"));
    if (!commands.isArray()) {
        error = QStringLiteral("\"commands\" must be an array");
        return false;
    }
    for (const auto &c : commands.toArray()) {
        const QJsonObject co = c.toObject();
        m.commands.append({ co.value(QLatin1String("name")).toString(),
                            co.value(QLatin1String("description")).toString() });
    }

    out = std::move(m);
    return true;
}

} // namespace Plugins
} // namespace Qord
