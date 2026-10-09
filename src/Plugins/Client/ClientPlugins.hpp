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

class SessionRelationshipDirectory : public RelationshipDirectory
{
public:
    explicit SessionRelationshipDirectory(Acheron::Core::Session *session)
        : session(session)
    {
    }
    bool isBlocked(Id accountId, Id userId) const override;
    bool isIgnored(Id accountId, Id userId) const override;

private:
    Acheron::Core::Session *session;
};

class QtUiService : public UiService
{
public:
    void showTextDialog(const QString &title, const QList<TextSection> &sections) override;
    void openUrl(const QUrl &url) override;
    void insertIntoMessageBox(const QString &text) override;
    void pickTimestamp(std::function<void(const QString &)> done) override;
    // Set by the main window, which owns the message box
    static std::function<void(const QString &)> &messageBoxInserter();
    void openPluginSettings(const QString &pluginId) override;
    void promptText(const QString &title, const QString &label, const QString &initial, int maxLength,
                    std::function<void(const QString &)> done) override;
    static std::function<void(const QString &)> &pluginSettingsOpener();
    void openImage(const QUrl &url) override;
    // Set by the main window, which owns the image viewer's parent
    static std::function<void(const QUrl &)> &imageOpener();
};

class QtDisplayOptions : public DisplayOptionsService
{
public:
    void setRelativeTimeRoundsDown(bool floor) override;
    void setMobileIndicatorForAnyStatus(bool any) override;
    PluginHost *host = nullptr;
};

// Marks servers read through the main window, which knows the server list
class QtReadStates : public ReadStateService
{
public:
    void markAllServersRead(Id accountId) override;
    static std::function<void(Id accountId)> &allServersMarker();
};

class SessionMessageActions : public MessageActionService
{
public:
    explicit SessionMessageActions(Acheron::Core::Session *session)
        : session(session)
    {
    }
    void setMessageFlags(Id accountId, Id channelId, Id messageId, quint64 flags) override;

private:
    Acheron::Core::Session *session;
};

class SessionMessageService : public MessageService
{
public:
    explicit SessionMessageService(Acheron::Core::Session *session)
        : session(session)
    {
    }
    void refreshMessage(Id accountId, Id channelId, Id messageId) override;
    void removeMessage(Id accountId, Id channelId, Id messageId) override;
    void refreshFilters() override;
    PluginHost *host = nullptr;

private:
    Acheron::Core::Session *session;
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
    SessionRelationshipDirectory relationships;
    QtUiService ui;
    SessionMessageService messages;
    SessionMessageActions messageActions;
    QtDisplayOptions displayOptions;
    QtReadStates readStates;
    std::unique_ptr<FileStorage> storage;
    std::unique_ptr<PluginHost> pluginHost;
};

} // namespace Plugins
} // namespace Qord
