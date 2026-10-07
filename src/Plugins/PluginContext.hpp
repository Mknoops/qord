#pragma once

#include "Permission.hpp"
#include "Services.hpp"
#include "Settings.hpp"

#include <QSet>
#include <QString>

namespace Qord {
namespace Plugins {

class ScopedNetwork;

// What a running plugin can reach. Services the plugin hasn't been granted
// are null. Everything obtained here is released by the host when the
// plugin stops (outstanding network requests are cancelled).
class PluginContext
{
public:
    PluginContext(QString pluginId, PluginSettings *settings, Permissions granted, const Services &services,
                  QString dataDirectory);
    ~PluginContext();

    PluginContext(const PluginContext &) = delete;
    PluginContext &operator=(const PluginContext &) = delete;

    [[nodiscard]] const QString &pluginId() const { return id; }
    [[nodiscard]] PluginSettings &settings() { return *pluginSettings; }
    [[nodiscard]] bool isGranted(Permission permission) const { return granted.testFlag(permission); }

    // Null without the network permission.
    [[nodiscard]] NetworkService *network();
    // Null without the read_guild_members permission.
    [[nodiscard]] const MemberDirectory *members() const;

    // Private writable directory for caches (created on demand).
    [[nodiscard]] QString dataDirectory() const;

    void logInfo(const QString &message) const;
    void logWarning(const QString &message) const;

private:
    QString id;
    PluginSettings *pluginSettings;
    Permissions granted;
    Services services;
    QString dataDir;
    std::unique_ptr<ScopedNetwork> scopedNetwork;
};

} // namespace Plugins
} // namespace Qord
