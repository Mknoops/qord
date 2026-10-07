/*
 * Qord, a native Discord client
 * Port of Equicord's NoReplyMention plugin (src/plugins/noReplyMention)
 * Original: Copyright (c) 2022 Vendicated and contributors,
 * authors DustyAngel47, rae, pylix, outfoxxed
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "NoReplyMention.hpp"

#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

#include <QRegularExpression>

namespace Qord {
namespace Plugins {
namespace NoReplyMention {

QSet<Id> parseIdList(const QString &text)
{
    static const QRegularExpression separators(QStringLiteral("[,\\s]+"));
    QSet<Id> ids;
    for (const QString &part : text.split(separators, Qt::SkipEmptyParts)) {
        bool ok = false;
        const Id id = part.toULongLong(&ok);
        if (ok && id)
            ids.insert(id);
    }
    return ids;
}

bool shouldMention(const Config &config, bool listed, bool shiftHeld)
{
    const bool isExempt = config.shouldPingListed ? listed : !listed;
    return config.inverseShiftReply ? shiftHeld != isExempt : !shiftHeld && isExempt;
}

bool isListed(const Config &config, Id authorId, const std::optional<QList<Id>> &authorRoles)
{
    if (config.users.contains(authorId))
        return true;
    if (!authorRoles)
        return false;
    for (Id role : *authorRoles)
        if (config.roles.contains(role))
            return true;
    return false;
}

} // namespace NoReplyMention

void NoReplyMentionPlugin::start(PluginContext &ctx)
{
    context = &ctx;
    reload();
}

void NoReplyMentionPlugin::stop()
{
    context = nullptr;
}

void NoReplyMentionPlugin::onSettingChanged(const QString &key)
{
    Q_UNUSED(key);
    reload();
}

void NoReplyMentionPlugin::reload()
{
    PluginSettings &s = context->settings();
    config.users = NoReplyMention::parseIdList(s.stringValue(QStringLiteral("userList")));
    config.roles = NoReplyMention::parseIdList(s.stringValue(QStringLiteral("roleList")));
    config.shouldPingListed = s.boolValue(QStringLiteral("shouldPingListed"));
    config.inverseShiftReply = s.boolValue(QStringLiteral("inverseShiftReply"));
}

std::optional<bool> NoReplyMentionPlugin::shouldMentionOnReply(const ReplyContext &reply)
{
    std::optional<QList<Id>> roles;
    if (reply.guildId && !config.users.contains(reply.authorId) && !config.roles.isEmpty()) {
        if (const MemberDirectory *members = context->members())
            roles = members->memberRoles(reply.accountId, reply.guildId, reply.authorId);
    }
    const bool listed = NoReplyMention::isListed(config, reply.authorId, roles);
    return NoReplyMention::shouldMention(config, listed, reply.shiftHeld);
}

QByteArray qord_manifest_noreplymention();

void qord_register_noreplymention(Registry &registry)
{
    registry.add({ QStringLiteral("noreplymention/plugin.json"),
                   [] { return std::make_unique<NoReplyMentionPlugin>(); }, qord_manifest_noreplymention() });
}

} // namespace Plugins
} // namespace Qord
