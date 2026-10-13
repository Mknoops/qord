/*
 * Qord, a native Discord client
 * Port of Equicord's QuickReply plugin (src/plugins/quickReply)
 * Original: Copyright (c) Vendicated and contributors, authors fawn, Ven, pylix
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "Plugins/Plugin.hpp"

namespace Qord {
namespace Plugins {

class PluginContext;

// Reply to (Ctrl+Up/Down) and edit (Ctrl+Shift+Up/Down) messages from the keyboard.
class QuickReplyPlugin : public Plugin
{
public:
    [[nodiscard]] Hooks hooks() const override { return Hook::KeyboardShortcuts; }
    void start(PluginContext &context) override { this->context = &context; }
    void stop() override { context = nullptr; }
    bool onShortcut(const ShortcutContext &shortcut) override;

    // Discord's MessageTypeSets.REPLYABLE
    [[nodiscard]] static bool isReplyable(int type);

private:
    PluginContext *context = nullptr;
};

} // namespace Plugins
} // namespace Qord
