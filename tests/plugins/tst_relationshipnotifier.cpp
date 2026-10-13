#include "PluginTestUtils.hpp"

#include <QJsonObject>
#include <QTest>

namespace Qord::Plugins {
void qord_register_relationshipnotifier(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

class TestRelationshipNotifier : public QObject
{
    Q_OBJECT

    static Memberships memberships()
    {
        Memberships m;
        m.accountId = 1;
        m.friends = { { 10, QStringLiteral("ava"), QStringLiteral("https://cdn/ava.png") },
                      { 11, QStringLiteral("ben"), QString() } };
        m.incomingRequests = { { 20, QStringLiteral("cleo"), QString() }, { 21, QStringLiteral("dan"), QString() } };
        m.servers = { { 30, QStringLiteral("Qord Dev"), QStringLiteral("https://cdn/icon.png") } };
        m.groups = { { 40, QStringLiteral("Weekend"), QString() } };
        return m;
    }

    static Removal removal(Removal::Kind kind, MembershipEntry entry, bool byYou = false)
    {
        Removal r;
        r.kind = kind;
        r.accountId = 1;
        r.entry = std::move(entry);
        r.byYou = byYou;
        return r;
    }

    static QStringList bodies(const SinglePluginHost &h)
    {
        QStringList out;
        for (const auto &n : h.ui.notifications)
            out.append(n.body);
        return out;
    }

private slots:
    void liveRemovals()
    {
        SinglePluginHost h(&qord_register_relationshipnotifier);
        QVERIFY(h.enableWithAllPermissions());
        h.host->dispatchMembershipsChanged(memberships());
        QVERIFY(h.ui.notifications.isEmpty()); // nothing known before: nothing to compare

        h.host->dispatchRemoved(removal(Removal::Kind::Friend, memberships().friends[0]));
        h.host->dispatchRemoved(removal(Removal::Kind::FriendRequest, memberships().incomingRequests[0]));
        h.host->dispatchRemoved(removal(Removal::Kind::Server, memberships().servers[0]));
        h.host->dispatchRemoved(removal(Removal::Kind::Group, memberships().groups[0]));
        QCOMPARE(bodies(h), (QStringList{ QStringLiteral("ava removed you as a friend."),
                                          QStringLiteral("A friend request from cleo has been removed."),
                                          QStringLiteral("You were removed from the server Qord Dev."),
                                          QStringLiteral("You were removed from the group Weekend.") }));
        const PluginNotification &first = h.ui.notifications.first();
        QCOMPARE(first.title, QStringLiteral("Relationship Notifier"));
        QCOMPARE(first.iconUrl, QStringLiteral("https://cdn/ava.png"));
        QVERIFY(!first.permanent);

        // A person's notification opens their profile; a server's does nothing
        h.ui.notificationClicks[0]();
        QCOMPARE(h.ui.profilesOpened, (QList<QPair<Id, Id>>{ { 1, 10 } }));
        if (h.ui.notificationClicks[2])
            h.ui.notificationClicks[2]();
        QCOMPARE(h.ui.profilesOpened.size(), 1);
    }

    void removalsByYouAreSilent()
    {
        SinglePluginHost h(&qord_register_relationshipnotifier);
        QVERIFY(h.enableWithAllPermissions());
        h.host->dispatchRemoved(removal(Removal::Kind::Friend, memberships().friends[0], true));
        h.host->dispatchRemoved(removal(Removal::Kind::Server, memberships().servers[0], true));
        QVERIFY(h.ui.notifications.isEmpty());
    }

    void settingsTurnKindsOff()
    {
        SinglePluginHost h(&qord_register_relationshipnotifier);
        QVERIFY(h.enableWithAllPermissions());
        auto *s = h.host->settings(h.id);
        for (const char *key : { "friends", "friendRequestCancels", "servers", "groups" })
            s->setValue(QString::fromLatin1(key), false);
        h.host->dispatchRemoved(removal(Removal::Kind::Friend, memberships().friends[0]));
        h.host->dispatchRemoved(removal(Removal::Kind::FriendRequest, memberships().incomingRequests[0]));
        h.host->dispatchRemoved(removal(Removal::Kind::Server, memberships().servers[0]));
        h.host->dispatchRemoved(removal(Removal::Kind::Group, memberships().groups[0]));
        QVERIFY(h.ui.notifications.isEmpty());

        // Notices: the notification stays
        s->setValue(QStringLiteral("servers"), true);
        s->setValue(QStringLiteral("notices"), true);
        h.host->dispatchRemoved(removal(Removal::Kind::Server, memberships().servers[0]));
        QCOMPARE(h.ui.notifications.size(), 1);
        QVERIFY(h.ui.notifications.first().permanent);
    }

    void offlineRemovals()
    {
        SinglePluginHost h(&qord_register_relationshipnotifier);
        QVERIFY(h.enableWithAllPermissions());
        h.host->dispatchMembershipsChanged(memberships());
        QVERIFY(h.host->settings(h.id)->value(QStringLiteral("known")).toObject().contains(QStringLiteral("1")));

        // Next run: everything went away while Qord was closed, except that
        // dan's request was answered (you are friends now or blocked him)
        h.host->setEnabled(h.id, false);
        QVERIFY(h.enableWithAllPermissions());
        h.relationships.others.insert(21);
        Memberships now;
        now.accountId = 1;
        now.friends = { memberships().friends[1] };
        h.host->dispatchMembershipsChanged(now);
        QCOMPARE(bodies(h), (QStringList{ QStringLiteral("You are no longer in the group Weekend."),
                                          QStringLiteral("You are no longer in the server Qord Dev."),
                                          QStringLiteral("You are no longer friends with ava."),
                                          QStringLiteral("Friend request from cleo has been revoked.") }));

        // Only once per run, and what is known follows
        h.host->dispatchMembershipsChanged(memberships());
        h.host->dispatchMembershipsChanged(now);
        QCOMPARE(h.ui.notifications.size(), 4);
    }

    void offlineRemovalsCanBeOff()
    {
        SinglePluginHost h(&qord_register_relationshipnotifier);
        QVERIFY(h.enableWithAllPermissions());
        h.host->dispatchMembershipsChanged(memberships());
        h.host->setEnabled(h.id, false);
        h.host->settings(h.id)->setValue(QStringLiteral("offlineRemovals"), false);
        QVERIFY(h.enableWithAllPermissions());
        Memberships empty;
        empty.accountId = 1;
        h.host->dispatchMembershipsChanged(empty);
        QVERIFY(h.ui.notifications.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestRelationshipNotifier)
#include "tst_relationshipnotifier.moc"
