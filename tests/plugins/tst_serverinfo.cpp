#include "PluginTestUtils.hpp"

#include "ServerInfo.hpp"

#include <QTest>
#include <QTimeZone>

namespace Qord::Plugins {
void qord_register_serverinfo(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

namespace {
constexpr Id Self = 1;
constexpr Id Guild = 100, Second = 200, Third = 300;

MemberInfo member(Id id, const QString &username, const QString &globalName, bool isMember = true)
{
    MemberInfo m;
    m.userId = id;
    m.username = username;
    m.globalName = globalName;
    m.isMember = isMember;
    m.avatarUrl = QStringLiteral("https://cdn.discordapp.com/avatars/%1/a.png").arg(id);
    return m;
}

GuildInfo guild(Id id, const QString &name)
{
    GuildInfo g;
    g.id = id;
    g.name = name;
    return g;
}

void fill(SinglePluginHost &h)
{
    GuildInfo g = guild(Guild, QStringLiteral("Qord Dev"));
    g.ownerId = 7;
    g.description = QStringLiteral("Mock server");
    g.iconUrl = QStringLiteral("https://cdn.discordapp.com/icons/100/g1.png");
    RoleInfo everyone;
    everyone.id = Guild;
    g.roles = { RoleInfo(), RoleInfo(), everyone };
    g.vanityUrlCode = QStringLiteral("qord");
    g.preferredLocale = QStringLiteral("en-US");
    g.verificationLevel = 1;
    g.premiumSubscriptionCount = 9;
    g.premiumTier = 2;
    g.joinedAt = QDateTime(QDate(2026, 9, 5), QTime(8, 30), QTimeZone::UTC);
    g.channelCount = 9;
    h.guilds.guilds.insert(Guild, g);
    h.guilds.guilds.insert(Second, guild(Second, QStringLiteral("Second Server")));
    h.guilds.guilds.insert(Third, guild(Third, QStringLiteral("Third")));
    h.guilds.members.insert(7, member(7, QStringLiteral("owner"), QStringLiteral("Owner")));
    h.guilds.members.insert(8, member(8, QStringLiteral("benji"), QStringLiteral("Ben")));
    h.guilds.members.insert(9, member(9, QStringLiteral("ava"), QStringLiteral("Ava")));
    h.guilds.members.insert(10, member(10, QStringLiteral("zed"), QString()));
    h.guilds.members.insert(11, member(11, QStringLiteral("eve"), QStringLiteral("Eve"), false));
    h.guilds.guildOrder = { Guild, Second, Third };
    h.guilds.guildMembers = { { Guild, { Self, 7, 8, 9, 10 } }, { Second, { Self, 8, 9 } }, { Third, { Self, 8 } } };
    h.relationships.friends = { 8, 9, 11 };
    h.relationships.blocked = { 10 };
}

ChannelContext server(Id guildId = Guild)
{
    ChannelContext c;
    c.accountId = Self;
    c.guildId = guildId;
    return c;
}

QStringList names(const ServerInfoView::Tab &tab)
{
    QStringList out;
    for (const auto &user : tab.users)
        out.append(user.name);
    return out;
}

QString field(const ServerInfoView &view, const QString &label)
{
    for (const auto &f : view.fields)
        if (f.label == label)
            return f.time.isValid() ? f.time.toString(Qt::ISODate) : f.text;
    return QStringLiteral("<missing>");
}
} // namespace

class TestServerInfo : public QObject
{
    Q_OBJECT

private slots:
    void acronymsLikeDiscord()
    {
        QCOMPARE(ServerInfoPlugin::acronym(QStringLiteral("Qord Dev")), QStringLiteral("QD"));
        QCOMPARE(ServerInfoPlugin::acronym(QStringLiteral("Bob's Server")), QStringLiteral("BS"));
        QCOMPARE(ServerInfoPlugin::acronym(QStringLiteral("the  big-room")), QStringLiteral("tb-r"));
    }

    void menuOnlyForServers()
    {
        SinglePluginHost h(qord_register_serverinfo);
        QVERIFY(h.info()->loadError.isEmpty());
        QVERIFY(h.enableWithAllPermissions());
        fill(h);
        QCOMPARE(h.host->dispatchChannelContextMenuItems(server()).value(0).item.label, QStringLiteral("Server Info"));
        ChannelContext channel = server();
        channel.channelId = 5;
        QVERIFY(h.host->dispatchChannelContextMenuItems(channel).isEmpty());
        QVERIFY(h.host->dispatchChannelContextMenuItems(server(0)).isEmpty()); // DMs
    }

    void detailsAndLists()
    {
        SinglePluginHost h(qord_register_serverinfo);
        QVERIFY(h.enableWithAllPermissions());
        fill(h);
        h.host->dispatchChannelContextMenuAction(h.id, QStringLiteral("vc-server-info"), server());
        QCOMPARE(h.ui.serverInfoViews.size(), 1);
        const ServerInfoView view = h.ui.serverInfoViews[0];
        QCOMPARE(view.name, QStringLiteral("Qord Dev"));
        QCOMPARE(view.acronym, QStringLiteral("QD"));
        QCOMPARE(view.description, QStringLiteral("Mock server"));

        QCOMPARE(view.fields[0].label, QStringLiteral("Server Owner"));
        QCOMPARE(view.fields[0].text, QStringLiteral("@Owner"));
        QCOMPARE(view.fields[0].userId, Id(7));
        QCOMPARE(field(view, QStringLiteral("Created At")), QStringLiteral("2015-01-01T00:00:00Z"));
        QCOMPARE(field(view, QStringLiteral("Joined At")), QStringLiteral("2026-09-05T08:30:00Z"));
        QCOMPARE(field(view, QStringLiteral("Vanity Link")), QStringLiteral("discord.gg/qord"));
        QCOMPARE(field(view, QStringLiteral("Preferred Locale")), QStringLiteral("en-US"));
        QCOMPARE(field(view, QStringLiteral("Verification Level")), QStringLiteral("Low"));
        QCOMPARE(field(view, QStringLiteral("Server Boosts")), QStringLiteral("9 (Level 2)"));
        QCOMPARE(field(view, QStringLiteral("Channels")), QStringLiteral("9"));
        QCOMPARE(field(view, QStringLiteral("Roles")), QStringLiteral("2"));

        QCOMPARE(view.tabs.size(), 4);
        QCOMPARE(view.tabs[0].title, QStringLiteral("Friends (2)")); // Eve isn't a member
        QCOMPARE(names(view.tabs[0]), (QStringList { "Ava", "Ben" }));
        QCOMPARE(view.tabs[0].users[1].username, QStringLiteral("benji"));
        // Owner and zed share only this server; you are left out
        QCOMPARE(view.tabs[1].title, QStringLiteral("Mutual Users (2)"));
        QCOMPARE(names(view.tabs[1]), (QStringList { "Ava", "Ben" }));
        QCOMPARE(view.tabs[1].users[1].mutualServers, 3);
        QCOMPARE(view.tabs[1].users[1].mutualServerIcons.size(), 3);
        QCOMPARE(view.tabs[1].users[1].mutualServerIcons[1].name, QStringLiteral("Second Server"));
        QCOMPARE(view.tabs[2].title, QStringLiteral("Blocked Users (1)"));
        QCOMPARE(names(view.tabs[2]), QStringList { "zed" });
        QCOMPARE(view.tabs[3].title, QStringLiteral("Ignored Users (0)"));
        // The friend who isn't loaded as a member is asked for
        QCOMPARE(h.guilds.requested.size(), 1);
        QCOMPARE(h.guilds.requested[0].second, QList<Id> { 11 });
    }

    void sortingSetting()
    {
        SinglePluginHost h(qord_register_serverinfo);
        QVERIFY(h.enableWithAllPermissions());
        fill(h);
        h.host->settings(h.id)->setValue(QStringLiteral("sorting"), QStringLiteral("none"));
        h.host->dispatchChannelContextMenuAction(h.id, QStringLiteral("vc-server-info"), server());
        // Unsorted: most shared servers first
        QCOMPARE(names(h.ui.serverInfoViews[0].tabs[1]), (QStringList { "Ben", "Ava" }));
        h.host->settings(h.id)->setValue(QStringLiteral("sorting"), QStringLiteral("username"));
        h.relationships.friends = { 8, 9, 10 };
        h.host->dispatchChannelContextMenuAction(h.id, QStringLiteral("vc-server-info"), server());
        QCOMPARE(names(h.ui.serverInfoViews[1].tabs[0]), (QStringList { "Ava", "Ben", "zed" }));
    }

    void unknownValues()
    {
        SinglePluginHost h(qord_register_serverinfo);
        QVERIFY(h.enableWithAllPermissions());
        fill(h);
        h.guilds.guilds[Guild].vanityUrlCode.clear();
        h.guilds.guilds[Guild].joinedAt = {};
        h.guilds.guilds[Guild].channelCount = -1;
        h.guilds.guilds[Guild].verificationLevel = -1;
        h.host->dispatchChannelContextMenuAction(h.id, QStringLiteral("vc-server-info"), server());
        const ServerInfoView view = h.ui.serverInfoViews[0];
        QCOMPARE(field(view, QStringLiteral("Vanity Link")), QStringLiteral("-"));
        QCOMPARE(field(view, QStringLiteral("Joined At")), QStringLiteral("-"));
        QCOMPARE(field(view, QStringLiteral("Channels")), QStringLiteral("?"));
        QCOMPARE(field(view, QStringLiteral("Verification Level")), QStringLiteral("?"));
    }
};

QTEST_GUILESS_MAIN(TestServerInfo)
#include "tst_serverinfo.moc"
