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
    Number, // any number (ImageZoom's zoom of 2.5)
    Action, // a button; clicking it calls Plugin::onSettingAction (no value)
    Color, // "#rrggbb", picked with a colour dialog
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
    std::optional<double> minimum; // Int and Number only
    std::optional<double> maximum; // Int and Number only
    bool multiline = false; // String only
    bool hidden = false; // internal state; no editor is generated
    // String only: an API key or session token. Kept in the system keychain
    // (SecretStore), never in the settings file, and edited as a password.
    bool secret = false;
    // The editor is shown only while setting `showWhenKey` has one of these values
    QString showWhenKey;
    QStringList showWhenValues;

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

// Where secret settings live: the system keychain in the app. Reads and
// writes are synchronous. Never log what passes through here.
class SecretStore
{
public:
    virtual ~SecretStore() = default;
    [[nodiscard]] virtual QString read(const QString &key) const = 0;
    virtual bool write(const QString &key, const QString &value) = 0;
    virtual bool remove(const QString &key) = 0;
};

// Keeps secrets for this run only (tests, the mock server)
class MemorySecretStore : public SecretStore
{
public:
    [[nodiscard]] QString read(const QString &key) const override { return data.value(key); }
    bool write(const QString &key, const QString &value) override
    {
        data.insert(key, value);
        return true;
    }
    bool remove(const QString &key) override
    {
        data.remove(key);
        return true;
    }
    QHash<QString, QString> data;
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
    [[nodiscard]] double numberValue(const QString &key) const { return value(key).toDouble(); }
    [[nodiscard]] QString stringValue(const QString &key) const { return value(key).toString(); }
    [[nodiscard]] QStringList stringListValue(const QString &key) const;

    // Validates, stores and persists. Returns an error message on failure.
    QString setValue(const QString &key, const QJsonValue &value);
    void reset(const QString &key);

    using ChangeCallback = std::function<void(const QString &key)>;
    void setChangeCallback(ChangeCallback callback) { onChange = std::move(callback); }

    void load();
    bool save() const;

    // The store for secret settings; a MemorySecretStore unless the app sets one
    static SecretStore *&secretStore();
    [[nodiscard]] QString secretKey(const QString &key) const;

private:
    QString id;
    SettingsSchema specs;
    Storage *storage;
    QJsonObject values;
    mutable QHash<QString, QString> secrets; // read once from the SecretStore
    ChangeCallback onChange;
};

} // namespace Plugins
} // namespace Qord
