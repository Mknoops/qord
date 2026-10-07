#include "PluginTestUtils.hpp"

#include "NoReplyMention.hpp"

#include <QTest>

namespace Qord::Plugins {
void qord_register_noreplymention(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;
using namespace Qord::Plugins::NoReplyMention;

class TestNoReplyMention : public QObject
{
    Q_OBJECT

private slots:
    void truthTable_data()
    {
        QTest::addColumn<bool>("shouldPingListed");
        QTest::addColumn<bool>("inverseShiftReply");
        QTest::addColumn<bool>("listed");
        QTest::addColumn<bool>("shift");
        QTest::addColumn<bool>("expected");
        // Exhaustive: isExempt = ping ? listed : !listed;
        // inverse ? shift != isExempt : !shift && isExempt
        for (int bits = 0; bits < 16; ++bits) {
            const bool ping = bits & 1, inverse = bits & 2, listed = bits & 4, shift = bits & 8;
            const bool exempt = ping ? listed : !listed;
            const bool expected = inverse ? shift != exempt : !shift && exempt;
            QTest::addRow("ping=%d inverse=%d listed=%d shift=%d", ping, inverse, listed, shift)
                    << ping << inverse << listed << shift << expected;
        }
    }

    void truthTable()
    {
        QFETCH(bool, shouldPingListed);
        QFETCH(bool, inverseShiftReply);
        QFETCH(bool, listed);
        QFETCH(bool, shift);
        QFETCH(bool, expected);
        Config c;
        c.shouldPingListed = shouldPingListed;
        c.inverseShiftReply = inverseShiftReply;
        QCOMPARE(shouldMention(c, listed, shift), expected);
    }

    void spotChecks()
    {
        Config defaults; // shouldPingListed = true, empty lists
        // Default: nobody is listed, so replies don't ping, shift or not
        QCOMPARE(shouldMention(defaults, false, false), false);
        QCOMPARE(shouldMention(defaults, false, true), false);
        Config exemptListed;
        exemptListed.shouldPingListed = false;
        QCOMPARE(shouldMention(exemptListed, false, false), true); // unlisted: ping
        QCOMPARE(shouldMention(exemptListed, false, true), false); // shift suppresses
        QCOMPARE(shouldMention(exemptListed, true, false), false); // listed: no ping
    }

    void parseIdList()
    {
        QCOMPARE(NoReplyMention::parseIdList(""), QSet<Id>());
        QCOMPARE(NoReplyMention::parseIdList("1, 2,3\n4  5"), (QSet<Id> { 1, 2, 3, 4, 5 }));
        QCOMPARE(NoReplyMention::parseIdList(" ,abc, 12x, 99 "), QSet<Id> { 99 });
        QCOMPARE(NoReplyMention::parseIdList("123456789012345678"), QSet<Id> { 123456789012345678ull });
    }

    void listedByUserOrRole()
    {
        Config c;
        c.users = { 10 };
        c.roles = { 500 };
        QVERIFY(isListed(c, 10, std::nullopt));
        QVERIFY(!isListed(c, 11, std::nullopt));
        QVERIFY(isListed(c, 11, QList<Id> { 499, 500 }));
        QVERIFY(!isListed(c, 11, QList<Id> { 499 }));
        QVERIFY(!isListed(c, 11, QList<Id> {}));
    }

    void pluginUsesSettingsAndMemberRoles()
    {
        SinglePluginHost h(qord_register_noreplymention);
        QVERIFY(h.info()->loadError.isEmpty());
        PluginSettings *s = h.host->settings(h.id);
        QVERIFY(s->setValue("userList", "100").isEmpty());
        QVERIFY(s->setValue("roleList", "777, 778").isEmpty());
        h.members.roles.insert({ 1, 200 }, { 777 });
        QVERIFY(h.enableWithAllPermissions());

        ReplyContext r;
        r.accountId = 9;
        r.guildId = 1;
        r.channelId = 2;
        // Listed user, default "only ping listed": ping unless shift
        r.authorId = 100;
        QCOMPARE(h.host->dispatchShouldMentionOnReply(r), true);
        r.shiftHeld = true;
        QCOMPARE(h.host->dispatchShouldMentionOnReply(r), false);
        r.shiftHeld = false;
        // Listed through a role in this guild
        r.authorId = 200;
        QCOMPARE(h.host->dispatchShouldMentionOnReply(r), true);
        // Same user in a DM: no guild roles, not listed
        r.guildId = 0;
        QCOMPARE(h.host->dispatchShouldMentionOnReply(r), false);
        // Unknown member: not listed
        r.guildId = 1;
        r.authorId = 300;
        QCOMPARE(h.host->dispatchShouldMentionOnReply(r), false);

        QVERIFY(s->setValue("inverseShiftReply", true).isEmpty());
        r.shiftHeld = true;
        QCOMPARE(h.host->dispatchShouldMentionOnReply(r), true);
    }

    void pluginWithoutMemberPermission()
    {
        SinglePluginHost h(qord_register_noreplymention);
        QVERIFY(h.host->settings(h.id)->setValue("roleList", "777").isEmpty());
        h.members.roles.insert({ 1, 200 }, { 777 });
        h.host->setGranted(h.id, Permission::ReplyMentions);
        QVERIFY(h.host->setEnabled(h.id, true));
        ReplyContext r;
        r.guildId = 1;
        r.authorId = 200;
        // Roles can't be read, so the member is not listed
        QCOMPARE(h.host->dispatchShouldMentionOnReply(r), false);
        QCOMPARE(h.members.lookups, 0);
    }

    void disabledMeansDiscordDefault()
    {
        SinglePluginHost h(qord_register_noreplymention);
        ReplyContext r;
        QCOMPARE(h.host->dispatchShouldMentionOnReply(r), true);
        r.shiftHeld = true;
        QCOMPARE(h.host->dispatchShouldMentionOnReply(r), false);
    }
};

QTEST_GUILESS_MAIN(TestNoReplyMention)
#include "tst_noreplymention.moc"
