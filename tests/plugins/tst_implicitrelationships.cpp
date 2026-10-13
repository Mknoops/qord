#include "PluginTestUtils.hpp"

#include <QSignalSpy>
#include <QTest>

namespace Qord::Plugins {
void qord_register_implicitrelationships(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

class TestImplicitRelationships : public QObject
{
    Q_OBJECT

    static FriendsSection section(SinglePluginHost &h, Id accountId = 1)
    {
        const auto sections = h.host->dispatchFriendsSections({ accountId });
        return sections.value(0);
    }

private slots:
    void emptyTabBeforeTheFetch()
    {
        SinglePluginHost h(&qord_register_implicitrelationships);
        QVERIFY(h.enableWithAllPermissions());
        const auto sections = h.host->dispatchFriendsSections({ 1 });
        QCOMPARE(sections.size(), 1);
        QCOMPARE(sections[0].id, QStringLiteral("implicitrelationships/implicit"));
        QCOMPARE(sections[0].label, QStringLiteral("Implicit"));
        QCOMPARE(sections[0].countTitle.arg(0), QStringLiteral("Implicit — 0"));
        QCOMPARE(sections[0].emptyText, QStringLiteral("No one's around to play with Wumpus."));
        QVERIFY(sections[0].userIds.isEmpty());
    }

    void openingFetchesAffinities()
    {
        SinglePluginHost h(&qord_register_implicitrelationships);
        QVERIFY(h.enableWithAllPermissions());
        h.relationships.friends = { 10 };
        h.relationships.blocked = { 11 };
        h.relationships.others = { 12 }; // a friend request
        QSignalSpy changed(h.host.get(), &PluginHost::friendsChanged);

        h.host->dispatchFriendsPageOpened({ 1 });
        QCOMPARE(h.relationships.affinityFetches, QList<Id>{ 1 });
        h.relationships.pendingAffinities.takeFirst()(
                true, { { 20, 3 }, { 10, 0 }, { 21, 1 }, { 11, 2 }, { 12, 4 }, { 22, 1 } });
        QCOMPARE(changed.size(), 1);

        // Without a relationship, closest first (ties keep Discord's order)
        QCOMPARE(section(h).userIds, (QList<Id>{ 21, 22, 20 }));
        QVERIFY(!section(h).sortByName);
        // Other accounts have their own
        QVERIFY(section(h, 2).userIds.isEmpty());

        // Someone who became a friend leaves the tab
        h.relationships.friends.insert(21);
        QCOMPARE(section(h).userIds, (QList<Id>{ 22, 20 }));
    }

    void sortByNameWithoutAffinity()
    {
        SinglePluginHost h(&qord_register_implicitrelationships);
        QVERIFY(h.enableWithAllPermissions());
        QSignalSpy changed(h.host.get(), &PluginHost::friendsChanged);
        h.host->settings(h.id)->setValue(QStringLiteral("sortByAffinity"), false);
        QVERIFY(!changed.isEmpty());
        QVERIFY(section(h).sortByName);
    }

    void failedFetchKeepsWhatWasThere()
    {
        SinglePluginHost h(&qord_register_implicitrelationships);
        QVERIFY(h.enableWithAllPermissions());
        h.host->dispatchFriendsPageOpened({ 1 });
        h.relationships.pendingAffinities.takeFirst()(true, { { 20, 1 } });
        h.host->dispatchFriendsPageOpened({ 1 });
        h.relationships.pendingAffinities.takeFirst()(false, {});
        QCOMPARE(section(h).userIds, QList<Id>{ 20 });
    }

    void answerAfterStopIsDropped()
    {
        SinglePluginHost h(&qord_register_implicitrelationships);
        QVERIFY(h.enableWithAllPermissions());
        h.host->dispatchFriendsPageOpened({ 1 });
        QString error;
        QVERIFY(h.host->setEnabled(h.id, false, &error));
        QSignalSpy changed(h.host.get(), &PluginHost::friendsChanged);
        h.relationships.pendingAffinities.takeFirst()(true, { { 20, 1 } });
        QCOMPARE(changed.size(), 0);
        QVERIFY(h.host->dispatchFriendsSections({ 1 }).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestImplicitRelationships)
#include "tst_implicitrelationships.moc"
