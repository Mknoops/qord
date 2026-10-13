#include "PluginTestUtils.hpp"

#include "WhoReacted.hpp"

#include <QSignalSpy>
#include <QTest>

namespace Qord::Plugins {
void qord_register_whoreacted(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

class TestWhoReacted : public QObject
{
    Q_OBJECT

    static ReactionContext reaction(Id messageId, const QString &emoji = QStringLiteral("👍"), Id emojiId = 0,
                                    int reactions = 1)
    {
        ReactionContext r;
        r.accountId = 1;
        r.channelId = 2;
        r.messageId = messageId;
        r.emojiName = emoji;
        r.emojiId = emojiId;
        r.reactionsOnMessage = reactions;
        return r;
    }

private slots:
    void keysMatchUpstream()
    {
        QCOMPARE(WhoReactedPlugin::cacheKey(5, QStringLiteral("👍"), 0, 0), QStringLiteral("5:👍::0"));
        QCOMPARE(WhoReactedPlugin::cacheKey(5, QStringLiteral("blob"), 77, 1), QStringLiteral("5:blob:77:1"));
        QCOMPARE(WhoReactedPlugin::emojiKey(QStringLiteral("blob"), 77), QStringLiteral("blob:77"));
        QCOMPARE(WhoReactedPlugin::emojiKey(QStringLiteral("👍"), 0), QStringLiteral("👍"));
    }

    void fetchesOnceThenShowsUsers()
    {
        SinglePluginHost h(qord_register_whoreacted);
        QVERIFY(h.info()->loadError.isEmpty());
        QVERIFY(h.enableWithAllPermissions());

        // Nothing to show until the users arrive; one request per reaction
        QVERIFY(!h.host->dispatchReactionUsers(reaction(10)).has_value());
        QVERIFY(!h.host->dispatchReactionUsers(reaction(10)).has_value());
        QCOMPARE(h.reactions.fetches.size(), 1);
        QCOMPARE(h.reactions.fetches[0].messageId, Id(10));
        QCOMPARE(h.reactions.fetches[0].emojiKey, QStringLiteral("👍"));

        h.reactions.fetches[0].done(true, { 100, 101, 102 });
        QCOMPARE(h.reactions.changed, QList<Id> { 10 }); // re-render asked for
        const auto users = h.host->dispatchReactionUsers(reaction(10));
        QVERIFY(users.has_value());
        QCOMPARE(users->users, (QList<Id> { 100, 101, 102 }));
        QVERIFY(users->clickable); // the default
        QCOMPARE(h.reactions.fetches.size(), 1); // cached
    }

    void queueIsNewestFirstAndSpaced()
    {
        SinglePluginHost h(qord_register_whoreacted);
        QVERIFY(h.enableWithAllPermissions());
        (void)h.host->dispatchReactionUsers(reaction(1)); // starts at once
        (void)h.host->dispatchReactionUsers(reaction(2));
        (void)h.host->dispatchReactionUsers(reaction(3));
        QCOMPARE(h.reactions.fetches.size(), 1);
        h.reactions.fetches[0].done(true, { 5 });
        // The next waits 250 ms after the last finished, as upstream sleeps
        QTest::qWait(WhoReactedPlugin::FetchSpacingMs / 2);
        QCOMPARE(h.reactions.fetches.size(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(h.reactions.fetches.size(), 2, 1000);
        // upstream unshifts: the most recently asked for (3) before 2
        QCOMPARE(h.reactions.fetches[1].messageId, Id(3));
    }

    void tooManyReactionsShowNothing()
    {
        SinglePluginHost h(qord_register_whoreacted);
        QVERIFY(h.enableWithAllPermissions());
        QVERIFY(!h.host->dispatchReactionUsers(reaction(9, QStringLiteral("👍"), 0, 11)).has_value());
        QCOMPARE(h.reactions.fetches.size(), 0);
    }

    void liveChangesUpdateTheList()
    {
        SinglePluginHost h(qord_register_whoreacted);
        QVERIFY(h.enableWithAllPermissions());
        (void)h.host->dispatchReactionUsers(reaction(10));
        h.reactions.fetches[0].done(true, { 100 });
        ReactionChange add;
        add.accountId = 1;
        add.channelId = 2;
        add.messageId = 10;
        add.emojiName = QStringLiteral("👍");
        add.userId = 200;
        h.host->dispatchReactionChanged(add);
        QCOMPARE(h.host->dispatchReactionUsers(reaction(10))->users, (QList<Id> { 100, 200 }));
        ReactionChange remove = add;
        remove.userId = 100;
        remove.added = false;
        h.host->dispatchReactionChanged(remove);
        QCOMPARE(h.host->dispatchReactionUsers(reaction(10))->users, (QList<Id> { 200 }));
        // Removing the last one shows nothing, like upstream's empty list
        remove.userId = 200;
        h.host->dispatchReactionChanged(remove);
        QVERIFY(!h.host->dispatchReactionUsers(reaction(10)).has_value());
    }

    void settingAndPermission()
    {
        SinglePluginHost h(qord_register_whoreacted);
        QVERIFY(h.enableWithAllPermissions());
        h.host->settings(h.id)->setValue(QStringLiteral("clickableAvatars"), false);
        (void)h.host->dispatchReactionUsers(reaction(10));
        h.reactions.fetches[0].done(true, { 100 });
        QVERIFY(!h.host->dispatchReactionUsers(reaction(10))->clickable);

        SinglePluginHost denied(qord_register_whoreacted);
        QVERIFY(!denied.host->setEnabled(denied.id, true) || !denied.host->hasHook(Hook::ReactionUsers));
    }

    void failedFetchShowsNothing()
    {
        SinglePluginHost h(qord_register_whoreacted);
        QVERIFY(h.enableWithAllPermissions());
        (void)h.host->dispatchReactionUsers(reaction(10));
        h.reactions.fetches[0].done(false, {});
        QVERIFY(!h.host->dispatchReactionUsers(reaction(10)).has_value());
        QCOMPARE(h.reactions.fetches.size(), 1); // no retry loop
    }
};

QTEST_GUILESS_MAIN(TestWhoReacted)
#include "tst_whoreacted.moc"
