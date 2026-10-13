#include "PluginTestUtils.hpp"

#include "ShowHiddenChannels.hpp"

#include <QTest>
#include <QTimeZone>

namespace Qord::Plugins {
void qord_register_showhiddenchannels(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

namespace {
constexpr Id Guild = 100;
constexpr Id Owner = 7;
constexpr Id Ben = 8;
constexpr Id Eve = 9;
constexpr Id Admin = 201, Mod = 202, Staff = 203;
constexpr quint64 View = 1ULL << 10, Send = 1ULL << 11, Connect = 1ULL << 20, Administrator = 1ULL << 3;
using T = PermissionOverwriteInfo::Type;

RoleInfo role(Id id, const QString &name, int position, quint64 permissions)
{
    RoleInfo r;
    r.id = id;
    r.name = name;
    r.position = position;
    r.permissions = permissions;
    return r;
}

PermissionOverwriteInfo overwrite(T type, Id id, quint64 allow, quint64 deny)
{
    PermissionOverwriteInfo o;
    o.type = type;
    o.id = id;
    o.allow = allow;
    o.deny = deny;
    return o;
}

MemberInfo member(Id id, const QString &username, bool isMember)
{
    MemberInfo m;
    m.userId = id;
    m.username = username;
    m.isMember = isMember;
    return m;
}

GuildInfo guild()
{
    GuildInfo g;
    g.id = Guild;
    g.name = QStringLiteral("Qord Dev");
    g.ownerId = Owner;
    g.roles = { role(Admin, QStringLiteral("Admin"), 3, Administrator), role(Mod, QStringLiteral("Mod"), 2, Send),
                role(Staff, QStringLiteral("Staff"), 1, Send), role(Guild, QStringLiteral("@everyone"), 0, View | Send | Connect) };
    return g;
}

ChannelInfo staffChannel()
{
    ChannelInfo c;
    c.id = 300;
    c.guildId = Guild;
    c.name = QStringLiteral("staff-only");
    c.type = 0;
    c.topic = QStringLiteral("Staff talk");
    c.lastMessageId = (quint64(1000) * 1000) << 22; // 1000 s after Discord's epoch
    c.rateLimitPerUser = 3605;
    c.overwrites = { overwrite(T::Role, Guild, 0, View), overwrite(T::Role, Staff, View, 0),
                     overwrite(T::Member, Ben, View, 0), overwrite(T::Member, Eve, 0, View) };
    return c;
}

void fill(FakeGuilds &guilds)
{
    guilds.guilds.insert(Guild, guild());
    guilds.channels.insert(300, staffChannel());
    ChannelInfo voice;
    voice.id = 301;
    voice.guildId = Guild;
    voice.name = QStringLiteral("Stage");
    voice.type = 2;
    voice.bitrate = 64000;
    voice.hasRtcRegion = true;
    voice.overwrites = { overwrite(T::Role, Guild, 0, Connect), overwrite(T::Role, Mod, Connect, 0) };
    guilds.channels.insert(301, voice);
    ChannelInfo forum;
    forum.id = 302;
    forum.guildId = Guild;
    forum.name = QStringLiteral("help");
    forum.type = 15;
    forum.topic = QStringLiteral("Be precise");
    forum.defaultAutoArchiveDuration = 10080;
    forum.defaultForumLayout = 2;
    forum.defaultSortOrder = 1;
    forum.defaultReactionEmojiName = QStringLiteral("👍");
    forum.flags = 1 << 4;
    forum.availableTags = { QStringLiteral("bug"), QStringLiteral("question") };
    forum.overwrites = { overwrite(T::Role, Guild, 0, View) };
    guilds.channels.insert(302, forum);
    guilds.members.insert(Owner, member(Owner, QStringLiteral("owner"), true));
    guilds.members.insert(Ben, member(Ben, QStringLiteral("ben"), false));
}

HiddenChannelContext hidden(Id channelId, bool canView = false)
{
    HiddenChannelContext c;
    c.accountId = 1;
    c.guildId = Guild;
    c.channelId = channelId;
    c.canView = canView;
    return c;
}

QStringList texts(const LockScreen &screen)
{
    QStringList out;
    for (const auto &detail : screen.details)
        out.append(detail.text);
    return out;
}

QStringList names(const QList<PermissionsEntry> &entries)
{
    QStringList out;
    for (const auto &entry : entries)
        out.append(entry.name);
    return out;
}
} // namespace

class TestShowHiddenChannels : public QObject
{
    Q_OBJECT

private slots:
    void durationsReadLikeUpstream()
    {
        using U = ShowHiddenChannelsPlugin::Unit;
        QCOMPARE(ShowHiddenChannelsPlugin::formatDuration(5, U::Seconds), QStringLiteral("5 seconds"));
        QCOMPARE(ShowHiddenChannelsPlugin::formatDuration(1, U::Seconds), QStringLiteral("1 second"));
        QCOMPARE(ShowHiddenChannelsPlugin::formatDuration(3605, U::Seconds), QStringLiteral("1 hour, 0 minutes and 5 seconds"));
        QCOMPARE(ShowHiddenChannelsPlugin::formatDuration(21600, U::Seconds), QStringLiteral("6 hours"));
        QCOMPARE(ShowHiddenChannelsPlugin::formatDuration(60, U::Minutes), QStringLiteral("1 hour"));
        QCOMPARE(ShowHiddenChannelsPlugin::formatDuration(1440, U::Minutes), QStringLiteral("1 day"));
        QCOMPARE(ShowHiddenChannelsPlugin::formatDuration(4320, U::Minutes), QStringLiteral("3 days"));
        QCOMPARE(ShowHiddenChannelsPlugin::formatDuration(10080, U::Minutes), QStringLiteral("1 week"));
        QCOMPARE(ShowHiddenChannelsPlugin::formatDuration(0, U::Seconds), QStringLiteral("0 seconds"));
    }

    void displayFollowsSettings()
    {
        SinglePluginHost h(qord_register_showhiddenchannels);
        QVERIFY(h.info()->loadError.isEmpty());
        QVERIFY(!h.host->dispatchHiddenChannelDisplay().has_value()); // disabled: Discord's behaviour
        QVERIFY(h.enableWithAllPermissions());
        auto display = h.host->dispatchHiddenChannelDisplay();
        QVERIFY(display.has_value());
        QVERIFY(display->icon == HiddenChannelDisplay::Icon::Lock);
        QVERIFY(display->style == HiddenChannelDisplay::Style::Classic);
        h.host->settings(h.id)->setValue(QStringLiteral("showMode"), QStringLiteral("eyeIconRight"));
        h.host->settings(h.id)->setValue(QStringLiteral("channelStyle"), QStringLiteral("mutedUnread"));
        display = h.host->dispatchHiddenChannelDisplay();
        QVERIFY(display->icon == HiddenChannelDisplay::Icon::EyeRight);
        QVERIFY(display->style == HiddenChannelDisplay::Style::MutedUnread);
    }

    void textChannelLockScreen()
    {
        SinglePluginHost h(qord_register_showhiddenchannels);
        QVERIFY(h.enableWithAllPermissions());
        fill(h.guilds);

        const auto screen = h.host->dispatchHiddenChannelLockScreen(hidden(300));
        QVERIFY(screen.has_value());
        QCOMPARE(screen->heading, QStringLiteral("This is a hidden text channel"));
        QCOMPARE(screen->subheading, QStringLiteral("You can not see the messages of this channel."));
        QVERIFY(screen->topic.isEmpty()); // only forums show their topic
        QCOMPARE(texts(*screen), (QStringList { "Last message created:", "Slowmode: 1 hour, 0 minutes and 5 seconds" }));
        QCOMPARE(screen->details[0].time, QDateTime::fromMSecsSinceEpoch(1420070400000LL + 1000000, QTimeZone::UTC));

        // The owner, Ben (allowed by an overwrite; not Eve, denied), then
        // the roles that can see it: Admin (administrator) and Staff
        QCOMPARE(names(screen->allowed), (QStringList { "owner", "ben", "Admin", "Staff" }));
        QVERIFY(screen->allowed[0].kind == PermissionsEntry::Kind::Member);
        QVERIFY(screen->allowed[2].kind == PermissionsEntry::Kind::Role);
        // Ben isn't a loaded member, so he is asked for
        QCOMPARE(h.guilds.requested.size(), 1);
        QCOMPARE(h.guilds.requested[0].second, QList<Id> { Ben });
        QVERIFY(screen->allowedExpanded);
    }

    void lockedVoiceChannelUsesConnect()
    {
        SinglePluginHost h(qord_register_showhiddenchannels);
        QVERIFY(h.enableWithAllPermissions());
        fill(h.guilds);
        const auto screen = h.host->dispatchHiddenChannelLockScreen(hidden(301, true));
        QVERIFY(screen.has_value());
        QCOMPARE(screen->heading, QStringLiteral("This is a locked voice channel"));
        QVERIFY(screen->subheading.isEmpty());
        QCOMPARE(texts(*screen), (QStringList { "Bitrate: 64000 bits", "Region: Automatic", "Video quality mode: Automatic" }));
        QCOMPARE(names(screen->allowed), (QStringList { "owner", "Admin", "Mod" }));
    }

    void forumLockScreenAndEveryone()
    {
        SinglePluginHost h(qord_register_showhiddenchannels);
        QVERIFY(h.enableWithAllPermissions());
        fill(h.guilds);
        auto screen = h.host->dispatchHiddenChannelLockScreen(hidden(302));
        QVERIFY(screen.has_value());
        QCOMPARE(screen->heading, QStringLiteral("This is a hidden forum channel"));
        QCOMPARE(screen->subheading,
                 QStringLiteral("You can not see the posts of this channel. However you may see its guidelines:"));
        QCOMPARE(screen->topic, QStringLiteral("Be precise"));
        QCOMPARE(texts(*screen), (QStringList { "Default inactivity duration before archiving posts: 1 week",
                                                "Default layout: Gallery view", "Default sort order: Creation date",
                                                "Default reaction emoji: 👍",
                                                "Posts on this forum require a tag to be set." }));
        QCOMPARE(screen->tags, (QStringList { "bug", "question" }));

        // When @everyone can see a channel, it is the only role listed
        h.guilds.channels[302].overwrites = {};
        screen = h.host->dispatchHiddenChannelLockScreen(hidden(302));
        QCOMPARE(names(screen->allowed), (QStringList { "owner", "@everyone" }));
    }

    void buttonsWork()
    {
        SinglePluginHost h(qord_register_showhiddenchannels);
        QVERIFY(h.enableWithAllPermissions());
        fill(h.guilds);
        const auto screen = h.host->dispatchHiddenChannelLockScreen(hidden(300));
        QVERIFY(screen.has_value());

        screen->toggleAllowed();
        QVERIFY(!h.host->settings(h.id)->boolValue(QStringLiteral("defaultAllowedUsersAndRolesDropdownState")));
        QVERIFY(!h.host->dispatchHiddenChannelLockScreen(hidden(300))->allowedExpanded);

        screen->showPermissionDetails();
        QCOMPARE(h.ui.permissionViews.size(), 1);
        QCOMPARE(h.ui.permissionViews[0].title, QStringLiteral("staff-only Permissions"));

        // Clicks after the plugin stopped do nothing
        QVERIFY(h.host->setEnabled(h.id, false));
        screen->toggleAllowed();
        screen->showPermissionDetails();
        QCOMPARE(h.ui.permissionViews.size(), 1);
        QVERIFY(!h.host->settings(h.id)->boolValue(QStringLiteral("defaultAllowedUsersAndRolesDropdownState")));
    }

    void needsItsPermissions()
    {
        SinglePluginHost h(qord_register_showhiddenchannels);
        fill(h.guilds);
        h.host->setGranted(h.id, Permission::AddUiElements | Permission::ReadServerPermissions);
        QVERIFY(h.host->setEnabled(h.id, true));
        QVERIFY(!h.host->dispatchHiddenChannelDisplay().has_value());
        QVERIFY(!h.host->dispatchHiddenChannelLockScreen(hidden(300)).has_value());
    }
};

QTEST_GUILESS_MAIN(TestShowHiddenChannels)
#include "tst_showhiddenchannels.moc"
