/*
 * Qord, a native Discord client
 * Port of Equicord's NoBlockedMessages plugin (src/plugins/noBlockedMessages)
 * Original: Copyright (c) Vendicated and contributors,
 * authors rushii, Samu, jamesbt365, Elvyra, Etorix
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "NoBlockedMessages.hpp"

#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

namespace Qord {
namespace Plugins {
namespace NoBlockedMessages {

namespace {
constexpr int AutoModerationActionType = 24;

struct Suppression
{
    bool suppressed = false;
    bool hide = false;
};

// true = keep (collapsed by Discord, shown by Qord), false = hide entirely
bool keepSuppressed(const Config &c, Id userId)
{
    return c.defaultHideUsers ? c.overrideUsers.contains(userId) : !c.overrideUsers.contains(userId);
}

bool suppressedUser(const Config &c, const Relationship &r)
{
    return r.blocked || (r.ignored && c.alsoHideIgnoredUsers);
}

// upstream isSuppressed(): the author, or for bot replies to a prefix
// command, the user who ran it
Suppression isSuppressed(const Config &c, Id authorId, bool authorIsBot, Id referencedAuthorId,
                         const RelationshipOf &relationshipOf)
{
    Suppression s;
    const bool prefixCommand = authorIsBot && referencedAuthorId;
    s.suppressed = suppressedUser(c, relationshipOf(authorId)) ||
                   (prefixCommand && suppressedUser(c, relationshipOf(referencedAuthorId)));
    s.hide = !keepSuppressed(c, authorId) || (prefixCommand && !keepSuppressed(c, referencedAuthorId));
    return s;
}
} // namespace

QSet<Id> parseOverrideUsers(const QString &text)
{
    QSet<Id> ids;
    for (const QString &part : text.split(u',')) {
        bool ok = false;
        const Id id = part.trimmed().toULongLong(&ok);
        if (ok && id)
            ids.insert(id);
    }
    return ids;
}

Verdict evaluate(const Config &c, const MessageContext &m, const RelationshipOf &relationshipOf)
{
    const Suppression own = isSuppressed(c, m.authorId, m.authorIsBot, m.referencedAuthorId, relationshipOf);
    if (m.type == AutoModerationActionType && c.allowAutoModMessages)
        return { true, own.suppressed };
    if (own.suppressed)
        return { !own.hide, true };
    if (c.hideBlockedUserReplies && m.referencedAuthorId) {
        // The replied-to message's own prefix-command author is not known here
        const Suppression reply = isSuppressed(c, m.referencedAuthorId, false, 0, relationshipOf);
        if (reply.suppressed)
            return { !reply.hide, true };
    }
    return { true, false };
}

} // namespace NoBlockedMessages

using namespace NoBlockedMessages;

void NoBlockedMessagesPlugin::start(PluginContext &ctx)
{
    context = &ctx;
    onSettingChanged({});
}

void NoBlockedMessagesPlugin::onSettingChanged(const QString &key)
{
    Q_UNUSED(key);
    PluginSettings &s = context->settings();
    config.alsoHideIgnoredUsers = s.boolValue(QStringLiteral("alsoHideIgnoredUsers"));
    config.disableNotifications = s.boolValue(QStringLiteral("disableNotifications"));
    config.allowAutoModMessages = s.boolValue(QStringLiteral("allowAutoModMessages"));
    config.hideBlockedUserReplies = s.boolValue(QStringLiteral("hideBlockedUserReplies"));
    config.defaultHideUsers = s.boolValue(QStringLiteral("defaultHideUsers"));
    config.overrideUsers = parseOverrideUsers(s.stringValue(QStringLiteral("overrideUsers")));
    if (MessageService *messages = context->messages())
        messages->refreshFilters();
}

Verdict NoBlockedMessagesPlugin::verdict(const MessageContext &m) const
{
    const RelationshipDirectory *relationships = context->relationships();
    if (!relationships)
        return {};
    return evaluate(config, m, [&](Id userId) {
        return Relationship { relationships->isBlocked(m.accountId, userId), relationships->isIgnored(m.accountId, userId) };
    });
}

bool NoBlockedMessagesPlugin::shouldHideMessage(const MessageContext &m)
{
    return !verdict(m).visible;
}

bool NoBlockedMessagesPlugin::shouldSilenceMessage(const MessageContext &m)
{
    return config.disableNotifications && verdict(m).suppressed;
}

QByteArray qord_manifest_noblockedmessages();

void qord_register_noblockedmessages(Registry &registry)
{
    registry.add({ QStringLiteral("noblockedmessages/plugin.json"),
                   [] { return std::make_unique<NoBlockedMessagesPlugin>(); }, qord_manifest_noblockedmessages() });
}

} // namespace Plugins
} // namespace Qord
