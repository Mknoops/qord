#pragma once

// Client glue: the real services behind PluginContext and the app-wide
// PluginHost. Compiled into the client only (depends on Acheron's Core).

#include "Plugins/PluginHost.hpp"
#include "Plugins/Services.hpp"
#include "Plugins/SettingsWidgets.hpp"

#include <QHash>
#include <QObject>
#include <QPointer>

#include <memory>

class QNetworkAccessManager;
class QNetworkReply;

namespace Acheron {
namespace Core {
class Session;
}
} // namespace Acheron

namespace Qord {
namespace Plugins {

// HTTPS GETs for plugins with the network permission. The network manager
// is created on first use, so nothing is allocated unless a plugin needs it.
class QtNetworkService : public QObject, public NetworkService
{
    Q_OBJECT
public:
    static constexpr qint64 MaxResponseBytes = 16 * 1024 * 1024;

    explicit QtNetworkService(QObject *parent = nullptr);
    ~QtNetworkService() override;

    Handle get(const QUrl &url, Callback callback) override;
    void cancel(Handle handle) override;

private:
    QNetworkAccessManager *nam = nullptr;
    QHash<Handle, QPointer<QNetworkReply>> replies;
    Handle next = 0;
};

class SessionMemberDirectory : public MemberDirectory
{
public:
    explicit SessionMemberDirectory(Acheron::Core::Session *session)
        : session(session)
    {
    }
    std::optional<QList<Id>> memberRoles(Id accountId, Id guildId, Id userId) const override;

private:
    Acheron::Core::Session *session;
};

class QtClipboardService : public ClipboardService
{
public:
    void setText(const QString &text) override;
};

// Owns the plugin host for the lifetime of the session and publishes it as
// PluginHost::instance() for client call sites.
class ClientPlugins
{
public:
    explicit ClientPlugins(Acheron::Core::Session *session);
    ~ClientPlugins();

    ClientPlugins(const ClientPlugins &) = delete;
    ClientPlugins &operator=(const ClientPlugins &) = delete;

    [[nodiscard]] PluginHost *host() const { return pluginHost.get(); }
    // Custom settings widgets of the built-in plugins
    static const SettingsWidgetRegistry &settingsWidgets();

private:
    QtNetworkService network;
    SessionMemberDirectory members;
    QtClipboardService clipboard;
    std::unique_ptr<FileStorage> storage;
    std::unique_ptr<PluginHost> pluginHost;
};

} // namespace Plugins
} // namespace Qord
