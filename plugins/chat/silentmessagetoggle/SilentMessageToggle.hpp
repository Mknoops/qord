/*
 * Qord, a native Discord client
 * Port of Equicord's SilentMessageToggle plugin (src/plugins/silentMessageToggle)
 * Original: Copyright (c) Vendicated and contributors, authors Nuckyz, CatNoir, Z1xus
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "Plugins/Plugin.hpp"

namespace Qord {
namespace Plugins {

class PluginContext;

// A chat bar toggle that prefixes messages with "@silent ", which the client
// turns into a message that doesn't notify (Discord's SUPPRESS_NOTIFICATIONS).
class SilentMessageTogglePlugin : public Plugin
{
public:
    enum class Persist { None, Channels, Restarts };

    [[nodiscard]] Hooks hooks() const override { return Hook::BeforeSend | Hook::ChatBarButtons; }
    void start(PluginContext &context) override;
    void stop() override { context = nullptr; }
    void onSettingChanged(const QString &key) override;
    void onBeforeSend(OutgoingMessage &message) override;
    QList<ChatBarButton> chatBarButtons(const ChatContext &chat) override;
    void onChatBarButtonClicked(const QString &id, const ChatContext &chat) override;

    [[nodiscard]] bool isOn() const { return enabled; }

private:
    void setEnabled(bool on);
    void followChannel(Id channelId);

    PluginContext *context = nullptr;
    Persist persist = Persist::None;
    bool enabled = false;
    Id channel = 0;
};

} // namespace Plugins
} // namespace Qord
