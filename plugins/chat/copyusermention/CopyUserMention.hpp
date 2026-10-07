/*
 * Qord, a native Discord client
 * Port of Equicord's CopyUserMention plugin (src/equicordplugins/copyUserMention)
 * Original: Copyright (c) Vendicated and contributors, authors Cortex, castdrian
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "Plugins/Plugin.hpp"

namespace Qord {
namespace Plugins {

class PluginContext;

// Adds "Copy User Mention" to the user context menu; copies a mention of the user (<@ID>).
class CopyUserMentionPlugin : public Plugin
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
