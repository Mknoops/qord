/*
 * Qord, a native Discord client
 * Port of Equicord's TypingIndicator plugin (src/plugins/typingIndicator)
 * Original: Copyright (c) Vendicated and contributors, authors Nuckyz, fawn, Sqaaakoi
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "Plugins/Plugin.hpp"

namespace Qord {
namespace Plugins {

class PluginContext;

// Who is typing, next to channels in the channel list.
class TypingIndicatorPlugin : public Plugin
{
public:
    [[nodiscard]] Hooks hooks() const override { return Hook::ChannelTyping; }
    void start(PluginContext &context) override { this->context = &context; }
    void stop() override { context = nullptr; }
    std::optional<TypingIndicator> channelTypingIndicator(const ChannelTypingContext &channel) override;

    // Discord's ONE/TWO/THREE/SEVERAL_USERS_TYPING
    [[nodiscard]] static QString tooltip(const QStringList &names);

private:
    [[nodiscard]] QString displayName(Id accountId, Id guildId, Id userId) const;
    PluginContext *context = nullptr;
};

} // namespace Plugins
} // namespace Qord
