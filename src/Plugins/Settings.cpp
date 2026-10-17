#include "Settings.hpp"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QSaveFile>

#include <cmath>
#include <limits>

Q_LOGGING_CATEGORY(LogPluginSettings, "qord.plugins.settings")

namespace Qord {
namespace Plugins {

std::optional<SettingType> settingTypeFromString(const QString &name)
{
    if (name == QLatin1String("bool"))
        return SettingType::Bool;
    if (name == QLatin1String("int"))
        return SettingType::Int;
    if (name == QLatin1String("string"))
        return SettingType::String;
    if (name == QLatin1String("choice"))
        return SettingType::Choice;
    if (name == QLatin1String("stringlist"))
        return SettingType::StringList;
    if (name == QLatin1String("json"))
        return SettingType::Json;
    if (name == QLatin1String("number"))
        return SettingType::Number;
    if (name == QLatin1String("action"))
        return SettingType::Action;
    if (name == QLatin1String("color"))
        return SettingType::Color;
    return std::nullopt;
}

QString settingTypeToString(SettingType type)
{
    switch (type) {
    case SettingType::Bool:
        return QStringLiteral("bool");
    case SettingType::Int:
        return QStringLiteral("int");
    case SettingType::String:
        return QStringLiteral("string");
    case SettingType::Choice:
        return QStringLiteral("choice");
    case SettingType::StringList:
        return QStringLiteral("stringlist");
    case SettingType::Json:
        return QStringLiteral("json");
    case SettingType::Number:
        return QStringLiteral("number");
    case SettingType::Action:
        return QStringLiteral("action");
    case SettingType::Color:
        return QStringLiteral("color");
    }
    return {};
}

QString SettingSpec::validate(const QJsonValue &value) const
{
    switch (type) {
    case SettingType::Bool:
        return value.isBool() ? QString() : QStringLiteral("expected true or false");
    case SettingType::Int: {
        if (!value.isDouble())
            return QStringLiteral("expected a number");
        const double d = value.toDouble();
        // Validate before converting: floating-to-integer conversion outside
        // qint64's range is undefined. Its upper endpoint rounds to 2^63 as
        // a double, so use an exclusive upper bound.
        const double lower = double((std::numeric_limits<qint64>::min)());
        if (!std::isfinite(d) || d < lower || d >= -lower || std::trunc(d) != d)
            return QStringLiteral("expected a whole number");
        if (minimum && d < *minimum)
            return QStringLiteral("must be at least %1").arg(*minimum);
        if (maximum && d > *maximum)
            return QStringLiteral("must be at most %1").arg(*maximum);
        return {};
    }
    case SettingType::Number: {
        if (!value.isDouble())
            return QStringLiteral("expected a number");
        const double d = value.toDouble();
        if (minimum && d < *minimum)
            return QStringLiteral("must be at least %1").arg(*minimum);
        if (maximum && d > *maximum)
            return QStringLiteral("must be at most %1").arg(*maximum);
        return {};
    }
    case SettingType::String:
        return value.isString() ? QString() : QStringLiteral("expected text");
    case SettingType::Color: {
        static const QRegularExpression hex(QStringLiteral("^#[0-9a-fA-F]{6}$"));
        return value.isString() && hex.match(value.toString()).hasMatch() ? QString()
                                                                         : QStringLiteral("expected a colour like #5865f2");
    }
    case SettingType::Choice:
        if (!value.isString())
            return QStringLiteral("expected one of the choices");
        for (const auto &choice : choices)
            if (choice.value == value.toString())
                return {};
        return QStringLiteral("unknown choice %1").arg(value.toString());
    case SettingType::StringList:
        if (!value.isArray())
            return QStringLiteral("expected a list");
        for (const auto &item : value.toArray())
            if (!item.isString())
                return QStringLiteral("expected a list of text");
        return {};
    case SettingType::Json:
        // Structure is the plugin's business; it must tolerate bad input
        return value.isUndefined() ? QStringLiteral("missing value") : QString();
    case SettingType::Action:
        return {}; // a button has no value
    }
    return QStringLiteral("unknown setting type");
}

FileStorage::FileStorage(QString directory)
    : dir(std::move(directory))
{
}

QByteArray FileStorage::read(const QString &key) const
{
    QFile file(QDir(dir).filePath(key + QStringLiteral(".json")));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

bool FileStorage::write(const QString &key, const QByteArray &data)
{
    if (!QDir().mkpath(dir))
        return false;
    QSaveFile file(QDir(dir).filePath(key + QStringLiteral(".json")));
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(data);
    return file.commit();
}

PluginSettings::PluginSettings(QString pluginId, SettingsSchema schema, Storage *storage)
    : id(std::move(pluginId)), specs(std::move(schema)), storage(storage)
{
}

const SettingSpec *PluginSettings::spec(const QString &key) const
{
    for (const auto &s : specs)
        if (s.key == key)
            return &s;
    return nullptr;
}

SecretStore *&PluginSettings::secretStore()
{
    static MemorySecretStore memory;
    static SecretStore *store = &memory;
    return store;
}

QString PluginSettings::secretKey(const QString &key) const
{
    return QStringLiteral("plugin_%1_%2").arg(id, key);
}

QJsonValue PluginSettings::value(const QString &key) const
{
    const SettingSpec *s = spec(key);
    if (!s)
        return {};
    if (s->secret) {
        auto cached = secrets.constFind(key);
        if (cached == secrets.constEnd())
            cached = secrets.insert(key, secretStore() ? secretStore()->read(secretKey(key)) : QString());
        return cached->isEmpty() ? s->defaultValue : QJsonValue(*cached);
    }
    auto it = values.constFind(key);
    if (it != values.constEnd() && s->validate(*it).isEmpty())
        return *it;
    return s->defaultValue;
}

QStringList PluginSettings::stringListValue(const QString &key) const
{
    QStringList out;
    for (const auto &v : value(key).toArray())
        out.append(v.toString());
    return out;
}

QString PluginSettings::setValue(const QString &key, const QJsonValue &newValue)
{
    const SettingSpec *s = spec(key);
    if (!s)
        return QStringLiteral("unknown setting %1").arg(key);
    QString error = s->validate(newValue);
    if (!error.isEmpty())
        return error;
    if (s->secret) {
        const QString text = newValue.toString();
        if (value(key).toString() == text)
            return {};
        SecretStore *store = secretStore();
        if (!store || !(text.isEmpty() ? store->remove(secretKey(key)) : store->write(secretKey(key), text)))
            return QStringLiteral("the system keychain could not store %1").arg(s->label);
        secrets.insert(key, text);
        if (onChange)
            onChange(key);
        return {};
    }
    if (values.value(key) == newValue)
        return {};
    values.insert(key, newValue);
    save();
    if (onChange)
        onChange(key);
    return {};
}

void PluginSettings::reset(const QString &key)
{
    if (const SettingSpec *s = spec(key); s && s->secret) {
        setValue(key, QString());
        return;
    }
    if (!values.contains(key))
        return;
    values.remove(key);
    save();
    if (onChange)
        onChange(key);
}

void PluginSettings::load()
{
    values = {};
    secrets.clear();
    if (!storage)
        return;
    const QByteArray data = storage->read(QStringLiteral("settings-") + id);
    if (data.isEmpty())
        return;
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qCWarning(LogPluginSettings) << "Ignoring unreadable settings for plugin" << id << err.errorString();
        return;
    }
    // Keep only known keys; invalid values fall back to defaults in value()
    const QJsonObject stored = doc.object();
    for (const auto &s : specs)
        if (stored.contains(s.key) && !s.secret)
            values.insert(s.key, stored.value(s.key));
}

bool PluginSettings::save() const
{
    if (!storage)
        return true;
    return storage->write(QStringLiteral("settings-") + id, QJsonDocument(values).toJson(QJsonDocument::Indented));
}

} // namespace Plugins
} // namespace Qord
