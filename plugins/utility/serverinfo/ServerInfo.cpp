/*
 * Qord, a native Discord client
 * Port of Equicord's ServerInfo plugin (src/plugins/serverInfo)
 * Original: Copyright (c) Vendicated and contributors, authors Ven, Nuckyz, Z1xus
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ServerInfo.hpp"

#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

#include <QRegularExpression>
#include <QSet>
#include <QTimeZone>

#include <algorithm>

namespace Qord {
namespace Plugins {

namespace {
const QString ItemId = QStringLiteral("vc-server-info");
constexpr Id DiscordEpochMs = 1420070400000ULL;
constexpr int MaxMutualIcons = 3;

QString displayName(const MemberInfo &member)
{
    return member.globalName.isEmpty() ? member.username : member.globalName;
}

// Upstream's comparators for the "sorting" setting
void sortUsers(QList<ServerInfoUser> &users, const QHash<Id, MemberInfo> &info, const QString &sorting)
{
    if (sorting == QLatin1String("none"))
        return;
    std::stable_sort(users.begin(), users.end(), [&](const ServerInfoUser &a, const ServerInfoUser &b) {
        const MemberInfo &ma = info[a.userId];
        const MemberInfo &mb = info[b.userId];
        if (sorting == QLatin1String("username"))
            return QString::localeAwareCompare(ma.username, mb.username) < 0;
        const QString other = mb.globalName.isEmpty() ? mb.username : mb.globalName;
        int cmp = ma.globalName.isEmpty() ? 0 : QString::localeAwareCompare(ma.globalName, other);
        if (cmp == 0)
            cmp = QString::localeAwareCompare(ma.username, other);
        return cmp < 0;
    });
}

ServerInfoUser userRow(const MemberInfo &member)
{
    ServerInfoUser user;
    user.userId = member.userId;
    user.name = displayName(member);
    user.username = member.username;
    user.avatarUrl = member.avatarUrl;
    return user;
}
} // namespace

QString ServerInfoPlugin::acronym(const QString &name)
{
    QString out = name;
    out.replace(QStringLiteral("'s "), QStringLiteral(" "));
    static const QRegularExpression word(QStringLiteral("\\w+")); // ASCII, as in JavaScript
    QString result;
    int last = 0;
    auto it = word.globalMatch(out);
    while (it.hasNext()) {
        const auto match = it.next();
        result += out.mid(last, match.capturedStart() - last);
        result += match.captured().left(1);
        last = match.capturedEnd();
    }
    result += out.mid(last);
    result.remove(QRegularExpression(QStringLiteral("\\s")));
    return result;
}

QList<MenuItem> ServerInfoPlugin::channelContextMenuItems(const ChannelContext &channel)
{
    if (channel.channelId || !channel.guildId || !context || !context->guilds())
        return {};
    return { { ItemId, QStringLiteral("Server Info"), {} } };
}

void ServerInfoPlugin::onChannelContextMenuAction(const QString &id, const ChannelContext &channel)
{
    UiService *ui = context ? context->ui() : nullptr;
    if (id != ItemId || !ui)
        return;
    if (const auto info = view(channel.accountId, channel.guildId))
        ui->showServerInfo(*info);
}

std::optional<ServerInfoView> ServerInfoPlugin::view(Id accountId, Id guildId)
{
    GuildDirectory *guilds = context ? context->guilds() : nullptr;
    if (!guilds)
        return std::nullopt;
    const auto guild = guilds->guild(accountId, guildId);
    if (!guild)
        return std::nullopt;
    const QString sorting = context->settings().stringValue(QStringLiteral("sorting"));

    ServerInfoView out;
    out.accountId = accountId;
    out.guildId = guildId;
    out.name = guild->name;
    out.description = guild->description;
    out.iconUrl = guild->iconUrl;
    out.bannerUrl = guild->bannerUrl;
    out.acronym = acronym(guild->name);

    QList<Id> missing;
    auto add = [&](const QString &label, const QString &text, const QDateTime &time = {}) {
        out.fields.append({ label, text, time, 0, {} });
    };
    ServerInfoView::Field owner { QStringLiteral("Server Owner"), QStringLiteral("Unknown User"), {}, guild->ownerId, {} };
    if (const auto info = guilds->member(accountId, guildId, guild->ownerId)) {
        owner.text = QLatin1Char('@') + (info->nick.isEmpty() ? displayName(*info) : info->nick);
        owner.avatarUrl = info->avatarUrl;
        if (!info->isMember)
            missing.append(guild->ownerId);
    } else if (guild->ownerId) {
        missing.append(guild->ownerId);
    }
    out.fields.append(owner);
    add(QStringLiteral("Created At"), {},
        QDateTime::fromMSecsSinceEpoch(qint64((guildId >> 22) + DiscordEpochMs)).toUTC());
    add(QStringLiteral("Joined At"), guild->joinedAt.isValid() ? QString() : QStringLiteral("-"), guild->joinedAt);
    add(QStringLiteral("Vanity Link"),
        guild->vanityUrlCode.isEmpty() ? QStringLiteral("-") : QStringLiteral("discord.gg/") + guild->vanityUrlCode);
    add(QStringLiteral("Preferred Locale"), guild->preferredLocale.isEmpty() ? QStringLiteral("-") : guild->preferredLocale);
    static const QStringList levels = { QStringLiteral("None"), QStringLiteral("Low"), QStringLiteral("Medium"),
                                        QStringLiteral("High"), QStringLiteral("Highest") };
    add(QStringLiteral("Verification Level"), levels.value(guild->verificationLevel, QStringLiteral("?")));
    add(QStringLiteral("Server Boosts"),
        QStringLiteral("%1 (Level %2)").arg(guild->premiumSubscriptionCount).arg(guild->premiumTier));
    add(QStringLiteral("Channels"), guild->channelCount > 0 ? QString::number(guild->channelCount) : QStringLiteral("?"));
    add(QStringLiteral("Roles"), QString::number(qMax(0, int(guild->roles.size()) - 1))); // without @everyone

    // Friends, blocked and ignored users who are members
    const RelationshipDirectory *relationships = context->relationships();
    auto relationshipTab = [&](const QString &title, RelationshipDirectory::Kind kind) {
        ServerInfoView::Tab tab;
        QHash<Id, MemberInfo> info;
        const QList<Id> ids = relationships ? relationships->relationshipIds(accountId, kind) : QList<Id>();
        for (Id id : ids) {
            const auto member = guilds->member(accountId, guildId, id);
            if (member && member->isMember) {
                info.insert(id, *member);
                tab.users.append(userRow(*member));
            } else if (!missing.contains(id)) {
                missing.append(id);
            }
        }
        sortUsers(tab.users, info, sorting);
        tab.title = QStringLiteral("%1 (%2)").arg(title).arg(tab.users.size());
        return tab;
    };
    const ServerInfoView::Tab friends = relationshipTab(QStringLiteral("Friends"), RelationshipDirectory::Kind::Friend);

    // Members who share other servers with you, most shared first
    QList<QPair<Id, QSet<Id>>> servers;
    for (Id id : guilds->guildIds(accountId)) {
        const QList<Id> members = guilds->memberIds(accountId, id);
        servers.append({ id, QSet<Id>(members.begin(), members.end()) });
    }
    ServerInfoView::Tab mutual;
    QHash<Id, MemberInfo> mutualInfo;
    for (Id userId : guilds->memberIds(accountId, guildId)) {
        if (userId == accountId)
            continue;
        ServerInfoUser user;
        QList<Id> shared;
        for (const auto &server : servers)
            if (server.second.contains(userId))
                shared.append(server.first);
        if (shared.size() <= 1)
            continue;
        const auto member = guilds->member(accountId, guildId, userId);
        if (!member)
            continue;
        user = userRow(*member);
        user.mutualServers = int(shared.size());
        for (Id sharedId : shared.mid(0, MaxMutualIcons))
            if (const auto server = guilds->guild(accountId, sharedId))
                user.mutualServerIcons.append({ server->name, server->iconUrl, acronym(server->name) });
        mutualInfo.insert(userId, *member);
        mutual.users.append(user);
    }
    std::stable_sort(mutual.users.begin(), mutual.users.end(),
                     [](const ServerInfoUser &a, const ServerInfoUser &b) { return a.mutualServers > b.mutualServers; });
    sortUsers(mutual.users, mutualInfo, sorting);
    mutual.title = QStringLiteral("Mutual Users (%1)").arg(mutual.users.size());

    out.tabs = { friends, mutual, relationshipTab(QStringLiteral("Blocked Users"), RelationshipDirectory::Kind::Blocked),
                 relationshipTab(QStringLiteral("Ignored Users"), RelationshipDirectory::Kind::Ignored) };
    if (!missing.isEmpty())
        guilds->requestMembers(accountId, guildId, missing);
    return out;
}

QByteArray qord_manifest_serverinfo();

void qord_register_serverinfo(Registry &registry)
{
    registry.add({ QStringLiteral("serverinfo/plugin.json"), [] { return std::make_unique<ServerInfoPlugin>(); },
                   qord_manifest_serverinfo() });
}

} // namespace Plugins
} // namespace Qord
