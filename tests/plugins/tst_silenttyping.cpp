#include "PluginTestUtils.hpp"

#include "SilentTyping.hpp"

#include <QTest>

namespace Qord::Plugins {
void qord_register_silenttyping(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

static TypingContext typing(Id channel, Id guild = 0)
{
    TypingContext t;
    t.accountId = 1;
    t.channelId = channel;
    t.guildId = guild;
    return t;
}

class TestSilentTyping : public QObject
{
    Q_OBJECT

private slots:
    void parsesLocations()
    {
        QCOMPARE(SilentTyping::parseLocations(" 1, 2,,3 "), (QStringList { "1", "2", "3" }));
        QVERIFY(SilentTyping::parseLocations("").isEmpty());
    }

    void defaultHiddenHidesEverywhereExceptListed()
    {
        SilentTyping::Config c;
        const QDateTime now = QDateTime::currentDateTimeUtc();
        QVERIFY(!SilentTyping::shouldSendTyping(c, typing(10, 100), now));
        c.disabledLocations = QStringList { QStringLiteral("100") }; // whole guild allowed
        QVERIFY(SilentTyping::shouldSendTyping(c, typing(10, 100), now));
        c.disabledLocations = QStringList { QStringLiteral("10") }; // just the channel
        QVERIFY(SilentTyping::shouldSendTyping(c, typing(10, 100), now));
        QVERIFY(!SilentTyping::shouldSendTyping(c, typing(11, 100), now));
        // Disabled globally: typing is always visible
        c.enabledGlobally = false;
        QVERIFY(SilentTyping::shouldSendTyping(c, typing(11, 100), now));
    }

    void defaultShownHidesOnlyListed()
    {
        SilentTyping::Config c;
        c.defaultHidden = false;
        const QDateTime now = QDateTime::currentDateTimeUtc();
        QVERIFY(SilentTyping::shouldSendTyping(c, typing(10, 100), now));
        c.enabledLocations = QStringList { QStringLiteral("100") };
        QVERIFY(!SilentTyping::shouldSendTyping(c, typing(10, 100), now));
        QVERIFY(SilentTyping::shouldSendTyping(c, typing(10, 200), now));
    }

    void implicitExceptions()
    {
        SilentTyping::Config c;
        const QDateTime now = QDateTime::currentDateTimeUtc();
        TypingContext t = typing(10, 100);
        t.connectedVoiceChannelId = 10;
        QCOMPARE(SilentTyping::implicitlyAllowed(c, t, now), 0);
        c.alwaysEnableInActiveVoiceChat = true;
        QCOMPARE(SilentTyping::implicitlyAllowed(c, t, now), 1);
        QVERIFY(SilentTyping::shouldSendTyping(c, t, now));

        c.alwaysEnableInActiveVoiceChat = false;
        t.lastOwnMessageAt = now.addSecs(-5);
        QCOMPARE(SilentTyping::implicitlyAllowed(c, t, now), 0); // threshold 0 = off
        c.thresholdServers = 10;
        QCOMPARE(SilentTyping::implicitlyAllowed(c, t, now), 2);
        c.thresholdServers = 3;
        QCOMPARE(SilentTyping::implicitlyAllowed(c, t, now), 0);
        // DMs use their own threshold
        TypingContext dm = typing(20);
        dm.lastOwnMessageAt = now.addSecs(-5);
        c.thresholdDirectMessages = 10;
        QCOMPARE(SilentTyping::implicitlyAllowed(c, dm, now), 2);
    }

    void tooltips()
    {
        SilentTyping::Config c;
        QCOMPARE(SilentTyping::tooltip(c, 10, 100, 0), QStringLiteral("Typing Hidden (Global)"));
        c.disabledLocations = QStringList { QStringLiteral("100") };
        QCOMPARE(SilentTyping::tooltip(c, 10, 100, 0), QStringLiteral("Typing Visible (Guild)"));
        c.disabledLocations = QStringList { QStringLiteral("10") };
        QCOMPARE(SilentTyping::tooltip(c, 10, 100, 0), QStringLiteral("Typing Visible (Channel)"));
        QCOMPARE(SilentTyping::tooltip(c, 10, 100, 2),
                 QStringLiteral("Typing Temporarily Visible In This Channel Due To A Recent Message"));
        c.enabledGlobally = false;
        QCOMPARE(SilentTyping::tooltip(c, 10, 100, 0), QStringLiteral("Typing Visible (Global)"));
    }

    void pluginClicksAndHooks()
    {
        SinglePluginHost h(qord_register_silenttyping);
        QVERIFY(h.info()->loadError.isEmpty());
        QVERIFY(h.host->dispatchShouldSendTyping(typing(10, 100))); // disabled
        QVERIFY(h.enableWithAllPermissions());
        QVERIFY(!h.host->dispatchShouldSendTyping(typing(10, 100)));

        ChatContext chat;
        chat.accountId = 1;
        chat.channelId = 10;
        chat.guildId = 100;
        auto buttons = h.host->dispatchChatBarButtons(chat);
        QCOMPARE(buttons.size(), 1);
        QCOMPARE(buttons[0].button.tooltip, QStringLiteral("Typing Hidden (Global)"));

        // Left click (default "channel") allows this channel
        h.host->dispatchChatBarButtonClicked(h.id, "toggle", chat);
        QCOMPARE(h.host->settings(h.id)->stringValue("disabledLocations"), QStringLiteral("10"));
        QVERIFY(h.host->dispatchShouldSendTyping(typing(10, 100)));
        // ...and again removes it
        h.host->dispatchChatBarButtonClicked(h.id, "toggle", chat);
        QCOMPARE(h.host->settings(h.id)->stringValue("disabledLocations"), QString());

        // Right click (default "global") turns the plugin's effect off
        chat.click = ChatContext::Click::Right;
        h.host->dispatchChatBarButtonClicked(h.id, "toggle", chat);
        QVERIFY(!h.host->settings(h.id)->boolValue("enabledGlobally"));
        QVERIFY(h.host->dispatchShouldSendTyping(typing(10, 100)));

        // Middle click (default "settings") opens the plugin's settings
        chat.click = ChatContext::Click::Middle;
        h.host->dispatchChatBarButtonClicked(h.id, "toggle", chat);
        QCOMPARE(h.ui.settingsOpened, QStringList { h.id });

        // Others' typing stays visible unless asked
        QVERIFY(h.host->dispatchShouldShowTypers(chat));
        QVERIFY(h.host->settings(h.id)->setValue("hideChatBoxTypingIndicators", true).isEmpty());
        QVERIFY(!h.host->dispatchShouldShowTypers(chat));

        // No chat icon when switched off
        QVERIFY(h.host->settings(h.id)->setValue("chatIcon", false).isEmpty());
        QVERIFY(h.host->dispatchChatBarButtons(chat).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestSilentTyping)
#include "tst_silenttyping.moc"
