#include "PluginTestUtils.hpp"

#include <QTest>

namespace Qord::Plugins {
void qord_register_mutualgroupdms(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

namespace {
PrivateChannelInfo channel(Id id, bool group, const QString &name, QList<Id> recipients, const QString &icon = {})
{
    PrivateChannelInfo c;
    c.channelId = id;
    c.isGroup = group;
    c.name = name;
    c.recipientIds = std::move(recipients);
    c.iconUrl = icon;
    return c;
}

UserContext user(Id id, bool bot = false)
{
    UserContext u;
    u.accountId = 1;
    u.userId = id;
    u.isBot = bot;
    return u;
}
} // namespace

class TestMutualGroupDMs : public QObject
{
    Q_OBJECT

private slots:
    void listsSharedGroups()
    {
        SinglePluginHost h(qord_register_mutualgroupdms);
        auto &dms = h.dms;
        dms.channels = { channel(10, true, QStringLiteral("Weekend plans"), { 7, 8 }, QStringLiteral("https://cdn/x.png")),
                         channel(11, false, QStringLiteral("Ava"), { 7 }), channel(12, true, QStringLiteral("Ben, Cleo"), { 8, 9 }),
                         channel(13, true, QStringLiteral("Raid"), { 7, 8, 9 }) };
        QVERIFY(h.info()->loadError.isEmpty());
        QVERIFY(h.enableWithAllPermissions());

        auto tabs = h.host->dispatchProfileTabs(user(7));
        QCOMPARE(tabs.size(), 1);
        QCOMPARE(tabs[0].tab.title, QStringLiteral("2 Mutual Groups"));
        QCOMPARE(tabs[0].tab.rows.size(), 2);
        QCOMPARE(tabs[0].tab.rows[0].title, QStringLiteral("Weekend plans"));
        QCOMPARE(tabs[0].tab.rows[0].subtitle, QStringLiteral("3 Members"));
        QCOMPARE(tabs[0].tab.rows[0].iconUrl, QStringLiteral("https://cdn/x.png"));
        QCOMPARE(tabs[0].tab.rows[1].subtitle, QStringLiteral("4 Members"));
        QCOMPARE(tabs[0].tab.rows[1].iconText, QStringLiteral("R"));

        QCOMPARE(h.host->dispatchProfileTabs(user(9))[0].tab.title, QStringLiteral("2 Mutual Groups"));
        dms.channels.removeLast();
        QCOMPARE(h.host->dispatchProfileTabs(user(9))[0].tab.title, QStringLiteral("1 Mutual Group"));
        const auto none = h.host->dispatchProfileTabs(user(20))[0].tab;
        QCOMPARE(none.title, QStringLiteral("No Mutual Groups"));
        QVERIFY(none.rows.isEmpty());
        QCOMPARE(none.emptyText, QStringLiteral("You don't have any group chats in common"));

        // Not for bots or yourself
        QVERIFY(h.host->dispatchProfileTabs(user(7, true)).isEmpty());
        QVERIFY(h.host->dispatchProfileTabs(user(1)).isEmpty());

        h.host->dispatchProfileTabRowClicked(h.id, QStringLiteral("MUTUAL_GDMS"), QStringLiteral("10"), user(7));
        QCOMPARE(h.ui.openedChannels, (QList<QPair<Id, Id>> { { 1, 10 } }));
    }

    void needsReadDms()
    {
        SinglePluginHost h(qord_register_mutualgroupdms);
        h.dms.channels = { channel(10, true, QStringLiteral("G"), { 7 }) };
        h.host->setGranted(h.id, Permission::AddUiElements);
        QVERIFY(h.host->setEnabled(h.id, true));
        QVERIFY(h.host->dispatchProfileTabs(user(7)).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestMutualGroupDMs)
#include "tst_mutualgroupdms.moc"
