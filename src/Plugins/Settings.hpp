#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

namespace Qord {
namespace Plugins {

enum class SettingType {
    Bool,
    Int,
    String,
    Choice,
    StringList,
    Json, // structured value edited by a plugin-provided widget
};

std::optional<SettingType> settingTypeFromString(const QString &name);
QString settingTypeToString(SettingType type);

struct SettingChoice
{
    QString value;
    QString label;
};

struct SettingSpec
{
    QString key;
    SettingType type = SettingType::Bool;
    QJsonValue defaultValue;
    QString label;
    QString description;
    QList<SettingChoice> choices; // Choice only
    std::optional<qint64> minimum; // Int only
    std::optional<qint64> maximum; // Int only
    bool multiline = false; // String only
    bool hidden = false; // internal state; no editor is generated

    // Returns an error message, or an empty string if `value` is acceptable.
    [[nodiscard]] QString validate(const QJsonValue &value) const;
};

using SettingsSchema = QList<SettingSpec>;

// Where plugin state lives. FileStorage in the app, MemoryStorage in tests.
class Storage
{
public:
    virtual ~Storage() = default;
    [[nodiscard]] virtual QByteArray read(const QString &key) const = 0;
    virtual bool write(const QString &key, const QByteArray &data) = 0;
};

class MemoryStorage : public Storage
{
public:
    [[nodiscard]] QByteArray read(const QString &key) const override { return data.value(key); }
    bool write(const QString &key, const QByteArray &bytes) override
    {
        data.insert(key, bytes);
        return true;
    }
    QHash<QString, QByteArray> data;
};

// Stores each key as <dir>/<key>.json
class FileStorage : public Storage
{
public:
    explicit FileStorage(QString directory);
    [[nodiscard]] QByteArray read(const QString &key) const override;
    bool write(const QString &key, const QByteArray &data) override;
    [[nodiscard]] QString directory() const { return dir; }

private:
    QString dir;
};

// Typed, schema-validated settings of one plugin, persisted as JSON.
class PluginSettings
{
public:
    PluginSettings(QString pluginId, SettingsSchema schema, Storage *storage);

    [[nodiscard]] const SettingsSchema &schema() const { return specs; }
    [[nodiscard]] const SettingSpec *spec(const QString &key) const;

    // Stored value if valid, otherwise the schema default.
    [[nodiscard]] QJsonValue value(const QString &key) const;
    [[nodiscard]] bool boolValue(const QString &key) const { return value(key).toBool(); }
    [[nodiscard]] qint64 intValue(const QString &key) const { return qint64(value(key).toDouble()); }
    [[nodiscard]] QString stringValue(const QString &key) const { return value(key).toString(); }
    [[nodiscard]] QStringList stringListValue(const QString &key) const;

    // Validates, stores and persists. Returns an error message on failure.
    QString setValue(const QString &key, const QJsonValue &value);
    void reset(const QString &key);

    using ChangeCallback = std::function<void(const QString &key)>;
    void setChangeCallback(ChangeCallback callback) { onChange = std::move(callback); }

    void load();
    bool save() const;

private:
    QString id;
    SettingsSchema specs;
    Storage *storage;
    QJsonObject values;
    ChangeCallback onChange;
};

} // namespace Plugins
} // namespace Qord
