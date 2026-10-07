#include "PluginTestUtils.hpp"

#include "NoBlockedMessages.hpp"

#include <QTest>

namespace Qord::Plugins {
void qord_register_noblockedmessages(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;
using namespace Qord::Plugins::NoBlockedMessages;

static MessageContext from(Id author, Id replyTo = 0, bool bot = false, int type = 0)
{
    MessageContext m;
    m.accountId = 1;
    m.authorId = author;
    m.referencedAuthorId = replyTo;
    m.authorIsBot = bot;
    m.type = type;
    return m;
}

class TestNoBlockedMessages : public QObject
{
    Q_OBJECT

private slots:
    void verdicts()
    {
        const auto rel = [](Id id) {
            Relationship r;
            r.blocked = id == 10;
            r.ignored = id == 20;
            return r;
        };
        Config c;
        // Plain messages are untouched
        QCOMPARE(evaluate(c, from(5), rel).visible, true);
        // Blocked and (by default) ignored authors are hidden
        QCOMPARE(evaluate(c, from(10), rel).visible, false);
        QCOMPARE(evaluate(c, from(20), rel).visible, false);
        c.alsoHideIgnoredUsers = false;
        QCOMPARE(evaluate(c, from(20), rel).visible, true);
        c.alsoHideIgnoredUsers = true;

        // Override list: these are only collapsed (shown in Qord)
        c.overrideUsers = { 10 };
        Verdict v = evaluate(c, from(10), rel);
        QVERIFY(v.visible);
        QVERIFY(v.suppressed);
        // ...and with defaultHideUsers off the logic flips
        c.defaultHideUsers = false;
        QVERIFY(!evaluate(c, from(10), rel).visible);
        QVERIFY(evaluate(c, from(20), rel).visible);
        c.defaultHideUsers = true;
        c.overrideUsers = {};

        // AutoMod action messages bypass filtering
        QVERIFY(evaluate(c, from(10, 0, false, 24), rel).visible);
        c.allowAutoModMessages = false;
        QVERIFY(!evaluate(c, from(10, 0, false, 24), rel).visible);
        c.allowAutoModMessages = true;

        // A bot answering a blocked user's prefix command
        QVERIFY(!evaluate(c, from(30, 10, true), rel).visible);
        // A human replying to a blocked user: only with hideBlockedUserReplies
        QVERIFY(evaluate(c, from(5, 10), rel).visible);
        c.hideBlockedUserReplies = true;
        QVERIFY(!evaluate(c, from(5, 10), rel).visible);
    }

    void parsesOverrides()
    {
        QCOMPARE(parseOverrideUsers(" 1, 2 ,x,,3"), (QSet<Id> { 1, 2, 3 }));
        QCOMPARE(parseOverrideUsers(""), QSet<Id>());
    }

    void pluginHidesAndSilences()
    {
        SinglePluginHost h(qord_register_noblockedmessages);
        QVERIFY(h.info()->loadError.isEmpty());
        h.relationships.blocked = { 10 };
        QVERIFY(!h.host->dispatchShouldHideMessage(from(10))); // disabled
        QVERIFY(h.enableWithAllPermissions());
        QVERIFY(h.host->dispatchShouldHideMessage(from(10)));
        QVERIFY(h.host->dispatchShouldSilenceMessage(from(10))); // hidden implies silent
        QVERIFY(!h.host->dispatchShouldHideMessage(from(5)));
        QVERIFY(!h.host->dispatchShouldSilenceMessage(from(5)));

        // Collapsed (shown) messages are silenced only with disableNotifications
        const int before = h.messages.filterRefreshes;
        QVERIFY(h.host->settings(h.id)->setValue("overrideUsers", "10").isEmpty());
        QVERIFY(h.messages.filterRefreshes > before); // settings changes re-filter the chat
        QVERIFY(!h.host->dispatchShouldHideMessage(from(10)));
        QVERIFY(!h.host->dispatchShouldSilenceMessage(from(10)));
        QVERIFY(h.host->settings(h.id)->setValue("disableNotifications", true).isEmpty());
        QVERIFY(h.host->dispatchShouldSilenceMessage(from(10)));
    }

    void needsRelationshipPermission()
    {
        SinglePluginHost h(qord_register_noblockedmessages);
        h.relationships.blocked = { 10 };
        h.host->setGranted(h.id, Permission::HideMessages);
        QVERIFY(h.host->setEnabled(h.id, true));
        QVERIFY(!h.host->dispatchShouldHideMessage(from(10)));
    }
};

QTEST_GUILESS_MAIN(TestNoBlockedMessages)
#include "tst_noblockedmessages.moc"
