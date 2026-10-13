/*
 * Qord, a native Discord client
 * Port of Equicord's MessageLinkEmbeds plugin (src/plugins/messageLinkEmbeds)
 * Original: Copyright (c) Vendicated and contributors, authors TheSun, Ven, RyanCaoDev
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "MessageLinkEmbeds.hpp"

#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

#include <QRegularExpression>

#include <algorithm>
#include <iterator>

namespace Qord {
namespace Plugins {

namespace {
constexpr int DmType = 1, GroupDmType = 3;

QString key(Id channelId, Id messageId)
{
    return QStringLiteral("%1:%2").arg(channelId).arg(messageId);
}
} // namespace

QList<MessageLinkEmbedsPlugin::Link> MessageLinkEmbedsPlugin::links(const QString &content)
{
    // Upstream's messageLinkRegex; QRegularExpression has the lookbehind too
    static const QRegularExpression pattern(QStringLiteral(
            "(?<!<)https?://(?:\\w+\\.)?discord(?:app)?\\.com/channels/(?:\\d{17,20}|@me)/(\\d{17,20})/(\\d{17,20})"));
    QList<Link> out;
    auto it = pattern.globalMatch(content);
    while (it.hasNext()) {
        const auto match = it.next();
        out.append({ match.captured(1).toULongLong(), match.captured(2).toULongLong() });
    }
    return out;
}

QString MessageLinkEmbedsPlugin::noContent(int attachments, int embeds)
{
    auto plural = [](int n, const char *word) {
        return QStringLiteral("%1 %2%3").arg(n).arg(QLatin1String(word), n != 1 ? QStringLiteral("s") : QString());
    };
    if (!attachments && !embeds)
        return {};
    if (!attachments)
        return QStringLiteral("[no content, %1]").arg(plural(embeds, "embed"));
    if (!embeds)
        return QStringLiteral("[no content, %1]").arg(plural(attachments, "attachment"));
    return QStringLiteral("[no content, %1 and %2]").arg(plural(attachments, "attachment"), plural(embeds, "embed"));
}

void MessageLinkEmbedsPlugin::start(PluginContext &ctx)
{
    context = &ctx;
    alive = std::make_shared<bool>(true);
}

void MessageLinkEmbedsPlugin::stop()
{
    if (alive)
        *alive = false;
    queue.clear();
    running = false;
    context = nullptr;
}

std::optional<MessageAccessory> MessageLinkEmbedsPlugin::messageAccessory(const MessageContext &message)
{
    MessageLookupService *lookup = context ? context->messageLookup() : nullptr;
    if (!lookup || message.content.isEmpty())
        return std::nullopt;
    const QString idList = context->settings().stringValue(QStringLiteral("idList"));
    const bool whitelist = context->settings().stringValue(QStringLiteral("listMode")) == QLatin1String("whitelist");

    MessageAccessory accessory;
    QSet<Id> seen;
    for (const Link &link : links(message.content)) {
        if (seen.contains(link.messageId) || int(seen.size()) >= MaxEmbeds)
            continue;
        seen.insert(link.messageId);
        const auto channel = lookup->channel(message.accountId, link.channelId);
        if (!channel || !channel->canView)
            continue;
        // Upstream checks the ID list as text, as here
        const Id ids[] = { channel->guildId, link.channelId, message.authorId };
        const bool listed = std::any_of(std::begin(ids), std::end(ids),
                                        [&](Id id) { return id && idList.contains(QString::number(id)); });
        if (whitelist != listed)
            continue;
        const auto linked = lookup->message(message.accountId, link.channelId, link.messageId);
        if (!linked) {
            queueFetch(message, link);
            continue;
        }

        // Upstream's automod-style embed: where it is, what it says, its images
        MessageAccessory::Embed embed;
        QString label;
        if (channel->type == DmType)
            label = QStringLiteral("Direct Message - @%1").arg(channel->name);
        else if (channel->type == GroupDmType)
            label = QStringLiteral("Group DM - %1").arg(channel->name);
        else
            label = QStringLiteral("Server - #%1").arg(channel->name);
        embed.authorName = label;
        embed.authorIconUrl = channel->iconUrl;
        embed.description = !linked->content.isEmpty() || linked->attachmentCount <= linked->images.size()
                                    ? linked->content
                                    : noContent(linked->attachmentCount, linked->embedCount);
        embed.footerText = linked->authorName;
        embed.footerIconUrl = linked->authorAvatarUrl;
        embed.timestamp = linked->timestamp;
        for (const auto &image : linked->images)
            embed.images.append({ image.url, image.width, image.height });
        accessory.embeds.append(embed);
    }
    if (accessory.embeds.isEmpty())
        return std::nullopt;
    return accessory;
}

// Upstream's Queue: one fetch at a time, the newest first
void MessageLinkEmbedsPlugin::queueFetch(const MessageContext &message, const Link &link)
{
    const QString k = key(link.channelId, link.messageId);
    if (requested.contains(k))
        return;
    requested.insert(k);
    std::weak_ptr<bool> weak = alive;
    queue.push_front([this, weak, message, link](std::function<void()> next) {
        MessageLookupService *lookup = context ? context->messageLookup() : nullptr;
        if (!lookup) {
            next();
            return;
        }
        lookup->fetchMessage(message.accountId, link.channelId, link.messageId, [this, weak, message, next](bool found) {
            if (auto a = weak.lock(); !a || !*a)
                return;
            if (found && context)
                if (MessageService *messages = context->messages())
                    messages->refreshMessage(message.accountId, message.channelId, message.messageId);
            next();
        });
    });
    runQueue();
}

void MessageLinkEmbedsPlugin::runQueue()
{
    if (running || queue.empty())
        return;
    running = true;
    auto task = std::move(queue.front());
    queue.pop_front();
    std::weak_ptr<bool> weak = alive;
    task([this, weak]() {
        if (auto a = weak.lock(); !a || !*a)
            return;
        running = false;
        runQueue();
    });
}

QByteArray qord_manifest_messagelinkembeds();

void qord_register_messagelinkembeds(Registry &registry)
{
    registry.add({ QStringLiteral("messagelinkembeds/plugin.json"),
                   [] { return std::make_unique<MessageLinkEmbedsPlugin>(); }, qord_manifest_messagelinkembeds() });
}

} // namespace Plugins
} // namespace Qord
