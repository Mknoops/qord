#include "PluginTestUtils.hpp"

#include <QTest>

namespace Qord::Plugins {
void qord_register_followvoiceuser(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

class TestFollowVoiceUser : public QObject
{
    Q_OBJECT

    static constexpr Id Me = 1;
    static constexpr Id Ava = 10;
    static constexpr Id Stranger = 11;

    static UserContext user(Id userId)
    {
        UserContext u;
        u.accountId = Me;
        u.userId = userId;
        return u;
    }

    static VoiceStateChange moved(Id userId, Id channelId)
    {
        VoiceStateChange c;
        c.accountId = Me;
        c.guildId = 5;
        c.userId = userId;
        c.channelId = channelId;
        return c;
    }

    // Enabled, Ava a friend, you in voice
    static void start(SinglePluginHost &h)
    {
        QVERIFY(h.enableWithAllPermissions());
        h.relationships.friends.insert(Ava);
        UserVoiceState mine;
        mine.channelId = 100;
        h.voiceStates.states.insert(Me, mine);
    }

    static void follow(SinglePluginHost &h, Id userId)
    {
        const auto items = h.host->dispatchUserContextMenuItems(user(userId));
        QCOMPARE(items.size(), 1);
        h.host->dispatchUserContextMenuAction(items[0].pluginId, items[0].item.id, user(userId));
    }

private slots:
    void menuOnlyForFriends()
    {
        SinglePluginHost h(&qord_register_followvoiceuser);
        start(h);
        QVERIFY(h.host->dispatchUserContextMenuItems(user(Stranger)).isEmpty());
        QVERIFY(h.host->dispatchUserContextMenuItems(user(Me)).isEmpty());
        const auto items = h.host->dispatchUserContextMenuItems(user(Ava));
        QCOMPARE(items.size(), 1);
        QCOMPARE(items[0].item.label, QStringLiteral("Follow User"));
        QCOMPARE(items[0].item.checked, std::optional<bool>(false));
        follow(h, Ava);
        QCOMPARE(h.host->dispatchUserContextMenuItems(user(Ava))[0].item.checked, std::optional<bool>(true));
    }

    void joinsWhereTheyGo()
    {
        SinglePluginHost h(&qord_register_followvoiceuser);
        start(h);
        h.host->dispatchVoiceStateChanged(moved(Ava, 200));
        QVERIFY(h.ui.joinedVoice.isEmpty()); // not following yet
        follow(h, Ava);
        h.host->dispatchVoiceStateChanged(moved(Stranger, 300));
        h.host->dispatchVoiceStateChanged(moved(Ava, 200));
        h.host->dispatchVoiceStateChanged(moved(Ava, 200)); // a mute or deafen: same channel
        h.host->dispatchVoiceStateChanged(moved(Ava, 201));
        QCOMPARE(h.ui.joinedVoice, (QList<QPair<Id, Id>>{ { Me, 200 }, { Me, 201 } }));

        // Unchecking stops following
        follow(h, Ava);
        h.host->dispatchVoiceStateChanged(moved(Ava, 202));
        QCOMPARE(h.ui.joinedVoice.size(), 2);
    }

    void onlyWhenInVoice()
    {
        SinglePluginHost h(&qord_register_followvoiceuser);
        start(h);
        follow(h, Ava);
        h.voiceStates.states.remove(Me);
        h.host->dispatchVoiceStateChanged(moved(Ava, 200));
        QVERIFY(h.ui.joinedVoice.isEmpty());
        h.host->settings(h.id)->setValue(QStringLiteral("onlyWhenInVoice"), false);
        h.host->dispatchVoiceStateChanged(moved(Ava, 200));
        QCOMPARE(h.ui.joinedVoice.size(), 1);
    }

    void leavingAndUnfriending()
    {
        SinglePluginHost h(&qord_register_followvoiceuser);
        start(h);
        follow(h, Ava);
        h.host->dispatchVoiceStateChanged(moved(Ava, 0));
        QVERIFY(h.ui.leftVoice.isEmpty()); // off by default
        h.host->settings(h.id)->setValue(QStringLiteral("leaveWhenUserLeaves"), true);
        h.host->dispatchVoiceStateChanged(moved(Ava, 0));
        QCOMPARE(h.ui.leftVoice, QList<Id>{ Me });

        // No longer friends: no following
        h.relationships.friends.remove(Ava);
        h.host->dispatchVoiceStateChanged(moved(Ava, 200));
        QVERIFY(h.ui.joinedVoice.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestFollowVoiceUser)
#include "tst_followvoiceuser.moc"
