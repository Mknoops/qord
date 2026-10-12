/*
 * Qord, a native Discord client
 * Port of Equicord's WhoReacted plugin (src/plugins/whoReacted)
 * Original: Copyright (c) 2022 Vendicated and contributors, authors Ven, KannaDev, newwares, paige
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "WhoReacted.hpp"

#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

#include <QTimer>

namespace Qord {
namespace Plugins {

namespace {
// Upstream keeps every entry until the gateway reconnects; a long session
// shouldn't grow without end, so the cache starts over past this many
constexpr int MaxEntries = 5000;
} // namespace

QString WhoReactedPlugin::cacheKey(Id messageId, const QString &emojiName, Id emojiId, int type)
{
    return QStringLiteral("%1:%2:%3:%4")
            .arg(messageId)
            .arg(emojiName, emojiId ? QString::number(emojiId) : QString())
            .arg(type);
}

QString WhoReactedPlugin::emojiKey(const QString &emojiName, Id emojiId)
{
    return emojiId ? QStringLiteral("%1:%2").arg(emojiName).arg(emojiId) : emojiName;
}

void WhoReactedPlugin::start(PluginContext &ctx)
{
    context = &ctx;
    alive = std::make_shared<bool>(true);
}

void WhoReactedPlugin::stop()
{
    if (alive)
        *alive = false;
    alive.reset();
    context = nullptr;
    cache.clear();
    queue.clear();
    busy = false;
}

std::optional<ReactionUsers> WhoReactedPlugin::reactionUsers(const ReactionContext &reaction)
{
    if (!context || reaction.reactionsOnMessage > MaxReactions)
        return std::nullopt;
    if (cache.size() > MaxEntries)
        cache.clear();
    Entry &entry = cache[cacheKey(reaction.messageId, reaction.emojiName, reaction.emojiId, reaction.type)];
    if (!entry.fetched) {
        // upstream: queue.unshift(...), so what is on screen now goes first
        entry.fetched = true;
        queue.push_front({ reaction.accountId, reaction.channelId, reaction.messageId, reaction.emojiName,
                           reaction.emojiId, reaction.type });
        pump();
    }
    if (entry.users.isEmpty())
        return std::nullopt;
    ReactionUsers out;
    out.users = entry.users;
    out.clickable = context->settings().boolValue(QStringLiteral("clickableAvatars"));
    return out;
}

void WhoReactedPlugin::onReactionChanged(const ReactionChange &change)
{
    if (!context)
        return;
    auto it = cache.find(cacheKey(change.messageId, change.emojiName, change.emojiId, change.type));
    if (it == cache.end() || !it->fetched)
        return; // not shown yet: the fetch will have it
    if (change.added) {
        if (it->users.contains(change.userId))
            return;
        it->users.append(change.userId);
    } else if (!it->users.removeOne(change.userId)) {
        return;
    }
    if (ReactionService *reactions = context->reactions())
        reactions->reactionUsersChanged(change.accountId, change.channelId, change.messageId);
}

void WhoReactedPlugin::pump()
{
    if (busy || queue.empty() || !context)
        return;
    ReactionService *reactions = context->reactions();
    if (!reactions)
        return;
    busy = true;
    const Job job = queue.front();
    queue.pop_front();
    const std::weak_ptr<bool> weak = alive;
    reactions->fetchReactionUsers(
            job.accountId, job.channelId, job.messageId, emojiKey(job.emojiName, job.emojiId), job.type,
            [this, weak, job](bool ok, const QList<Id> &users) {
                const auto a = weak.lock();
                if (!a || !*a)
                    return;
                if (ok) {
                    Entry &entry = cache[cacheKey(job.messageId, job.emojiName, job.emojiId, job.type)];
                    // Live additions that came in meanwhile stay
                    QList<Id> merged = users;
                    for (Id id : std::as_const(entry.users))
                        if (!merged.contains(id))
                            merged.append(id);
                    entry.fetched = true;
                    entry.users = merged;
                    if (ReactionService *service = context ? context->reactions() : nullptr)
                        service->reactionUsersChanged(job.accountId, job.channelId, job.messageId);
                }
                // upstream: .finally(() => sleep(250)) before the next one
                QTimer::singleShot(FetchSpacingMs, [this, weak]() {
                    const auto alive = weak.lock();
                    if (!alive || !*alive)
                        return;
                    busy = false;
                    pump();
                });
            });
}

QByteArray qord_manifest_whoreacted();

void qord_register_whoreacted(Registry &registry)
{
    registry.add({ QStringLiteral("whoreacted/plugin.json"), [] { return std::make_unique<WhoReactedPlugin>(); },
                   qord_manifest_whoreacted() });
}

} // namespace Plugins
} // namespace Qord
