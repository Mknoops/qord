#pragma once

#include "Plugins/Plugin.hpp"

#include <QHash>
#include <QList>

#include <deque>
#include <memory>

namespace Qord {
namespace Plugins {

class WhoReactedPlugin : public Plugin
{
public:
    // Upstream: messages with more reactions show no avatars
    static constexpr int MaxReactions = 10;
    // Upstream's queue sleeps this long after each fetch
    static constexpr int FetchSpacingMs = 250;

    [[nodiscard]] Hooks hooks() const override { return Hook::ReactionUsers; }
    void start(PluginContext &ctx) override;
    void stop() override;

    std::optional<ReactionUsers> reactionUsers(const ReactionContext &reaction) override;
    void onReactionChanged(const ReactionChange &change) override;

    // upstream: `${msg.id}:${e.name}:${e.id ?? ""}:${type}`
    static QString cacheKey(Id messageId, const QString &emojiName, Id emojiId, int type);
    // The emoji as Discord's reactions endpoint names it: "name" or "name:id"
    static QString emojiKey(const QString &emojiName, Id emojiId);

private:
    struct Entry
    {
        bool fetched = false;
        QList<Id> users;
    };
    struct Job
    {
        Id accountId;
        Id channelId;
        Id messageId;
        QString emojiName;
        Id emojiId;
        int type;
    };
    void pump();

    PluginContext *context = nullptr;
    QHash<QString, Entry> cache;
    std::deque<Job> queue;
    bool busy = false;
    std::shared_ptr<bool> alive;
};

} // namespace Plugins
} // namespace Qord
