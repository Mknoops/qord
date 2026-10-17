/*
 * Qord, a native Discord client
 * Port of Equicord's SilentTyping plugin (src/plugins/silentTyping)
 * Original: Copyright (c) Vendicated and contributors, authors V, rini, Im_Banana, Etorix
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "Plugins/Plugin.hpp"

#include <QStringList>

namespace Qord {
namespace Plugins {

namespace SilentTyping {

struct Config
{
    bool enabledGlobally = true;
    bool defaultHidden = true;
    bool alwaysEnableInActiveVoiceChat = false;
    int thresholdServers = 0; // seconds
    int thresholdDirectMessages = 0; // seconds
    QStringList enabledLocations;
    QStringList disabledLocations;
};

// "1, 2,,3 " -> ["1", "2", "3"] (upstream: split(",").map(trim).filter(Boolean))
QStringList parseLocations(const QString &setting);
// The list that applies under the current "Default Hidden" mode
const QStringList &effectiveList(const Config &config);
// upstream isExplicitlyDisallowed
bool isExplicitlyDisallowed(const Config &config, Id channelId, Id guildId);
// upstream isImplicitlyAllowed: 0 no, 1 connected voice channel, 2 recent own message
int implicitlyAllowed(const Config &config, const TypingContext &typing, const QDateTime &now);
bool shouldSendTyping(const Config &config, const TypingContext &typing, const QDateTime &now);
// The chat icon tooltip, as upstream words it
QString tooltip(const Config &config, Id channelId, Id guildId, int implicit);
// toggleLocation: adds or removes an id from the effective list
QStringList toggled(QStringList list, const QString &id);

} // namespace SilentTyping

class SilentTypingPlugin : public Plugin
{
public:
    [[nodiscard]] Hooks hooks() const override
    {
        return Hook::Typing | Hook::ChatBarButtons | Hook::SlashCommands | Hook::ChatInputContextMenu;
    }

    void start(PluginContext &context) override;
    void onSettingChanged(const QString &key) override;

    std::optional<bool> shouldSendTyping(const TypingContext &typing) override;
    std::optional<bool> shouldShowTypers(const ChatContext &chat) override;
    std::optional<bool> shouldShowMemberListTypers(const ChatContext &chat) override;
    QList<ChatBarButton> chatBarButtons(const ChatContext &chat) override;
    void onChatBarButtonClicked(const QString &id, const ChatContext &chat) override;
    std::optional<QString> onSlashCommand(const QString &name, const QJsonObject &options,
                                          const ChatContext &chat) override;
    QList<MenuItem> chatInputContextMenuItems(const ChatContext &chat) override;
    void onChatInputContextMenuAction(const QString &id, const ChatContext &chat) override;

private:
    SilentTyping::Config readConfig() const;
    void perform(const QString &action, const ChatContext &chat);

    PluginContext *context = nullptr;
    SilentTyping::Config config;
};

} // namespace Plugins
} // namespace Qord
