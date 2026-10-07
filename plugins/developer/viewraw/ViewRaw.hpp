/*
 * Qord, a native Discord client
 * Port of Equicord's ViewRaw plugin (src/plugins/viewRaw)
 * Original: Copyright (c) Vendicated and contributors, authors KingFish, Ven, rad, ImLvna
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "Plugins/Plugin.hpp"

namespace Qord {
namespace Plugins {

class PluginContext;

// "View Raw" (content + data) and "Copy Raw Content" for messages.
class ViewRawPlugin : public Plugin
{
public:
    [[nodiscard]] Hooks hooks() const override { return Hook::MessageContextMenu; }
    void start(PluginContext &context) override { this->context = &context; }
    void stop() override { context = nullptr; }
    QList<MenuItem> messageContextMenuItems(const MessageContext &message) override;
    void onMessageContextMenuAction(const QString &id, const MessageContext &message) override;

private:
    PluginContext *context = nullptr;
};

} // namespace Plugins
} // namespace Qord
