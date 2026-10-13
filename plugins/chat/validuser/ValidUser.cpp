/*
 * Qord, a native Discord client
 * Port of Equicord's ValidUser plugin (src/plugins/validUser)
 * Original: Copyright (c) Vendicated and contributors, authors Ven, Dolfies
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ValidUser.hpp"

#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

#include <QTimer>

namespace Qord {
namespace Plugins {

void ValidUserPlugin::start(PluginContext &context)
{
    this->context = &context;
    alive = std::make_shared<bool>(true);
}

void ValidUserPlugin::stop()
{
    if (alive)
        *alive = false;
    alive.reset();
    queue.clear();
    fetching.clear();
    running = false;
    context = nullptr;
}

void ValidUserPlugin::onUserMentionHovered(const MentionContext &mention)
{
    UserLookupService *users = context ? context->users() : nullptr;
    if (!users || fetching.contains(mention.userId))
        return;
    // Known by now (another mention fetched them): just show the name
    if (users->isKnown(mention.accountId, mention.userId)) {
        users->refreshMessage(mention.accountId, mention.channelId, mention.messageId);
        return;
    }
    fetch(mention);
}

void ValidUserPlugin::fetch(const MentionContext &mention)
{
    fetching.insert(mention.userId);
    if (int(queue.size()) >= MaxQueued) {
        fetching.remove(queue.back().first);
        queue.pop_back();
    }
    std::weak_ptr<bool> weak = alive;
    queue.emplace_front(mention.userId, [this, weak, mention]() {
        UserLookupService *users = context ? context->users() : nullptr;
        if (!users) {
            running = false;
            return;
        }
        users->fetchUser(mention.accountId, mention.userId, [this, weak, mention](const UserLookupService::Result &result) {
            if (auto a = weak.lock(); !a || !*a)
                return;
            if (result.ok) {
                fetching.remove(mention.userId);
                if (UserLookupService *users = context ? context->users() : nullptr)
                    users->refreshMessage(mention.accountId, mention.channelId, mention.messageId);
            } else if (result.status == 429) {
                // Wait out the rate limit, then look them up again
                fetching.remove(mention.userId);
                queue.emplace_front(mention.userId, [this, weak, mention, wait = result.retryAfterMs]() {
                    QTimer::singleShot(wait, [this, weak, mention]() {
                        if (auto a = weak.lock(); !a || !*a)
                            return;
                        fetch(mention);
                        running = false;
                        runNext();
                    });
                });
            }
            // Any other failure: upstream leaves the user marked as fetching
            QTimer::singleShot(SpacingMs, [this, weak]() {
                if (auto a = weak.lock(); !a || !*a)
                    return;
                running = false;
                runNext();
            });
        });
    });
    runNext();
}

void ValidUserPlugin::runNext()
{
    if (running || queue.empty())
        return;
    running = true;
    auto task = std::move(queue.front().second);
    queue.pop_front();
    task();
}

QByteArray qord_manifest_validuser();

void qord_register_validuser(Registry &registry)
{
    registry.add({ QStringLiteral("validuser/plugin.json"), [] { return std::make_unique<ValidUserPlugin>(); },
                   qord_manifest_validuser() });
}

} // namespace Plugins
} // namespace Qord
