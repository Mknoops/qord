#include "PluginTestUtils.hpp"

#include "PermissionsViewer.hpp"

#include <QTest>

namespace Qord::Plugins {
void qord_register_permissionsviewer(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

namespace {
constexpr Id Guild = 100;
constexpr Id Owner = 7;
constexpr Id Ben = 8;
constexpr Id Eve = 9; // not a member
constexpr Id Admin = 201, Mod = 202, MemberRole = 203, Unknown = 999;
constexpr quint64 Kick = 1ULL << 1, Administrator = 1ULL << 3, View = 1ULL << 10, Send = 1ULL << 11,
                  ManageMessages = 1ULL << 13;

RoleInfo role(Id id, const QString &name, int position, quint64 permissions, quint32 color = 0)
{
    RoleInfo r;
    r.id = id;
    r.name = name;
    r.position = position;
    r.permissions = permissions;
    r.color = color;
    return r;
}

MemberInfo member(Id id, const QString &username, bool isMember, QList<Id> roles, const QString &nick = {})
{
    MemberInfo m;
    m.userId = id;
    m.username = username;
    m.isMember = isMember;
    m.roles = std::move(roles);
    m.nick = nick;
    m.avatarUrl = QStringLiteral("https://cdn.discordapp.com/avatars/%1/a.png").arg(id);
    return m;
}

PermissionOverwriteInfo overwrite(PermissionOverwriteInfo::Type type, Id id, quint64 allow, quint64 deny)
{
    PermissionOverwriteInfo o;
    o.type = type;
    o.id = id;
    o.allow = allow;
    o.deny = deny;
    return o;
}

void fill(FakeGuilds &guilds)
{
    GuildInfo guild;
    guild.id = Guild;
    guild.name = QStringLiteral("Qord Dev");
    guild.ownerId = Owner;
    guild.roles = { role(Admin, QStringLiteral("Admin"), 3, Administrator, 0xff0000),
                    role(Mod, QStringLiteral("Mod"), 2, Kick | ManageMessages, 0x00ff00),
                    role(MemberRole, QStringLiteral("Member"), 1, Send),
                    role(Guild, QStringLiteral("@everyone"), 0, View | Send) };
    guilds.guilds.insert(Guild, guild);
    guilds.members.insert(Owner, member(Owner, QStringLiteral("owner"), true, { Admin }));
    guilds.members.insert(Ben, member(Ben, QStringLiteral("ben"), true, { Mod, MemberRole }, QStringLiteral("Benny")));
    guilds.members.insert(Eve, member(Eve, QStringLiteral("eve"), false, {}));

    using T = PermissionOverwriteInfo::Type;
    ChannelInfo channel;
    channel.id = 300;
    channel.guildId = Guild;
    channel.name = QStringLiteral("general");
    channel.overwrites = { overwrite(T::Member, Eve, Send, 0), overwrite(T::Role, Guild, 0, Send),
                           overwrite(T::Role, Unknown, 0, View), overwrite(T::Role, Mod, ManageMessages, 0),
                           overwrite(T::Member, Ben, 0, Kick) };
    guilds.channels.insert(300, channel);
}

UserContext user(Id userId, Id guildId = Guild)
{
    UserContext u;
    u.accountId = 1;
    u.guildId = guildId;
    u.userId = userId;
    return u;
}

ChannelContext channel(Id channelId, Id guildId = Guild)
{
    ChannelContext c;
    c.accountId = 1;
    c.guildId = guildId;
    c.channelId = channelId;
    return c;
}

QStringList names(const PermissionsView &view)
{
    QStringList out;
    for (const auto &entry : view.entries)
        out.append(entry.name);
    return out;
}

QList<quint64> bits(const MemberPermissionsView &view)
{
    QList<quint64> out;
    for (const auto &p : view.permissions)
        out.append(p.bit);
    return out;
}
} // namespace

class TestPermissionsViewer : public QObject
{
    Q_OBJECT

private slots:
    void menusOnlyWhereUpstreamShowsThem()
    {
        SinglePluginHost h(qord_register_permissionsviewer);
        QVERIFY(h.info()->loadError.isEmpty());
        QVERIFY(h.enableWithAllPermissions());
        fill(h.guilds);

        QCOMPARE(h.host->dispatchUserContextMenuItems(user(Ben)).size(), 1);
        QCOMPARE(h.host->dispatchUserContextMenuItems(user(Ben)).value(0).item.label, QStringLiteral("View Permissions"));
        QVERIFY(h.host->dispatchUserContextMenuItems(user(Eve)).isEmpty()); // not a member
        QVERIFY(h.host->dispatchUserContextMenuItems(user(Ben, 0)).isEmpty()); // outside servers
        QCOMPARE(h.host->dispatchChannelContextMenuItems(channel(300)).size(), 1);
        QCOMPARE(h.host->dispatchChannelContextMenuItems(channel(0)).size(), 1); // the server menu
        QVERIFY(h.host->dispatchChannelContextMenuItems(channel(55, 0)).isEmpty()); // a DM
        QCOMPARE(h.host->dispatchProfileRoleButtons(user(Ben)).size(), 1);
        QVERIFY(h.host->dispatchProfileRoleButtons(user(Eve)).isEmpty());
    }

    void memberListsTheirRolesAndOwnership()
    {
        SinglePluginHost h(qord_register_permissionsviewer);
        QVERIFY(h.enableWithAllPermissions());
        fill(h.guilds);

        const QString pid = h.id;
        h.host->dispatchUserContextMenuAction(pid, QStringLiteral("perm-viewer-permissions"), user(Ben));
        QCOMPARE(h.ui.permissionViews.size(), 1);
        const PermissionsView ben = h.ui.permissionViews[0];
        QCOMPARE(ben.title, QStringLiteral("Benny Permissions")); // the nickname first
        QCOMPARE(names(ben), (QStringList { "Mod", "Member", "@everyone" }));
        QCOMPARE(ben.entries[0].permissions, std::optional<quint64>(Kick | ManageMessages));
        QCOMPARE(ben.entries[0].color, quint32(0x00ff00));
        QVERIFY(ben.entries[0].kind == PermissionsEntry::Kind::Role);

        h.host->dispatchUserContextMenuAction(pid, QStringLiteral("perm-viewer-permissions"), user(Owner));
        const PermissionsView owner = h.ui.permissionViews[1];
        QCOMPARE(owner.title, QStringLiteral("owner Permissions"));
        QCOMPARE(names(owner), (QStringList { "Admin", "@everyone", "@owner" }));
        QVERIFY(owner.entries[2].kind == PermissionsEntry::Kind::Owner);
        QCOMPARE(owner.entries[2].permissions, std::optional<quint64>(PermissionsViewerPlugin::AllPermissions));
    }

    void channelListsOverwritesRolesFirst()
    {
        SinglePluginHost h(qord_register_permissionsviewer);
        QVERIFY(h.enableWithAllPermissions());
        fill(h.guilds);

        h.host->dispatchChannelContextMenuAction(h.id, QStringLiteral("perm-viewer-permissions"), channel(300));
        QCOMPARE(h.ui.permissionViews.size(), 1);
        const PermissionsView view = h.ui.permissionViews[0];
        QCOMPARE(view.title, QStringLiteral("general Permissions"));
        // Roles by position (unknown ones last), then members in Discord's order
        QCOMPARE(names(view), (QStringList { "Mod", "@everyone", "Unknown Role", "eve", "ben" }));
        QVERIFY(!view.entries[0].permissions.has_value());
        QCOMPARE(view.entries[0].allow, ManageMessages);
        QCOMPARE(view.entries[1].deny, Send);
        QCOMPARE(view.entries[2].deny, View);
        QVERIFY(view.entries[3].kind == PermissionsEntry::Kind::Member);
        QCOMPARE(view.entries[3].allow, Send);
        QCOMPARE(view.entries[3].imageUrl, QStringLiteral("https://cdn.discordapp.com/avatars/9/a.png"));
        QCOMPARE(view.entries[4].deny, Kick);
        // Overwrite members who aren't loaded are asked for
        QCOMPARE(h.guilds.requested.size(), 1);
        QCOMPARE(h.guilds.requested[0].first, Guild);
        QCOMPARE(h.guilds.requested[0].second, QList<Id> { Eve });

        h.host->dispatchChannelContextMenuAction(h.id, QStringLiteral("perm-viewer-permissions"), channel(0));
        const PermissionsView server = h.ui.permissionViews[1];
        QCOMPARE(server.title, QStringLiteral("Qord Dev Permissions"));
        QCOMPARE(names(server), (QStringList { "Admin", "Mod", "Member", "@everyone" }));
    }

    void popoutShowsWhichRoleGrantsEachPermission()
    {
        SinglePluginHost h(qord_register_permissionsviewer);
        QVERIFY(h.enableWithAllPermissions());
        fill(h.guilds);

        h.host->dispatchProfileRoleButtonClicked(h.id, QStringLiteral("perm-viewer-role-button"), user(Ben));
        QCOMPARE(h.ui.memberPermissionViews.size(), 1);
        MemberPermissionsView view = h.ui.memberPermissionViews[0];
        QVERIFY(!view.lowestFirst);
        // Highest role first: Send comes from Member, not @everyone
        QCOMPARE(bits(view), (QList<quint64> { Kick, ManageMessages, Send, View }));
        QCOMPARE(view.permissions[2].roleName, QStringLiteral("Member"));
        QCOMPARE(view.permissions[3].roleName, QStringLiteral("@everyone"));
        QCOMPARE(view.permissions[0].roleColor, quint32(0x00ff00));

        // The sort toggle flips the setting and shows the popout again
        view.toggleSort();
        QCOMPARE(h.host->settings(h.id)->stringValue(QStringLiteral("permissionsSortOrder")), QStringLiteral("lowestRole"));
        QCOMPARE(h.ui.memberPermissionViews.size(), 2);
        view = h.ui.memberPermissionViews[1];
        QVERIFY(view.lowestFirst);
        QCOMPARE(bits(view), (QList<quint64> { Kick, ManageMessages, View, Send }));
        QCOMPARE(view.permissions[3].roleName, QStringLiteral("@everyone"));

        // Role Details opens the member's list
        view.showRoleDetails();
        QCOMPARE(h.ui.permissionViews.size(), 1);
        QCOMPARE(h.ui.permissionViews[0].title, QStringLiteral("Benny Permissions"));

        // The owner's ownership comes first
        h.host->dispatchProfileRoleButtonClicked(h.id, QStringLiteral("perm-viewer-role-button"), user(Owner));
        view = h.ui.memberPermissionViews.last();
        QCOMPARE(view.permissions.value(0).bit, quint64(0));
        QCOMPARE(view.permissions.value(0).roleName, QStringLiteral("Owner"));
        QCOMPARE(view.permissions.value(1).bit, Administrator);
    }

    void buttonsDoNothingAfterStop()
    {
        SinglePluginHost h(qord_register_permissionsviewer);
        QVERIFY(h.enableWithAllPermissions());
        fill(h.guilds);
        h.host->dispatchProfileRoleButtonClicked(h.id, QStringLiteral("perm-viewer-role-button"), user(Ben));
        QCOMPARE(h.ui.memberPermissionViews.size(), 1);
        const MemberPermissionsView view = h.ui.memberPermissionViews[0];
        QVERIFY(h.host->setEnabled(h.id, false));
        view.toggleSort();
        view.showRoleDetails();
        QCOMPARE(h.ui.memberPermissionViews.size(), 1);
        QVERIFY(h.ui.permissionViews.isEmpty());
    }

    void needsTheServerPermission()
    {
        SinglePluginHost h(qord_register_permissionsviewer);
        fill(h.guilds);
        h.host->setGranted(h.id, Permission::AddUiElements);
        QVERIFY(h.host->setEnabled(h.id, true));
        QVERIFY(h.host->dispatchUserContextMenuItems(user(Ben)).isEmpty());
        QVERIFY(h.host->dispatchChannelContextMenuItems(channel(300)).isEmpty());
        QVERIFY(h.host->dispatchProfileRoleButtons(user(Ben)).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestPermissionsViewer)
#include "tst_permissionsviewer.moc"
