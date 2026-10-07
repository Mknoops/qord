#include "PluginTestUtils.hpp"

#include "TalkInReverse.hpp"

#include <QSignalSpy>
#include <QTest>

namespace Qord::Plugins {
void qord_register_talkinreverse(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

class TestTalkInReverse : public QObject
{
    Q_OBJECT

private slots:
    void reverse()
    {
        QCOMPARE(TalkInReverse::reverse("hello"), QStringLiteral("olleh"));
        QCOMPARE(TalkInReverse::reverse(""), QString());
        // Emoji (surrogate pairs) and combining marks stay intact
        QCOMPARE(TalkInReverse::reverse(QString::fromUtf8("a\xF0\x9F\x98\x80" "b")),
                 QString::fromUtf8("b\xF0\x9F\x98\x80" "a"));
        QCOMPARE(TalkInReverse::reverse(QString::fromUtf8("e\xCC\x81x")), QString::fromUtf8("xe\xCC\x81"));
    }

    void toggleAndSend()
    {
        SinglePluginHost h(qord_register_talkinreverse);
        QVERIFY(h.info()->loadError.isEmpty());
        QVERIFY(h.enableWithAllPermissions());
        ChatContext chat;
        chat.channelId = 1;
        auto buttons = h.host->dispatchChatBarButtons(chat);
        QCOMPARE(buttons.size(), 1);
        QVERIFY(!buttons[0].button.active);
        QCOMPARE(buttons[0].button.tooltip, QStringLiteral("Enable Reverse Message"));

        OutgoingMessage m;
        m.content = "abc";
        h.host->dispatchBeforeSend(m);
        QCOMPARE(m.content, QStringLiteral("abc"));

        QSignalSpy refresh(h.host.get(), &PluginHost::chatBarChanged);
        h.host->dispatchChatBarButtonClicked(h.id, "toggle", chat);
        QCOMPARE(refresh.count(), 1);
        QVERIFY(h.host->dispatchChatBarButtons(chat)[0].button.active);
        h.host->dispatchBeforeSend(m);
        QCOMPARE(m.content, QStringLiteral("cba"));

        // Survives a channel switch, like upstream's module-level state
        chat.channelId = 2;
        QVERIFY(h.host->dispatchChatBarButtons(chat)[0].button.active);
    }

    void buttonsNeedUiPermission()
    {
        SinglePluginHost h(qord_register_talkinreverse);
        h.host->setGranted(h.id, Permission::ModifyOutgoingMessages);
        QVERIFY(h.host->setEnabled(h.id, true));
        QVERIFY(h.host->dispatchChatBarButtons({}).isEmpty());
        // Clicks for a plugin without the permission are ignored
        h.host->dispatchChatBarButtonClicked(h.id, "toggle", {});
        OutgoingMessage m;
        m.content = "abc";
        h.host->dispatchBeforeSend(m);
        QCOMPARE(m.content, QStringLiteral("abc"));
    }
};

QTEST_GUILESS_MAIN(TestTalkInReverse)
#include "tst_talkinreverse.moc"
