/*
 * Qord, a native Discord client
 * Port of Equicord's ServerInfo plugin (src/plugins/serverInfo)
 * Original: Copyright (c) Vendicated and contributors, authors Ven, Nuckyz, Z1xus
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "Plugins/Plugin.hpp"

namespace Qord {
namespace Plugins {

class PluginContext;

// "Server Info" in the server menu: the server's details and your friends,
// mutual users, blocked and ignored users in it.
class ServerInfoPlugin : public Plugin
{
public:
    [[nodiscard]] Hooks hooks() const override { return Hook::ChannelContextMenu; }
    void start(PluginContext &context) override { this->context = &context; }
    void stop() override { context = nullptr; }
    QList<MenuItem> channelContextMenuItems(const ChannelContext &channel) override;
    void onChannelContextMenuAction(const QString &id, const ChannelContext &channel) override;

    [[nodiscard]] std::optional<ServerInfoView> view(Id accountId, Id guildId);
    // Discord's guild acronym ("Qord Dev" -> "QD")
    [[nodiscard]] static QString acronym(const QString &name);

private:
    PluginContext *context = nullptr;
};

} // namespace Plugins
} // namespace Qord
