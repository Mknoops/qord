#pragma once

#include "Permission.hpp"
#include "Services.hpp"
#include "Settings.hpp"

#include <QSet>
#include <QString>

#include <functional>
#include <memory>

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
    // Null without the write_clipboard permission.
    [[nodiscard]] ClipboardService *clipboard() const;
    // Null without the read_relationships permission.
    [[nodiscard]] const RelationshipDirectory *relationships() const;
    // Dialogs need add_ui_elements; openUrl() additionally needs open_links
    // (it does nothing without it). Null without add_ui_elements.
    [[nodiscard]] UiService *ui();
    // Null unless the plugin may change how messages are shown
    // (log_messages or hide_messages).
    [[nodiscard]] MessageService *messages() const;
    // Null without the edit_messages permission.
    [[nodiscard]] MessageActionService *messageActions() const;
    // Null without the modify_displayed_messages permission.
    [[nodiscard]] DisplayOptionsService *displayOptions() const;
    // Null without the mark_read permission.
    [[nodiscard]] ReadStateService *readStates() const;
    // Null without the read_reactions permission. Replies arriving after the
    // plugin stopped are dropped.
    [[nodiscard]] ReactionService *reactions();
    // Null without the read_expressions permission.
    [[nodiscard]] ExpressionService *expressions() const;
    // Null without the read_server_permissions permission.
    [[nodiscard]] GuildDirectory *guilds() const;
    // Null without the fetch_users permission. Replies arriving after the
    // plugin stopped are dropped.
    [[nodiscard]] UserLookupService *users();

    // Asks the client to re-query this plugin's chat bar buttons.
    void refreshChatBar() const
    {
        if (onChatBarChanged)
            onChatBarChanged();
    }
    std::function<void()> onChatBarChanged; // set by the host

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
    std::unique_ptr<UiService> scopedUi;
    std::unique_ptr<ReactionService> scopedReactions;
    std::unique_ptr<UserLookupService> scopedUsers;
};

} // namespace Plugins
} // namespace Qord
