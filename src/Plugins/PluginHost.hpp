#pragma once

#include "Manifest.hpp"
#include "Plugin.hpp"
#include "PluginContext.hpp"
#include "Registry.hpp"
#include "Services.hpp"
#include "Settings.hpp"

#include <QObject>

#include <array>
#include <map>
#include <memory>
#include <optional>

namespace Qord {
namespace Plugins {

// Owns every plugin: manifests, enabled state, granted permissions,
// settings, running instances, and the hook tables used for dispatch.
//
// Dispatch is cheap by construction: a hook table only holds plugins that
// are enabled, started, implement the hook, and have its permission granted.
// Call sites check hasHook() first, so with no such plugin a hook costs one
// branch.
class PluginHost : public QObject
{
    Q_OBJECT
public:
    struct PluginInfo
    {
        PluginManifest manifest;
        QString loadError; // non-empty if the manifest or plugin is broken
        bool enabled = false;
        bool running = false;
        Permissions granted;
    };

    // `stateStorage` keeps enabled/granted state and settings. `dataDirectory`
    // is the parent of each plugin's private cache directory.
    PluginHost(Registry registry, Storage *stateStorage, Services services, QString dataDirectory,
               QObject *parent = nullptr);
    ~PluginHost() override;

    // The app-wide host used by client call sites; may be null (tests, or
    // plugins compiled out), in which case the static helpers are no-ops.
    static PluginHost *instance();
    static void setInstance(PluginHost *host);

    // Reads manifests and persisted state, then starts enabled plugins.
    void load();
    // Stops every running plugin (called on shutdown).
    void unload();

    [[nodiscard]] QStringList pluginIds() const;
    [[nodiscard]] const PluginInfo *info(const QString &id) const;

    // Enabling also enables the plugin's dependencies; disabling also
    // disables plugins that depend on it. Returns false with `error` set if
    // the plugin (or a dependency) can't be started.
    bool setEnabled(const QString &id, bool enabled, QString *error = nullptr);
    [[nodiscard]] bool isEnabled(const QString &id) const;
    [[nodiscard]] bool isRunning(const QString &id) const;

    // Grants are limited to what the manifest declares. A running plugin is
    // restarted so its context reflects the new grants.
    void setGranted(const QString &id, Permissions permissions);
    [[nodiscard]] Permissions granted(const QString &id) const;

    // Settings exist whether or not the plugin is enabled, so they can be
    // edited before enabling. Created on first access.
    PluginSettings *settings(const QString &id);

    [[nodiscard]] bool hasHook(Hook hook) const { return !tables[hookIndex(hook)].empty(); }
    [[nodiscard]] QStringList pluginsForHook(Hook hook) const;

    // Ask the client to re-evaluate message filters (e.g. a plugin's
    // settings or the user's relationships changed).
    void requestMessageFilterRefresh() { emit messageFiltersChanged(); }

    void dispatchBeforeSend(OutgoingMessage &message);
    void dispatchBeforeEdit(OutgoingMessage &message);
    // First plugin with an opinion wins; otherwise Discord's default of
    // pinging unless shift is held.
    [[nodiscard]] bool dispatchShouldMentionOnReply(const ReplyContext &reply);
    [[nodiscard]] QString dispatchTransformCopiedMessageLink(const QString &url);
    void dispatchTransformIncomingContent(IncomingMessage &message);

    struct PluginButton
    {
        QString pluginId;
        ChatBarButton button;
    };
    [[nodiscard]] QList<PluginButton> dispatchChatBarButtons(const ChatContext &chat);
    void dispatchChatBarButtonClicked(const QString &pluginId, const QString &buttonId, const ChatContext &chat);

    struct PluginMenuItem
    {
        QString pluginId;
        MenuItem item;
    };
    [[nodiscard]] QList<PluginMenuItem> dispatchUserContextMenuItems(const UserContext &user);
    [[nodiscard]] QList<PluginMenuItem> dispatchMessageContextMenuItems(const MessageContext &message);
    void dispatchMessageContextMenuAction(const QString &pluginId, const QString &itemId, const MessageContext &message);

    // True if any plugin keeps the deleted message on screen.
    [[nodiscard]] bool dispatchMessageDeleted(const MessageContext &message);
    void dispatchMessageEdited(const MessageContext &before, const MessageContext &after);
    // First plugin with an annotation wins.
    [[nodiscard]] std::optional<MessageAnnotation> dispatchAnnotateMessage(const MessageContext &message);

    [[nodiscard]] bool dispatchShouldHideMessage(const MessageContext &message);
    // Hidden messages are always silent.
    [[nodiscard]] bool dispatchShouldSilenceMessage(const MessageContext &message);
    void dispatchUserContextMenuAction(const QString &pluginId, const QString &itemId, const UserContext &user);

    // Any plugin saying no wins (default: send / show).
    [[nodiscard]] bool dispatchShouldSendTyping(const TypingContext &typing);
    [[nodiscard]] bool dispatchShouldShowTypers(const ChatContext &chat);
    // Any plugin saying no skips the warning (default: confirm).
    [[nodiscard]] bool dispatchShouldConfirmLink(const QString &url);
    // Any plugin saying no suppresses the notification (default: notify).
    [[nodiscard]] bool dispatchShouldNotify(const NotificationContext &notification);
    // First plugin with a name wins.
    [[nodiscard]] std::optional<QString> dispatchAuthorDisplayName(const AuthorContext &author);
    [[nodiscard]] QList<PluginMenuItem> dispatchChannelContextMenuItems(const ChannelContext &channel);
    void dispatchChannelContextMenuAction(const QString &pluginId, const QString &itemId, const ChannelContext &channel);

signals:
    void pluginStateChanged(const QString &id);
    // Something that affects how received messages are rendered changed
    // (a display plugin was toggled or reconfigured).
    void displayTransformsChanged();
    // Chat bar buttons need to be re-queried (a plugin was toggled, or one
    // asked for a refresh).
    void chatBarChanged();
    // Message filters or annotations changed: re-render what is on screen.
    void messageFiltersChanged();
    // How author names or typing are shown changed: re-render.
    void presentationChanged();

private:
    struct Entry
    {
        PluginDescriptor descriptor;
        PluginInfo info;
        QString manifestError; // permanent: the plugin can never start
        std::unique_ptr<PluginSettings> settings;
        std::unique_ptr<PluginContext> context;
        std::unique_ptr<Plugin> plugin;
    };

    Entry *find(const QString &id);
    const Entry *find(const QString &id) const;
    bool start(Entry &entry, QString *error);
    void stop(Entry &entry);
    void rebuildTables();
    void loadState();
    void saveState() const;
    void onSettingChanged(Entry &entry, const QString &key);
    Plugin *tablePlugin(Hook hook, const QString &id) const;
    void notifyHooksChanged(Hooks hooks);

    std::map<QString, Entry> entries; // sorted by id: dispatch order is deterministic
    Storage *storage;
    Services services;
    QString dataDir;
    std::array<std::vector<Plugin *>, HookCount> tables;
    std::array<std::vector<QString>, HookCount> tableIds;
};

} // namespace Plugins
} // namespace Qord
