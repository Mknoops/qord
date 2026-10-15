#include "BetterSessions.hpp"
#include "PluginTestUtils.hpp"

#include <QJsonObject>
#include <QTest>

namespace Qord::Plugins {
void qord_register_bettersessions(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

class TestBetterSessions : public QObject
{
    Q_OBJECT

    static DeviceSession session(const QString &hash, const QString &os = QStringLiteral("Windows"),
                                 const QString &platform = QStringLiteral("Discord Client"))
    {
        DeviceSession s;
        s.idHash = hash;
        s.os = os;
        s.platform = platform;
        s.location = QStringLiteral("Amsterdam, NH, Netherlands");
        s.lastUsedMs = 1760000000000;
        return s;
    }

    // Enabled, with the start-up check answered by `sessions`
    static void start(SinglePluginHost &h, const QList<DeviceSession> &sessions = {})
    {
        QVERIFY(h.enableWithAllPermissions());
        QCOMPARE(h.sessions.fetches, QList<Id>{ 1 });
        h.sessions.pending.takeFirst()(true, sessions);
    }

    static DeviceSessionView view(SinglePluginHost &h, const DeviceSession &s)
    {
        const auto v = h.host->dispatchDeviceSessionView(1, s);
        return v ? v->second : DeviceSessionView();
    }

private slots:
    void upstreamTables()
    {
        QCOMPARE(BetterSessionsPlugin::osColor(QStringLiteral("Windows")), QStringLiteral("#55a6ef"));
        QCOMPARE(BetterSessionsPlugin::osColor(QStringLiteral("Linux")), QStringLiteral("#cdcd31"));
        QCOMPARE(BetterSessionsPlugin::osColor(QStringLiteral("Android")), QStringLiteral("#7bc958"));
        QCOMPARE(BetterSessionsPlugin::osColor(QStringLiteral("iOS")), QString());
        QCOMPARE(BetterSessionsPlugin::osColor(QStringLiteral("Horizon OS")), QStringLiteral("#0081fb"));
        QCOMPARE(BetterSessionsPlugin::osColor(QStringLiteral("Plan 9")), QStringLiteral("#f3799a"));
        // Chrome's, Discord's and the unknown icon differ by their view boxes
        QVERIFY(BetterSessionsPlugin::platformIconSvg(QStringLiteral("Chrome iOS")).contains("0 0 512 512"));
        QVERIFY(BetterSessionsPlugin::platformIconSvg(QStringLiteral("Discord VR")).contains("M13.545 2.907"));
        QVERIFY(BetterSessionsPlugin::platformIconSvg(QStringLiteral("Lynx")).contains("M4.475 5.458"));
        QVERIFY(BetterSessionsPlugin::platformIconSvg(QStringLiteral("Android Mobile")).contains("M15.5 1h-8"));
        QCOMPARE(BetterSessionsPlugin::defaultName(session("a")), QStringLiteral("Windows · Discord Client"));
    }

    void newSessionsNotifyOnce()
    {
        SinglePluginHost h(&qord_register_bettersessions);
        start(h, { session("a"), session("b", "Android", "Discord Android") });
        QCOMPARE(h.ui.notifications.size(), 2);
        QCOMPARE(h.ui.notifications[1].title, QStringLiteral("BetterSessions"));
        QCOMPARE(h.ui.notifications[1].body,
                 QStringLiteral("New session:\nAndroid · Discord Android · Amsterdam, NH, Netherlands"));
        QVERIFY(h.ui.notifications[1].permanent);
        h.ui.notificationClicks[1]();
        QCOMPARE(h.ui.devicesOpened, 1);

        // Remembered across restarts: nothing new the second time
        QVERIFY(h.host->setEnabled(h.id, false));
        h.sessions.fetches.clear();
        start(h, { session("a"), session("b", "Android", "Discord Android") });
        QCOMPARE(h.ui.notifications.size(), 2);
    }

    void viewNamesAndBadges()
    {
        SinglePluginHost h(&qord_register_bettersessions);
        start(h, { session("a") });
        DeviceSessionView v = view(h, session("a"));
        QCOMPARE(v.title, QStringLiteral("Windows · Discord Client"));
        QVERIFY(v.isNew);
        QCOMPARE(v.iconColor, QStringLiteral("#55a6ef"));
        QCOMPARE(v.badgeSvg, BetterSessionsPlugin::platformIconSvg(QStringLiteral("Discord Client")));
        QVERIFY(!v.timeTooltip.isEmpty());
        QCOMPARE(v.buttons.size(), 1);
        QCOMPARE(v.buttons[0].label, QStringLiteral("Rename"));

        // Rename shows the name with a *; an empty name brings back the default
        h.host->dispatchDeviceSessionButton(h.id, 1, v.buttons[0].id, session("a"));
        QCOMPARE(h.ui.promptTitle, QStringLiteral("Rename"));
        h.ui.pendingPrompt(QStringLiteral("Desk PC"));
        QVERIFY(h.sessions.refreshes > 0);
        v = view(h, session("a"));
        QCOMPARE(v.title, QStringLiteral("Desk PC*"));
        QVERIFY(!v.isNew); // renaming counts as seen
        h.host->dispatchDeviceSessionButton(h.id, 1, v.buttons[0].id, session("a"));
        QCOMPARE(h.ui.promptInitial, QStringLiteral("Desk PC"));
        h.ui.pendingPrompt(QString());
        QCOMPARE(view(h, session("a")).title, QStringLiteral("Windows · Discord Client"));
    }

    void closingThePageMarksSeenAndForgets()
    {
        SinglePluginHost h(&qord_register_bettersessions);
        start(h, { session("a"), session("gone") });
        h.host->dispatchDevicesPageClosed(1, { session("a"), session("c") });
        QVERIFY(!view(h, session("a")).isNew);
        QVERIFY(!view(h, session("c")).isNew);
        const QJsonObject stored = h.host->settings(h.id)->value(QStringLiteral("savedSessions")).toObject();
        const QJsonObject account = stored.value(QStringLiteral("1")).toObject();
        QCOMPARE(account.keys(), (QStringList{ QStringLiteral("a"), QStringLiteral("c") }));
    }

    void backgroundCheckFollowsSessionChanges()
    {
        SinglePluginHost h(&qord_register_bettersessions);
        start(h, { session("a") });
        // Off by default
        h.host->dispatchDeviceSessionsChanged(1);
        QVERIFY(h.sessions.pending.isEmpty());

        auto *s = h.host->settings(h.id);
        s->setValue(QStringLiteral("backgroundCheck"), true);
        s->setValue(QStringLiteral("checkInterval"), 0.001); // 60 ms
        QTest::qWait(80);
        h.host->dispatchDeviceSessionsChanged(1);
        QCOMPARE(h.sessions.pending.size(), 1);
        h.sessions.pending.takeFirst()(true, { session("a"), session("new", "Linux", "Firefox") });
        QCOMPARE(h.ui.notifications.size(), 2);
        // A change right after waits for the interval, then checks once
        h.host->dispatchDeviceSessionsChanged(1);
        h.host->dispatchDeviceSessionsChanged(1);
        QVERIFY(h.sessions.pending.isEmpty());
        QTRY_COMPARE(h.sessions.pending.size(), 1);
    }

    void accountsConnectingLaterAreChecked()
    {
        SinglePluginHost h(&qord_register_bettersessions);
        h.sessions.accounts.clear(); // none connected yet
        QVERIFY(h.enableWithAllPermissions());
        QVERIFY(h.sessions.fetches.isEmpty());
        // The first report after the account connects, even without Background check
        h.host->dispatchDeviceSessionsChanged(7);
        QCOMPARE(h.sessions.fetches, QList<Id>{ 7 });
        h.sessions.pending.takeFirst()(true, { session("a") });
        QCOMPARE(h.ui.notifications.size(), 1);
        h.host->dispatchDeviceSessionsChanged(7);
        QCOMPARE(h.sessions.fetches.size(), 1);
    }

    void needsTheSessionsPermission()
    {
        SinglePluginHost h(&qord_register_bettersessions);
        h.host->setGranted(h.id, Permission::AddUiElements);
        QVERIFY(h.host->setEnabled(h.id, true));
        QVERIFY(h.sessions.fetches.isEmpty());
        QVERIFY(!h.host->dispatchDeviceSessionView(1, session("a")));
    }

    void answersAfterStopAreDropped()
    {
        SinglePluginHost h(&qord_register_bettersessions);
        QVERIFY(h.enableWithAllPermissions());
        QCOMPARE(h.sessions.pending.size(), 1);
        auto delayed = h.sessions.pending.takeFirst();
        const auto stored = h.host->settings(h.id)->value(QStringLiteral("savedSessions"));
        const int refreshes = h.sessions.refreshes;
        QVERIFY(h.host->setEnabled(h.id, false));

        // The service can finish after the host has destroyed the plugin.
        delayed(true, { session("late") });
        QVERIFY(h.ui.notifications.isEmpty());
        QCOMPARE(h.sessions.refreshes, refreshes);
        QCOMPARE(h.host->settings(h.id)->value(QStringLiteral("savedSessions")), stored);

        // A new incarnation must not make callbacks from the previous one live.
        QVERIFY(h.enableWithAllPermissions());
        delayed(true, { session("stale") });
        QVERIFY(h.ui.notifications.isEmpty());
        QCOMPARE(h.host->settings(h.id)->value(QStringLiteral("savedSessions")), stored);
        QCOMPARE(h.sessions.pending.size(), 1);
        h.sessions.pending.takeFirst()(true, { session("current") });
        QCOMPARE(h.ui.notifications.size(), 1);
        QCOMPARE(h.sessions.refreshes, refreshes + 1);
        const auto account = h.host->settings(h.id)->value(QStringLiteral("savedSessions"))
                                 .toObject().value(QStringLiteral("1")).toObject();
        QVERIFY(account.contains(QStringLiteral("current")));
        QVERIFY(!account.contains(QStringLiteral("late")));
        QVERIFY(!account.contains(QStringLiteral("stale")));
    }
};

QTEST_GUILESS_MAIN(TestBetterSessions)
#include "tst_bettersessions.moc"
