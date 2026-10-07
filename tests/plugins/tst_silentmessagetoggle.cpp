#include "PluginTestUtils.hpp"

#include "SilentMessageToggle.hpp"

#include <QTest>

namespace Qord::Plugins {
void qord_register_silentmessagetoggle(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

static ChatContext in(Id channel)
{
    ChatContext c;
    c.channelId = channel;
    return c;
}

static OutgoingMessage msg(Id channel, const QString &text)
{
    OutgoingMessage m;
    m.channelId = channel;
    m.content = text;
    return m;
}

class TestSilentMessageToggle : public QObject
{
    Q_OBJECT

private slots:
    void prefixAndAutoDisable()
    {
        SinglePluginHost h(qord_register_silentmessagetoggle);
        QVERIFY(h.info()->loadError.isEmpty());
        QVERIFY(h.enableWithAllPermissions());
        auto b = h.host->dispatchChatBarButtons(in(1));
        QCOMPARE(b.size(), 1);
        QCOMPARE(b[0].button.tooltip, QStringLiteral("Enable Silent Message"));

        h.host->dispatchChatBarButtonClicked(h.id, "toggle", in(1));
        QVERIFY(h.host->dispatchChatBarButtons(in(1))[0].button.active);
        OutgoingMessage m = msg(1, "hi");
        h.host->dispatchBeforeSend(m);
        QCOMPARE(m.content, QStringLiteral("@silent hi"));
        // autoDisable (default on) turns it off after one message
        QVERIFY(!h.host->dispatchChatBarButtons(in(1))[0].button.active);
        m = msg(1, "hi");
        h.host->dispatchBeforeSend(m);
        QCOMPARE(m.content, QStringLiteral("hi"));
    }

    void noDoublePrefixAndStaysOn()
    {
        SinglePluginHost h(qord_register_silentmessagetoggle);
        QVERIFY(h.host->settings(h.id)->setValue("autoDisable", false).isEmpty());
        QVERIFY(h.enableWithAllPermissions());
        h.host->dispatchChatBarButtonClicked(h.id, "toggle", in(1));
        OutgoingMessage m = msg(1, "@silent hi");
        h.host->dispatchBeforeSend(m);
        QCOMPARE(m.content, QStringLiteral("@silent hi"));
        m = msg(1, "again");
        h.host->dispatchBeforeSend(m);
        QCOMPARE(m.content, QStringLiteral("@silent again"));
    }

    void persistenceModes()
    {
        SinglePluginHost h(qord_register_silentmessagetoggle);
        QVERIFY(h.host->settings(h.id)->setValue("autoDisable", false).isEmpty());
        QVERIFY(h.enableWithAllPermissions());

        // none: switching channels resets the toggle
        h.host->dispatchChatBarButtons(in(1));
        h.host->dispatchChatBarButtonClicked(h.id, "toggle", in(1));
        QVERIFY(!h.host->dispatchChatBarButtons(in(2))[0].button.active);

        // channels: kept across channels, not across restarts
        QVERIFY(h.host->settings(h.id)->setValue("persistState", "channels").isEmpty());
        h.host->dispatchChatBarButtonClicked(h.id, "toggle", in(2));
        QVERIFY(h.host->dispatchChatBarButtons(in(3))[0].button.active);
        QVERIFY(h.host->setEnabled(h.id, false));
        QVERIFY(h.host->setEnabled(h.id, true));
        QVERIFY(!h.host->dispatchChatBarButtons(in(3))[0].button.active);

        // restarts: kept across a restart of the plugin
        QVERIFY(h.host->settings(h.id)->setValue("persistState", "restarts").isEmpty());
        h.host->dispatchChatBarButtonClicked(h.id, "toggle", in(3));
        QVERIFY(h.host->setEnabled(h.id, false));
        QVERIFY(h.host->setEnabled(h.id, true));
        QVERIFY(h.host->dispatchChatBarButtons(in(4))[0].button.active);

        // switching back to "none" turns it off (upstream's onChange)
        QVERIFY(h.host->settings(h.id)->setValue("persistState", "none").isEmpty());
        QVERIFY(!h.host->dispatchChatBarButtons(in(4))[0].button.active);
    }

    void hiddenStateHasNoEditor()
    {
        SinglePluginHost h(qord_register_silentmessagetoggle);
        const SettingSpec *last = h.host->settings(h.id)->spec("lastState");
        QVERIFY(last);
        QVERIFY(last->hidden);
    }
};

QTEST_GUILESS_MAIN(TestSilentMessageToggle)
#include "tst_silentmessagetoggle.moc"
