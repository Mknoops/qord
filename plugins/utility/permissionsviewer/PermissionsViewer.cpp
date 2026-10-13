/*
 * Qord, a native Discord client
 * Port of Equicord's PermissionsViewer plugin (src/plugins/permissionsViewer)
 * Original: Copyright (c) Vendicated and contributors, authors Nuckyz, Ven
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "PermissionsViewer.hpp"

#include "Plugins/PermissionViews.hpp"
#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

#include <QSet>

#include <algorithm>
#include <limits>

namespace Qord {
namespace Plugins {

namespace {
const QString ItemId = QStringLiteral("perm-viewer-permissions");
const QString ButtonId = QStringLiteral("perm-viewer-role-button");

// Discord's SafetyIcon
const QByteArray ShieldSvg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\" viewBox=\"0 0 24 24\">"
        "<path fill=\"currentColor\" fill-rule=\"evenodd\" d=\"M4.27 5.22A2.66 2.66 0 0 0 3 7.5v2.3c0 5.6 3.3 "
        "10.68 8.42 12.95.37.17.79.17 1.16 0A14.18 14.18 0 0 0 21 9.78V7.5c0-.93-.48-1.78-1.27-2.27l-6.7-4.13a2 2 0 "
        "0 0-2.06 0L4.27 5.22ZM12 5a1 1 0 0 1 1 1v6a1 1 0 1 1-2 0V6a1 1 0 0 1 1-1Zm0 12a1.5 1.5 0 1 0 0-3 1.5 1.5 0 0 "
        "0 0 3Z\" clip-rule=\"evenodd\"/></svg>";

PermissionsEntry ownerEntry()
{
    PermissionsEntry entry;
    entry.kind = PermissionsEntry::Kind::Owner;
    entry.name = QStringLiteral("@owner");
    entry.permissions = PermissionsViewerPlugin::AllPermissions;
    return entry;
}

// The member's roles, highest first; the guild id is the @everyone role
QList<RoleInfo> memberRoles(const GuildInfo &guild, const MemberInfo &member)
{
    QList<RoleInfo> out;
    for (const RoleInfo &role : guild.roles)
        if (role.id == guild.id || member.roles.contains(role.id))
            out.append(role);
    return out;
}

QString memberName(const MemberInfo &member)
{
    return member.nick.isEmpty() ? member.username : member.nick;
}
} // namespace

bool PermissionsViewerPlugin::isMember(Id accountId, Id guildId, Id userId) const
{
    const GuildDirectory *guilds = context ? context->guilds() : nullptr;
    if (!guilds || !guildId)
        return false;
    const auto member = guilds->member(accountId, guildId, userId);
    return member && member->isMember;
}

std::optional<PermissionsView> PermissionsViewerPlugin::memberView(Id accountId, Id guildId, Id userId) const
{
    const GuildDirectory *guilds = context ? context->guilds() : nullptr;
    if (!guilds)
        return std::nullopt;
    const auto guild = guilds->guild(accountId, guildId);
    const auto member = guilds->member(accountId, guildId, userId);
    if (!guild || !member || !member->isMember)
        return std::nullopt;
    PermissionsView view;
    view.accountId = accountId;
    view.guildId = guildId;
    view.title = memberName(*member) + QStringLiteral(" Permissions");
    for (const RoleInfo &role : memberRoles(*guild, *member))
        view.entries.append(roleEntry(role));
    if (guild->ownerId == userId)
        view.entries.append(ownerEntry());
    return view;
}

std::optional<PermissionsView> PermissionsViewerPlugin::channelView(Id accountId, Id channelId) const
{
    GuildDirectory *guilds = context ? context->guilds() : nullptr;
    if (!guilds)
        return std::nullopt;
    return channelPermissionsView(*guilds, accountId, channelId);
}

std::optional<PermissionsView> PermissionsViewerPlugin::guildView(Id accountId, Id guildId) const
{
    const GuildDirectory *guilds = context ? context->guilds() : nullptr;
    if (!guilds)
        return std::nullopt;
    const auto guild = guilds->guild(accountId, guildId);
    if (!guild)
        return std::nullopt;
    PermissionsView view;
    view.accountId = accountId;
    view.guildId = guildId;
    view.title = guild->name + QStringLiteral(" Permissions");
    for (const RoleInfo &role : guild->roles)
        view.entries.append(roleEntry(role));
    return view;
}

std::optional<MemberPermissionsView> PermissionsViewerPlugin::memberPermissions(const UserContext &user)
{
    const GuildDirectory *guilds = context ? context->guilds() : nullptr;
    if (!guilds)
        return std::nullopt;
    const auto guild = guilds->guild(user.accountId, user.guildId);
    const auto member = guilds->member(user.accountId, user.guildId, user.userId);
    if (!guild || !member || !member->isMember)
        return std::nullopt;

    struct Granted
    {
        GrantedPermission permission;
        qint64 position;
    };
    QList<Granted> granted;
    if (guild->ownerId == user.userId)
        granted.append({ { 0, QStringLiteral("Owner"), 0 }, std::numeric_limits<qint64>::max() });

    const bool lowestFirst = context->settings().stringValue(QStringLiteral("permissionsSortOrder")) ==
                             QLatin1String("lowestRole");
    QList<RoleInfo> roles = memberRoles(*guild, *member);
    std::stable_sort(roles.begin(), roles.end(), [lowestFirst](const RoleInfo &a, const RoleInfo &b) {
        return lowestFirst ? a.position < b.position : a.position > b.position;
    });
    for (int bit = 0; bit < 53; ++bit) {
        const quint64 flag = quint64(1) << bit;
        for (const RoleInfo &role : roles) {
            if ((role.permissions & flag) == flag) {
                granted.append({ { flag, role.name, role.color }, role.position });
                break;
            }
        }
    }
    std::stable_sort(granted.begin(), granted.end(),
                     [](const Granted &a, const Granted &b) { return a.position > b.position; });

    MemberPermissionsView view;
    view.lowestFirst = lowestFirst;
    for (const Granted &g : granted)
        view.permissions.append(g.permission);
    view.toggleSort = [this, user, lowestFirst]() {
        if (!context)
            return;
        context->settings().setValue(QStringLiteral("permissionsSortOrder"),
                                     lowestFirst ? QStringLiteral("highestRole") : QStringLiteral("lowestRole"));
        onProfileRoleButtonClicked(ButtonId, user);
    };
    view.showRoleDetails = [this, user]() {
        UiService *ui = context ? context->ui() : nullptr;
        if (!ui)
            return;
        if (const auto details = memberView(user.accountId, user.guildId, user.userId))
            ui->showPermissions(*details);
    };
    return view;
}

QList<MenuItem> PermissionsViewerPlugin::userContextMenuItems(const UserContext &user)
{
    if (!isMember(user.accountId, user.guildId, user.userId))
        return {};
    return { { ItemId, QStringLiteral("View Permissions"), {} } };
}

void PermissionsViewerPlugin::onUserContextMenuAction(const QString &id, const UserContext &user)
{
    UiService *ui = context ? context->ui() : nullptr;
    if (id != ItemId || !ui)
        return;
    if (const auto view = memberView(user.accountId, user.guildId, user.userId))
        ui->showPermissions(*view);
}

QList<MenuItem> PermissionsViewerPlugin::channelContextMenuItems(const ChannelContext &channel)
{
    if (!channel.guildId || !context || !context->guilds())
        return {};
    return { { ItemId, QStringLiteral("View Permissions"), {} } };
}

void PermissionsViewerPlugin::onChannelContextMenuAction(const QString &id, const ChannelContext &channel)
{
    UiService *ui = context ? context->ui() : nullptr;
    if (id != ItemId || !ui)
        return;
    const auto view = channel.channelId ? channelView(channel.accountId, channel.channelId)
                                        : guildView(channel.accountId, channel.guildId);
    if (view)
        ui->showPermissions(*view);
}

QList<ChatBarButton> PermissionsViewerPlugin::profileRoleButtons(const UserContext &user)
{
    if (!isMember(user.accountId, user.guildId, user.userId))
        return {};
    return { { ButtonId, QStringLiteral("View Permissions"), ShieldSvg, false } };
}

void PermissionsViewerPlugin::onProfileRoleButtonClicked(const QString &id, const UserContext &user)
{
    UiService *ui = context ? context->ui() : nullptr;
    if (id != ButtonId || !ui)
        return;
    if (const auto view = memberPermissions(user))
        ui->showMemberPermissions(*view);
}

QByteArray qord_manifest_permissionsviewer();

void qord_register_permissionsviewer(Registry &registry)
{
    registry.add({ QStringLiteral("permissionsviewer/plugin.json"),
                   [] { return std::make_unique<PermissionsViewerPlugin>(); }, qord_manifest_permissionsviewer() });
}

} // namespace Plugins
} // namespace Qord
