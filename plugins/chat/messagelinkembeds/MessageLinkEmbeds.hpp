/*
 * Qord, a native Discord client
 * Port of Equicord's MessageLinkEmbeds plugin (src/plugins/messageLinkEmbeds)
 * Original: Copyright (c) Vendicated and contributors, authors TheSun, Ven, RyanCaoDev
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "Plugins/Plugin.hpp"

#include <QSet>

#include <deque>
#include <functional>

namespace Qord {
namespace Plugins {

class PluginContext;

// A preview of the message a Discord message link points at, under the
// message with the link.
class MessageLinkEmbedsPlugin : public Plugin
{
public:
    static constexpr int MaxEmbeds = 5;

    struct Link
    {
        Id channelId = 0;
        Id messageId = 0;
    };
    static QList<Link> links(const QString &content);
    static QString noContent(int attachments, int embeds);

    [[nodiscard]] Hooks hooks() const override { return Hook::MessageAccessories; }
    void start(PluginContext &context) override;
    void stop() override;
    std::optional<MessageAccessory> messageAccessory(const MessageContext &message) override;

private:
    void queueFetch(const MessageContext &message, const Link &link);
    void runQueue();

    PluginContext *context = nullptr;
    QSet<QString> requested; // channel:message, fetched once
    std::deque<std::function<void(std::function<void()>)>> queue;
    bool running = false;
    std::shared_ptr<bool> alive;
};

} // namespace Plugins
} // namespace Qord
