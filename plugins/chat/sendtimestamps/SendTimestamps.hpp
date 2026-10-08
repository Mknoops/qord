/*
 * Qord, a native Discord client
 * Port of Equicord's SendTimestamps plugin (src/plugins/sendTimestamps)
 * Original: Copyright (c) Vendicated and contributors, authors V, \GGTyler\, Grzesiek11
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "Plugins/Plugin.hpp"

#include <QDateTime>

#include <optional>

namespace Qord {
namespace Plugins {

namespace SendTimestamps {

// Seconds after local midnight that V8's Date parser reads from "H:MM",
// "H:MM AM" etc. (as upstream builds it); nullopt = Invalid Date.
std::optional<int> secondsAfterMidnight(const QString &clean);
// upstream parseTime(): "`17:59`" -> "<t:...:t>", or the input unchanged
QString parseTime(const QString &backticked, const QDateTime &now);
// upstream onBeforeMessageSend replacement
QString replaceTimes(const QString &content, const QDateTime &now);

} // namespace SendTimestamps

class SendTimestampsPlugin : public Plugin
{
public:
    [[nodiscard]] Hooks hooks() const override { return Hook::BeforeSend | Hook::ChatBarButtons; }
    void start(PluginContext &context) override { this->context = &context; }

    void onBeforeSend(OutgoingMessage &message) override;
    QList<ChatBarButton> chatBarButtons(const ChatContext &chat) override;
    void onChatBarButtonClicked(const QString &id, const ChatContext &chat) override;

private:
    PluginContext *context = nullptr;
};

} // namespace Plugins
} // namespace Qord
