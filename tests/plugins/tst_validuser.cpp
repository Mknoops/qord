#include "PluginTestUtils.hpp"

#include "ValidUser.hpp"

#include <QTest>

namespace Qord::Plugins {
void qord_register_validuser(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

namespace {
MentionContext mention(Id userId, Id messageId = 50)
{
    MentionContext m;
    m.accountId = 1;
    m.channelId = 2;
    m.messageId = messageId;
    m.userId = userId;
    return m;
}

UserLookupService::Result result(bool ok, int status = 200, int retryAfterMs = 0)
{
    UserLookupService::Result r;
    r.ok = ok;
    r.status = status;
    r.retryAfterMs = retryAfterMs;
    return r;
}
} // namespace

class TestValidUser : public QObject
{
    Q_OBJECT

private slots:
    void fetchesUnknownUsersOnce()
    {
        SinglePluginHost h(qord_register_validuser);
        QVERIFY(h.info()->loadError.isEmpty());
        QVERIFY(h.enableWithAllPermissions());

        h.host->dispatchUserMentionHovered(mention(7));
        h.host->dispatchUserMentionHovered(mention(7)); // already being fetched
        QCOMPARE(h.users.fetches.size(), 1);
        QCOMPARE(h.users.fetches[0].userId, Id(7));

        h.users.known.insert(7);
        h.users.fetches[0].done(result(true));
        QCOMPARE(h.users.refreshed, QList<Id> { 50 });

        // Known now: another message's mention is just re-rendered
        h.host->dispatchUserMentionHovered(mention(7, 51));
        QCOMPARE(h.users.refreshed, (QList<Id> { 50, 51 }));
        QCOMPARE(h.users.fetches.size(), 1);
    }

    void oneAtATimeNewestFirst()
    {
        SinglePluginHost h(qord_register_validuser);
        QVERIFY(h.enableWithAllPermissions());
        h.host->dispatchUserMentionHovered(mention(1));
        for (Id id = 2; id <= 8; ++id) // 7 more: only the newest 5 wait
            h.host->dispatchUserMentionHovered(mention(id));
        QCOMPARE(h.users.fetches.size(), 1);
        h.users.fetches[0].done(result(true));
        QCOMPARE(h.users.fetches.size(), 1); // spaced 300 ms apart
        QTRY_COMPARE(h.users.fetches.size(), 2);
        QCOMPARE(h.users.fetches[1].userId, Id(8));
        // 2 and 3 were dropped and can be fetched again later
        h.users.fetches[1].done(result(true));
        h.host->dispatchUserMentionHovered(mention(2));
        QTRY_COMPARE(h.users.fetches.size(), 3);
        QCOMPARE(h.users.fetches[2].userId, Id(2));
    }

    void waitsOutRateLimits()
    {
        SinglePluginHost h(qord_register_validuser);
        QVERIFY(h.enableWithAllPermissions());
        h.host->dispatchUserMentionHovered(mention(7));
        h.users.fetches[0].done(result(false, 429, 200));
        // After the spacing and the 200 ms wait, they are fetched again
        QTRY_COMPARE_WITH_TIMEOUT(h.users.fetches.size(), 2, 3000);
        QCOMPARE(h.users.fetches[1].userId, Id(7));
        h.users.fetches[1].done(result(true));
        QCOMPARE(h.users.refreshed, QList<Id> { 50 });
    }

    void otherFailuresAreNotRetried()
    {
        SinglePluginHost h(qord_register_validuser);
        QVERIFY(h.enableWithAllPermissions());
        h.host->dispatchUserMentionHovered(mention(7));
        h.users.fetches[0].done(result(false, 404));
        QTest::qWait(ValidUserPlugin::SpacingMs + 100);
        h.host->dispatchUserMentionHovered(mention(7));
        QTest::qWait(ValidUserPlugin::SpacingMs + 100);
        QCOMPARE(h.users.fetches.size(), 1);
        QVERIFY(h.users.refreshed.isEmpty());
    }

    void repliesAfterStopAreDropped()
    {
        SinglePluginHost h(qord_register_validuser);
        QVERIFY(h.enableWithAllPermissions());
        h.host->dispatchUserMentionHovered(mention(7));
        QVERIFY(h.host->setEnabled(h.id, false));
        h.users.fetches[0].done(result(true));
        QVERIFY(h.users.refreshed.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestValidUser)
#include "tst_validuser.moc"
