#include "UserVoiceShow.hpp"
#include "PluginTestUtils.hpp"

#include <QTest>

namespace Qord::Plugins {
void qord_register_uservoiceshow(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

class TestUserVoiceShow : public QObject
{
    Q_OBJECT

    static constexpr Id Lounge = 500;
    static constexpr Id Stage = 501;
    static constexpr Id Call = 600;

    static DecorationContext user(Id userId, DecorationContext::Place place = DecorationContext::Place::Message)
    {
        DecorationContext u;
        u.accountId = 1;
        u.guildId = 10;
        u.userId = userId;
        u.place = place;
        return u;
    }

    static void setUp(SinglePluginHost &h)
    {
        LinkedChannel lounge;
        lounge.channelId = Lounge;
        lounge.guildId = 10;
        lounge.type = 2;
        lounge.name = QStringLiteral("Lounge");
        lounge.canView = lounge.canConnect = true;
        h.voiceStates.channels.insert(Lounge, lounge);
        LinkedChannel stage = lounge;
        stage.channelId = Stage;
        stage.name = QStringLiteral("Stage <1>");
        stage.canConnect = false;
        h.voiceStates.channels.insert(Stage, stage);
        LinkedChannel call;
        call.channelId = Call;
        call.type = 1;
        call.name = QStringLiteral("dan");
        call.canView = call.canConnect = true;
        h.voiceStates.channels.insert(Call, call);

        h.voiceStates.states.insert(42, { 10, Lounge, false, false });
        h.voiceStates.states.insert(43, { 10, Lounge, true, false });
        h.voiceStates.states.insert(44, { 10, Lounge, true, true });
        h.voiceStates.states.insert(45, { 10, Stage, false, false });
        h.voiceStates.states.insert(46, { 0, Call, false, false });
        h.voiceStates.names.insert(42, QStringLiteral("Ava"));
        h.voiceStates.names.insert(43, QStringLiteral("Ben & co"));
        h.voiceStates.names.insert(44, QStringLiteral("Cy"));
        QVERIFY(h.enableWithAllPermissions());
    }

private slots:
    void iconFollowsVoiceState()
    {
        SinglePluginHost h(&qord_register_uservoiceshow);
        setUp(h);
        auto icon = [&](Id userId) {
            const auto d = h.host->dispatchNameDecorations(user(userId));
            return d.isEmpty() ? QByteArray() : d.first().svg;
        };
        QVERIFY(h.host->dispatchNameDecorations(user(99)).isEmpty()); // not in voice
        QCOMPARE(icon(42), UserVoiceShowPlugin::iconSvg(UserVoiceShowPlugin::Icon::Speaker, 16));
        QCOMPARE(icon(43), UserVoiceShowPlugin::iconSvg(UserVoiceShowPlugin::Icon::Muted, 16));
        // Deafened wins over muted
        QCOMPARE(icon(44), UserVoiceShowPlugin::iconSvg(UserVoiceShowPlugin::Icon::Deaf, 16));
        // A channel you can't connect to is locked
        QCOMPARE(icon(45), UserVoiceShowPlugin::iconSvg(UserVoiceShowPlugin::Icon::LockedSpeaker, 16));
        // DM calls are never locked
        QCOMPARE(icon(46), UserVoiceShowPlugin::iconSvg(UserVoiceShowPlugin::Icon::Speaker, 16));

        const auto d = h.host->dispatchNameDecorations(user(42)).first();
        QCOMPARE(d.color, QStringLiteral("icon"));
        QCOMPARE(d.width, 16);
        QCOMPARE(d.clickId, QString::number(Lounge));
        QCOMPARE(d.pluginId, QStringLiteral("uservoiceshow"));
    }

    void tooltipNamesChannelAndPeople()
    {
        SinglePluginHost h(&qord_register_uservoiceshow);
        setUp(h);
        QCOMPARE(h.host->dispatchNameDecorations(user(42)).first().tooltip,
                 QStringLiteral("<b>In Voice Chat</b><br><b>\U0001F50A Lounge</b><br>Ava, Ben &amp; co, Cy"));
        // Names are escaped; ones the client doesn't know are left out
        QCOMPARE(h.host->dispatchNameDecorations(user(45)).first().tooltip,
                 QStringLiteral("<b>In Voice Chat</b><br><b>\U0001F50A Stage &lt;1&gt;</b>"));
        QCOMPARE(h.host->dispatchNameDecorations(user(46)).first().tooltip,
                 QStringLiteral("<b>In Voice Chat</b><br><b>@dan</b>"));

        // At most 13 people, then how many more
        for (Id id = 100; id < 115; ++id) {
            h.voiceStates.states.insert(id, { 10, Lounge, false, false });
            h.voiceStates.names.insert(id, QStringLiteral("u%1").arg(id));
        }
        const QString tip = h.host->dispatchNameDecorations(user(42)).first().tooltip;
        QVERIFY(tip.endsWith(QStringLiteral(", u109, +5")));
    }

    void hiddenChannelsAreSkipped()
    {
        SinglePluginHost h(&qord_register_uservoiceshow);
        setUp(h);
        h.voiceStates.channels[Stage].canView = false;
        // Without ShowHiddenChannels nothing shows
        QVERIFY(h.host->dispatchNameDecorations(user(45)).isEmpty());
    }

    void settingsPickPlaces()
    {
        SinglePluginHost h(&qord_register_uservoiceshow);
        setUp(h);
        auto *s = h.host->settings(h.id);
        using Place = DecorationContext::Place;
        const QList<QPair<QString, Place>> places = { { QStringLiteral("showInMessages"), Place::Message },
                                                      { QStringLiteral("showInMemberList"), Place::MemberList },
                                                      { QStringLiteral("showInUserProfileModal"), Place::Profile } };
        for (const auto &[key, place] : places) {
            QCOMPARE(h.host->dispatchNameDecorations(user(42, place)).size(), 1);
            s->setValue(key, false);
            QVERIFY(h.host->dispatchNameDecorations(user(42, place)).isEmpty());
        }
    }

    void clickOpensDoubleClickJoins()
    {
        SinglePluginHost h(&qord_register_uservoiceshow);
        setUp(h);
        const QString click = QString::number(Lounge);
        h.host->dispatchNameDecorationClicked(h.id, click, user(42), false);
        QVERIFY(h.ui.openedChannels.isEmpty()); // waits for a second click
        QTRY_COMPARE_WITH_TIMEOUT(h.ui.openedChannels.size(), 1, 1000);
        QCOMPARE(h.ui.openedChannels.first(), qMakePair(Id(1), Lounge));
        QVERIFY(h.ui.joinedVoice.isEmpty());

        // The double click cancels the pending open and joins
        h.host->dispatchNameDecorationClicked(h.id, click, user(42), false);
        h.host->dispatchNameDecorationClicked(h.id, click, user(42), true);
        QCOMPARE(h.ui.joinedVoice, (QList<QPair<Id, Id>>{ { 1, Lounge } }));
        QTest::qWait(UserVoiceShowPlugin::ClickDelayMs + 100);
        QCOMPARE(h.ui.openedChannels.size(), 1);

        // Joining needs Connect; DM calls don't
        h.host->dispatchNameDecorationClicked(h.id, click, user(45), true);
        QCOMPARE(h.ui.toasts.size(), 1);
        QCOMPARE(h.ui.toasts.first().first, QStringLiteral("You cannot join the user's Voice Channel"));
        QCOMPARE(h.ui.toasts.first().second, UiService::ToastType::Failure);
        QCOMPARE(h.ui.joinedVoice.size(), 1);
        h.host->dispatchNameDecorationClicked(h.id, click, user(46), true);
        QCOMPARE(h.ui.joinedVoice.last(), qMakePair(Id(1), Call));
    }

    void clicksForOtherPluginsAreIgnored()
    {
        SinglePluginHost h(&qord_register_uservoiceshow);
        setUp(h);
        h.host->dispatchNameDecorationClicked(QStringLiteral("someoneelse"), QStringLiteral("x"), user(42), true);
        QVERIFY(h.ui.joinedVoice.isEmpty());
    }

    void stopCancelsPendingOpen()
    {
        SinglePluginHost h(&qord_register_uservoiceshow);
        setUp(h);
        h.host->dispatchNameDecorationClicked(h.id, QString::number(Lounge), user(42), false);
        QString error;
        QVERIFY(h.host->setEnabled(h.id, false, &error));
        QTest::qWait(UserVoiceShowPlugin::ClickDelayMs + 100);
        QVERIFY(h.ui.openedChannels.isEmpty());
    }

    void needsVoiceStatePermission()
    {
        SinglePluginHost h(&qord_register_uservoiceshow);
        setUp(h);
        QString error;
        QVERIFY(h.host->setEnabled(h.id, false, &error));
        h.host->setGranted(h.id, Permission::ReadPresences | Permission::AddUiElements);
        QVERIFY(h.host->setEnabled(h.id, true, &error));
        QVERIFY(h.host->dispatchNameDecorations(user(42)).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestUserVoiceShow)
#include "tst_uservoiceshow.moc"
