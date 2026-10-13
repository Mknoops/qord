/*
 * Qord, a native Discord client
 * Port of Equicord's PermissionsViewer plugin (src/plugins/permissionsViewer)
 * Original: Copyright (c) Vendicated and contributors, authors Nuckyz, Ven
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "Plugins/Plugin.hpp"

namespace Qord {
namespace Plugins {

class PluginContext;

// "View Permissions" for members, channels and servers: their roles (or the
// channel's overwrites) and what each allows and denies; in a member's
// profile, their permissions and the role granting each.
class PermissionsViewerPlugin : public Plugin
{
public:
    // Every permission Discord defines (bits 0 to 52); the owner has them all
    static constexpr quint64 AllPermissions = (quint64(1) << 53) - 1;

    [[nodiscard]] Hooks hooks() const override
    {
        return Hook::UserContextMenu | Hook::ChannelContextMenu | Hook::ProfileRoleButtons;
    }
    void start(PluginContext &context) override { this->context = &context; }
    void stop() override { context = nullptr; }

    QList<MenuItem> userContextMenuItems(const UserContext &user) override;
    void onUserContextMenuAction(const QString &id, const UserContext &user) override;
    QList<MenuItem> channelContextMenuItems(const ChannelContext &channel) override;
    void onChannelContextMenuAction(const QString &id, const ChannelContext &channel) override;
    QList<ChatBarButton> profileRoleButtons(const UserContext &user) override;
    void onProfileRoleButtonClicked(const QString &id, const UserContext &user) override;

    // The views, built as upstream builds them (public for tests)
    [[nodiscard]] std::optional<PermissionsView> memberView(Id accountId, Id guildId, Id userId) const;
    [[nodiscard]] std::optional<PermissionsView> channelView(Id accountId, Id channelId) const;
    [[nodiscard]] std::optional<PermissionsView> guildView(Id accountId, Id guildId) const;
    [[nodiscard]] std::optional<MemberPermissionsView> memberPermissions(const UserContext &user);

private:
    [[nodiscard]] bool isMember(Id accountId, Id guildId, Id userId) const;
    PluginContext *context = nullptr;
};

} // namespace Plugins
} // namespace Qord
