#include "GameActivityToggle.hpp"
#include "PluginTestUtils.hpp"

#include <QSignalSpy>
#include <QTest>

namespace Qord::Plugins {
void qord_register_gameactivitytoggle(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

class TestGameActivityToggle : public QObject
{
    Q_OBJECT

    static constexpr Id Account = 1;
    const UserPanelContext panel{ Account };

    static UserPanelButton button(SinglePluginHost &h, const UserPanelContext &panel)
    {
        const auto buttons = h.host->dispatchUserPanelButtons(panel);
        return buttons.isEmpty() ? UserPanelButton() : buttons.first().button;
    }

private slots:
    void buttonFollowsTheSetting()
    {
        SinglePluginHost h(&qord_register_gameactivitytoggle);
        QVERIFY(h.host->dispatchUserPanelButtons(panel).isEmpty()); // off
        QVERIFY(h.enableWithAllPermissions());

        UserPanelButton b = button(h, panel);
        QCOMPARE(b.tooltip, QStringLiteral("Disable Game Activity"));
        QVERIFY(!b.danger);
        QCOMPARE(b.svg, GameActivityTogglePlugin::iconSvg(true, false));
        QVERIFY(!b.svg.contains("#f23f43"));

        // Clicking switches Discord's setting and asks for a redraw
        QSignalSpy redraw(h.host.get(), &PluginHost::userPanelChanged);
        h.host->dispatchUserPanelButtonClicked(h.id, b.id, panel);
        QVERIFY(!h.activity.showGame);
        QCOMPARE(h.activity.gameChanges, 1);
        QCOMPARE(redraw.size(), 1);
        b = button(h, panel);
        QCOMPARE(b.tooltip, QStringLiteral("Enable Game Activity"));
        QVERIFY(b.danger);
        QCOMPARE(b.svg, GameActivityTogglePlugin::iconSvg(false, false));

        h.host->dispatchUserPanelButtonClicked(h.id, b.id, panel);
        QVERIFY(h.activity.showGame);
        // Other ids do nothing
        h.host->dispatchUserPanelButtonClicked(h.id, QStringLiteral("other"), panel);
        QCOMPARE(h.activity.gameChanges, 2);
    }

    void icons()
    {
        // Upstream's new icon: a red controller and slash; the old one keeps
        // the controller in the icon color with a red slash
        const QByteArray off = GameActivityTogglePlugin::iconSvg(false, false);
        QCOMPARE(off.count("#f23f43"), 2);
        QVERIFY(off.contains("M22.7 2.7a1 1 0 0 0-1.4-1.4l-20 20a1 1 0 1 0 1.4 1.4Z"));
        const QByteArray oldOff = GameActivityTogglePlugin::iconSvg(false, true);
        QCOMPARE(oldOff.count("#f23f43"), 1);
        QVERIFY(oldOff.contains("currentColor"));
        QVERIFY(oldOff.contains("M23 2.27 21.73 1 1 21.73 2.27 23 23 2.27Z"));
        QVERIFY(!GameActivityTogglePlugin::iconSvg(true, true).contains("#f23f43"));

        SinglePluginHost h(&qord_register_gameactivitytoggle);
        QVERIFY(h.enableWithAllPermissions());
        h.activity.showGame = false;
        h.host->settings(h.id)->setValue(QStringLiteral("oldIcon"), true);
        QCOMPARE(button(h, panel).svg, oldOff);
    }

    void spotifyMenu()
    {
        SinglePluginHost h(&qord_register_gameactivitytoggle);
        QVERIFY(h.enableWithAllPermissions());
        const QString id = button(h, panel).id;
        QCOMPARE(h.activity.connectionFetches, 1);
        // Without Spotify there is no menu
        h.activity.pendingFetches.takeFirst()(true, { { "github", "g", "octo", true, false } });
        QVERIFY(h.host->dispatchUserPanelButtonMenu(h.id, id, panel).isEmpty());
        h.activity.pendingFetches.takeFirst()(
                true, { { "spotify", "s1", "ava", true, false }, { "spotify", "s2", "old", false, true } });

        // One working account (revoked ones are left out)
        auto items = h.host->dispatchUserPanelButtonMenu(h.id, id, panel);
        QCOMPARE(items.size(), 1);
        QCOMPARE(items[0].label, QStringLiteral("Share Spotify Activity"));
        QCOMPARE(items[0].checked, std::optional<bool>(true));
        h.host->dispatchUserPanelButtonMenuAction(h.id, id, items[0].id, panel);
        QCOMPARE(h.activity.activityChanges.size(), 1);
        QCOMPARE(h.activity.activityChanges[0].type, QStringLiteral("spotify"));
        QCOMPARE(h.activity.activityChanges[0].id, QStringLiteral("s1"));
        QVERIFY(!h.activity.activityChanges[0].show);
        // Shown switched at once, before Discord answers
        h.activity.pendingFetches.clear();
        items = h.host->dispatchUserPanelButtonMenu(h.id, id, panel);
        QCOMPARE(items[0].checked, std::optional<bool>(false));

        // Several accounts are named
        h.activity.pendingFetches.takeFirst()(
                true, { { "spotify", "s1", "ava", true, false }, { "spotify", "s3", "work", false, false } });
        items = h.host->dispatchUserPanelButtonMenu(h.id, id, panel);
        QCOMPARE(items.size(), 2);
        QCOMPARE(items[1].label, QStringLiteral("Share Spotify Activity (work)"));
        QCOMPARE(items[1].checked, std::optional<bool>(false));
    }

    void answersAfterStopAreDropped()
    {
        SinglePluginHost h(&qord_register_gameactivitytoggle);
        QVERIFY(h.enableWithAllPermissions());
        (void)button(h, panel);
        QVERIFY(h.host->setEnabled(h.id, false));
        h.activity.pendingFetches.takeFirst()(true, { { "spotify", "s1", "ava", true, false } });
        QVERIFY(h.host->dispatchUserPanelButtons(panel).isEmpty());
    }

    void needsActivityPermission()
    {
        SinglePluginHost h(&qord_register_gameactivitytoggle);
        h.host->setGranted(h.id, Permission::AddUiElements);
        QVERIFY(h.host->setEnabled(h.id, true));
        QVERIFY(h.host->dispatchUserPanelButtons(panel).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestGameActivityToggle)
#include "tst_gameactivitytoggle.moc"
