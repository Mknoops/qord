/*
 * Qord, a native Discord client
 * Port of Equicord's ViewRaw plugin (src/plugins/viewRaw)
 * Original: Copyright (c) Vendicated and contributors, authors KingFish, Ven, rad, ImLvna
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "Plugins/Plugin.hpp"
#include "Plugins/Services.hpp"

namespace Qord {
namespace Plugins {

class PluginContext;

// "View Raw" for messages (a hover-bar button, and the message menu when
// turned on), servers, channels, users and roles
class ViewRawPlugin : public Plugin
{
public:
    [[nodiscard]] Hooks hooks() const override
    {
        return Hook::MessageContextMenu | Hook::MessagePopover | Hook::ChannelContextMenu | Hook::UserContextMenu |
               Hook::RoleContextMenu;
    }
    void start(PluginContext &context) override { this->context = &context; }
    void stop() override { context = nullptr; }

    QList<MenuItem> messageContextMenuItems(const MessageContext &message) override;
    void onMessageContextMenuAction(const QString &id, const MessageContext &message) override;
    QList<ChatBarButton> messagePopoverButtons(const MessageContext &message) override;
    void onMessagePopoverClicked(const QString &id, const MessageContext &message, bool rightClick) override;
    QList<MenuItem> channelContextMenuItems(const ChannelContext &channel) override;
    void onChannelContextMenuAction(const QString &id, const ChannelContext &channel) override;
    QList<MenuItem> userContextMenuItems(const UserContext &user) override;
    void onUserContextMenuAction(const QString &id, const UserContext &user) override;
    QList<MenuItem> roleContextMenuItems(const RoleContext &role) override;
    void onRoleContextMenuAction(const QString &id, const RoleContext &role) override;

private:
    void viewMessage(const MessageContext &message);
    void copyContent(const MessageContext &message);
    // openViewRawModal(json, type)
    void view(GuildDirectory::RawKind kind, const QString &type, Id accountId, Id id, Id guildId);
    [[nodiscard]] bool rightViews() const;

    PluginContext *context = nullptr;
};

} // namespace Plugins
} // namespace Qord
