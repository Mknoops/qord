#include "PermissionViews.hpp"

#include <QHash>

#include <algorithm>
#include <limits>

namespace Qord {
namespace Plugins {

PermissionsEntry roleEntry(const RoleInfo &role)
{
    PermissionsEntry entry;
    entry.kind = PermissionsEntry::Kind::Role;
    entry.id = role.id;
    entry.name = role.name;
    entry.color = role.color;
    entry.imageUrl = role.iconUrl;
    entry.emoji = role.unicodeEmoji;
    entry.permissions = role.permissions;
    return entry;
}

std::optional<PermissionsView> channelPermissionsView(GuildDirectory &guilds, Id accountId, Id channelId)
{
    const auto channel = guilds.channel(accountId, channelId);
    if (!channel || !channel->guildId)
        return std::nullopt;
    const auto guild = guilds.guild(accountId, channel->guildId);
    if (!guild)
        return std::nullopt;

    // Roles by position, highest first (unknown roles last), then members
    // in Discord's order, as upstream's list ends up after its type sort
    QHash<Id, const RoleInfo *> roles;
    for (const RoleInfo &role : guild->roles)
        roles.insert(role.id, &role);
    QList<PermissionOverwriteInfo> roleOverwrites;
    QList<PermissionOverwriteInfo> memberOverwrites;
    for (const auto &overwrite : channel->overwrites)
        (overwrite.type == PermissionOverwriteInfo::Type::Role ? roleOverwrites : memberOverwrites).append(overwrite);
    auto position = [&](Id id) {
        const RoleInfo *role = roles.value(id);
        return role ? qint64(role->position) : std::numeric_limits<qint64>::min();
    };
    std::stable_sort(roleOverwrites.begin(), roleOverwrites.end(),
                     [&](const auto &a, const auto &b) { return position(a.id) > position(b.id); });

    PermissionsView view;
    view.accountId = accountId;
    view.guildId = channel->guildId;
    view.title = channel->name + QStringLiteral(" Permissions");
    for (const auto &overwrite : roleOverwrites) {
        PermissionsEntry entry;
        if (const RoleInfo *role = roles.value(overwrite.id))
            entry = roleEntry(*role);
        else
            entry.name = QStringLiteral("Unknown Role");
        entry.kind = PermissionsEntry::Kind::Role;
        entry.id = overwrite.id;
        entry.permissions.reset();
        entry.allow = overwrite.allow;
        entry.deny = overwrite.deny;
        view.entries.append(entry);
    }
    QList<Id> missing;
    for (const auto &overwrite : memberOverwrites) {
        PermissionsEntry entry;
        entry.kind = PermissionsEntry::Kind::Member;
        entry.id = overwrite.id;
        entry.allow = overwrite.allow;
        entry.deny = overwrite.deny;
        const auto member = guilds.member(accountId, channel->guildId, overwrite.id);
        entry.name = member && !member->username.isEmpty() ? member->username : QStringLiteral("Unknown User");
        if (member)
            entry.imageUrl = member->avatarUrl;
        if (!member || !member->isMember)
            missing.append(overwrite.id);
        view.entries.append(entry);
    }
    if (!missing.isEmpty())
        guilds.requestMembers(accountId, channel->guildId, missing);
    return view;
}

} // namespace Plugins
} // namespace Qord
