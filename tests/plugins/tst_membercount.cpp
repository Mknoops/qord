#include "PluginTestUtils.hpp"

#include <QLocale>
#include <QTest>

namespace Qord::Plugins {
void qord_register_membercount(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

class TestMemberCount : public QObject
{
    Q_OBJECT

    static MemberCountContext counts(int online, int total, int voice,
                                     MemberCountContext::Place place = MemberCountContext::Place::MemberList)
    {
        MemberCountContext c;
        c.accountId = 1;
        c.guildId = 2;
        c.place = place;
        c.online = online;
        c.total = total;
        c.inVoice = voice;
        return c;
    }

private slots:
    void initTestCase() { QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates)); }

    void memberListBadges()
    {
        SinglePluginHost h(qord_register_membercount);
        QVERIFY(h.info()->loadError.isEmpty());
        QVERIFY(h.host->dispatchMemberCountBadges(counts(5, 10, 0)).isEmpty()); // disabled
        QVERIFY(h.enableWithAllPermissions());
        const auto badges = h.host->dispatchMemberCountBadges(counts(1234, 56789, 3));
        QCOMPARE(badges.size(), 3);
        QCOMPARE(badges[0].icon, CountBadge::Icon::Online);
        QCOMPARE(badges[0].text, QStringLiteral("1,234"));
        QCOMPARE(badges[0].tooltip, QStringLiteral("1,234 online in this channel"));
        QCOMPARE(badges[1].icon, CountBadge::Icon::Total);
        QCOMPARE(badges[1].text, QStringLiteral("56,789"));
        QCOMPARE(badges[1].tooltip, QStringLiteral("56,789 total server members"));
        QCOMPARE(badges[2].icon, CountBadge::Icon::Voice);
        QCOMPARE(badges[2].tooltip, QStringLiteral("3 members in voice"));
    }

    void unknownCounts()
    {
        SinglePluginHost h(qord_register_membercount);
        QVERIFY(h.enableWithAllPermissions());
        // No total: nothing (upstream returns null)
        QVERIFY(h.host->dispatchMemberCountBadges(counts(5, -1, 0)).isEmpty());
        // Unknown online count shows "?"
        const auto badges = h.host->dispatchMemberCountBadges(counts(-1, 10, 0));
        QCOMPARE(badges.size(), 2); // no voice badge for 0
        QCOMPARE(badges[0].text, QStringLiteral("?"));
        QCOMPARE(badges[0].tooltip, QStringLiteral("? online in this channel"));
    }

    void tooltipHasNoVoice()
    {
        SinglePluginHost h(qord_register_membercount);
        QVERIFY(h.enableWithAllPermissions());
        const auto badges = h.host->dispatchMemberCountBadges(counts(5, 10, 4, MemberCountContext::Place::ServerTooltip));
        QCOMPARE(badges.size(), 2);
    }

    void settings()
    {
        SinglePluginHost h(qord_register_membercount);
        QVERIFY(h.enableWithAllPermissions());
        auto *s = h.host->settings(h.id);
        s->setValue(QStringLiteral("voiceActivity"), false);
        QCOMPARE(h.host->dispatchMemberCountBadges(counts(5, 10, 4)).size(), 2);
        s->setValue(QStringLiteral("memberList"), false);
        QVERIFY(h.host->dispatchMemberCountBadges(counts(5, 10, 4)).isEmpty());
        QCOMPARE(h.host->dispatchMemberCountBadges(counts(5, 10, 4, MemberCountContext::Place::ServerTooltip)).size(), 2);
        s->setValue(QStringLiteral("toolTip"), false);
        QVERIFY(h.host->dispatchMemberCountBadges(counts(5, 10, 4, MemberCountContext::Place::ServerTooltip)).isEmpty());
    }

    void needsPermission()
    {
        SinglePluginHost h(qord_register_membercount);
        h.host->setGranted(h.id, {});
        QVERIFY(h.host->setEnabled(h.id, true));
        QVERIFY(h.host->dispatchMemberCountBadges(counts(5, 10, 4)).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestMemberCount)
#include "tst_membercount.moc"
