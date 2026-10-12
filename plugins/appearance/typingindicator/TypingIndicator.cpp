/*
 * Qord, a native Discord client
 * Port of Equicord's TypingIndicator plugin (src/plugins/typingIndicator)
 * Original: Copyright (c) Vendicated and contributors, authors Nuckyz, fawn, Sqaaakoi
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "TypingIndicator.hpp"

#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

namespace Qord {
namespace Plugins {

QString TypingIndicatorPlugin::tooltip(const QStringList &names)
{
    switch (names.size()) {
    case 0:
        return {};
    case 1:
        return QStringLiteral("%1 is typing...").arg(names[0]);
    case 2:
        return QStringLiteral("%1 and %2 are typing...").arg(names[0], names[1]);
    case 3:
        return QStringLiteral("%1, %2, and %3 are typing...").arg(names[0], names[1], names[2]);
    default:
        return QStringLiteral("Several people are typing...");
    }
}

QString TypingIndicatorPlugin::displayName(Id accountId, Id guildId, Id userId) const
{
    const GuildDirectory *guilds = context ? context->guilds() : nullptr;
    const auto member = guilds ? guilds->member(accountId, guildId, userId) : std::nullopt;
    if (!member)
        return QStringLiteral("Unknown User");
    if (!member->nick.isEmpty())
        return member->nick;
    return member->globalName.isEmpty() ? member->username : member->globalName;
}

std::optional<TypingIndicator> TypingIndicatorPlugin::channelTypingIndicator(const ChannelTypingContext &channel)
{
    if (!context)
        return std::nullopt;
    const PluginSettings &settings = context->settings();
    if (!settings.boolValue(QStringLiteral("includeMutedChannels")) && channel.muted)
        return std::nullopt;
    if (!settings.boolValue(QStringLiteral("includeCurrentChannel")) && channel.selected)
        return std::nullopt;

    const RelationshipDirectory *relationships = context->relationships();
    const bool blocked = settings.boolValue(QStringLiteral("includeBlockedUsers"));
    const bool ignored = settings.boolValue(QStringLiteral("includeIgnoredUsers"));
    QList<Id> typers;
    for (Id id : channel.typers) {
        if (id == channel.accountId)
            continue;
        if (relationships && !blocked && relationships->isBlocked(channel.accountId, id))
            continue;
        if (relationships && !ignored && relationships->isIgnored(channel.accountId, id))
            continue;
        typers.append(id);
    }
    if (typers.isEmpty())
        return std::nullopt;

    // Beyond three only the count matters ("Several people")
    QStringList names;
    for (Id id : typers)
        names.append(names.size() < 3 ? displayName(channel.accountId, channel.guildId, id) : QString());
    TypingIndicator out;
    out.tooltip = tooltip(names);
    const QString mode = settings.stringValue(QStringLiteral("indicatorMode"));
    if (mode != QLatin1String("dots"))
        out.avatars = typers;
    out.dots = mode != QLatin1String("avatars");
    return out;
}

QByteArray qord_manifest_typingindicator();

void qord_register_typingindicator(Registry &registry)
{
    registry.add({ QStringLiteral("typingindicator/plugin.json"),
                   [] { return std::make_unique<TypingIndicatorPlugin>(); }, qord_manifest_typingindicator() });
}

} // namespace Plugins
} // namespace Qord
