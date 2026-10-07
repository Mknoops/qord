/*
 * Qord, a native Discord client
 * Port of Equicord's TalkInReverse plugin (src/equicordplugins/talkInReverse)
 * Original: Copyright (c) Vendicated and contributors, author Tolgchu
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "Plugins/Plugin.hpp"

namespace Qord {
namespace Plugins {

class PluginContext;

namespace TalkInReverse {
// Reverses the text by user-perceived characters, so emoji and accented
// letters stay intact.
QString reverse(const QString &text);
} // namespace TalkInReverse

// A chat bar toggle; while on, messages you send are reversed. Like upstream,
// the toggle survives switching channels but not a restart.
class TalkInReversePlugin : public Plugin
{
public:
    [[nodiscard]] Hooks hooks() const override { return Hook::BeforeSend | Hook::ChatBarButtons; }
    void start(PluginContext &context) override { this->context = &context; }
    void stop() override { context = nullptr; }
    void onBeforeSend(OutgoingMessage &message) override;
    QList<ChatBarButton> chatBarButtons(const ChatContext &chat) override;
    void onChatBarButtonClicked(const QString &id, const ChatContext &chat) override;

    [[nodiscard]] bool isOn() const { return enabled; }

private:
    PluginContext *context = nullptr;
    bool enabled = false;
};

} // namespace Plugins
} // namespace Qord
