#include "PluginTestUtils.hpp"

#include "OnePingPerDM.hpp"

#include <QTest>

namespace Qord::Plugins {
void qord_register_onepingperdm(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

static NotificationContext note(bool dm, bool groupDm, bool oldestUnread, Id author = 5)
{
    NotificationContext n;
    n.message.accountId = 1;
    n.message.authorId = author;
    n.message.isDm = dm;
    n.message.isGroupDm = groupDm;
    n.isOldestUnread = oldestUnread;
    return n;
}

class TestOnePingPerDM : public QObject
{
    Q_OBJECT

private slots:
    void onlyFirstUnreadInDms()
    {
        OnePingPerDM::Config c;
        QVERIFY(OnePingPerDM::isPrivateChannelRead(c, note(true, false, true)));
        QVERIFY(!OnePingPerDM::isPrivateChannelRead(c, note(true, false, false)));
        QVERIFY(!OnePingPerDM::isPrivateChannelRead(c, note(false, true, false)));
        // Server channels are never throttled
        QVERIFY(OnePingPerDM::isPrivateChannelRead(c, note(false, false, false)));
    }

    void channelTypeFilter()
    {
        OnePingPerDM::Config c;
        c.channelToAffect = "group_dm";
        QVERIFY(OnePingPerDM::isPrivateChannelRead(c, note(true, false, false)));
        QVERIFY(!OnePingPerDM::isPrivateChannelRead(c, note(false, true, false)));
        c.channelToAffect = "user_dm";
        QVERIFY(OnePingPerDM::isPrivateChannelRead(c, note(false, true, false)));
        QVERIFY(!OnePingPerDM::isPrivateChannelRead(c, note(true, false, false)));
    }

    void exceptions()
    {
        OnePingPerDM::Config c;
        NotificationContext n = note(true, false, false);
        n.message.mentionsMe = true;
        QVERIFY(!OnePingPerDM::isPrivateChannelRead(c, n));
        c.allowMentions = true;
        QVERIFY(OnePingPerDM::isPrivateChannelRead(c, n));

        NotificationContext e = note(false, true, false);
        e.message.mentionEveryone = true;
        QVERIFY(!OnePingPerDM::isPrivateChannelRead(c, e));
        c.allowEveryone = true;
        QVERIFY(OnePingPerDM::isPrivateChannelRead(c, e));

        // Upstream splits on ", " exactly
        c.ignoreUsers = "7, 5";
        QVERIFY(OnePingPerDM::isPrivateChannelRead(c, note(true, false, false, 5)));
        c.ignoreUsers = "7,5";
        QVERIFY(!OnePingPerDM::isPrivateChannelRead(c, note(true, false, false, 5)));
    }

    void plugin()
    {
        SinglePluginHost h(qord_register_onepingperdm);
        QVERIFY(h.info()->loadError.isEmpty());
        QVERIFY(h.host->dispatchShouldNotify(note(true, false, false)));
        QVERIFY(h.enableWithAllPermissions());
        QVERIFY(!h.host->dispatchShouldNotify(note(true, false, false)));
        QVERIFY(h.host->dispatchShouldNotify(note(true, false, true)));
    }
};

QTEST_GUILESS_MAIN(TestOnePingPerDM)
#include "tst_onepingperdm.moc"
