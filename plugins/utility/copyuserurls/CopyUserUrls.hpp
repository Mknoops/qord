/*
 * Qord, a native Discord client
 * Port of Equicord's CopyUserURLs plugin (src/plugins/copyUserURLs)
 * Original: Copyright (c) Vendicated and contributors, author castdrian
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "Plugins/Plugin.hpp"

namespace Qord {
namespace Plugins {

class PluginContext;

// Adds "Copy User URL" to the user context menu; copies the user's profile link as <https://discord.com/users/ID>.
class CopyUserUrlsPlugin : public Plugin
{
public:
    static QString textFor(Id userId);

    [[nodiscard]] Hooks hooks() const override { return Hook::UserContextMenu; }
    void start(PluginContext &context) override { this->context = &context; }
    void stop() override { context = nullptr; }
    QList<MenuItem> userContextMenuItems(const UserContext &user) override;
    void onUserContextMenuAction(const QString &id, const UserContext &user) override;

private:
    PluginContext *context = nullptr;
};

} // namespace Plugins
} // namespace Qord
