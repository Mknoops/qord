#include "MessageClickActions.hpp"
#include "PluginTestUtils.hpp"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

namespace Qord::Plugins {
void qord_register_messageclickactions(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

class TestMessageClickActions : public QObject
{
    Q_OBJECT

    static MessageClickContext click(bool own, int detail, qint64 at)
    {
        MessageClickContext c;
        c.message.accountId = 1;
        c.message.guildId = 2;
        c.message.channelId = 3;
        c.message.messageId = 4;
        c.message.authorId = own ? 1 : 9;
        c.message.isOwn = own;
        c.message.content = QStringLiteral("hello\nworld");
        c.detail = detail;
        c.timeMs = at;
        return c;
    }

    // A real double click: down, up (detail 1), down, up (detail 2)
    static void doubleClick(SinglePluginHost &h, MessageClickContext c, qint64 at)
    {
        h.host->dispatchChatMouseDown(at);
        c.detail = 1;
        c.timeMs = at + 40;
        h.host->dispatchMessageClicked(c);
        h.host->dispatchChatMouseDown(at + 100);
        c.detail = 2;
        c.timeMs = at + 140;
        h.host->dispatchMessageClicked(c);
    }

private slots:
    // normalizeEmoji(), the reaction list and quoting, run in Node from upstream's source
    void helpersMatchUpstream()
    {
        QFile file(QStringLiteral(QORD_TEST_DATA_DIR "/messageclickactions-golden.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonObject golden = QJsonDocument::fromJson(file.readAll()).object();
        for (const auto &v : golden[u"normalized"].toArray()) {
            const QJsonObject c = v.toObject();
            QCOMPARE(MessageClickActions::normalizeEmoji(c[u"input"].toString()), c[u"expected"].toString());
        }
        for (const auto &v : golden[u"reactCases"].toArray()) {
            const QJsonObject c = v.toObject();
            QStringList expected;
            for (const auto &e : c[u"expected"].toArray())
                expected.append(e.toString());
            QCOMPARE(MessageClickActions::configuredReactions(c[u"reactEmoji"].toString(), c[u"addAdditionalReacts"].toBool(),
                                                              c[u"additionalReactEmojis"].toString()),
                     expected);
        }
        for (const auto &v : golden[u"quotes"].toArray()) {
            const QJsonObject c = v.toObject();
            QCOMPARE(MessageClickActions::quote(c[u"input"].toString()), c[u"expected"].toString());
        }
    }

    void backspaceClickDeletes()
    {
        SinglePluginHost h(qord_register_messageclickactions);
        QVERIFY(h.info()->loadError.isEmpty());
        QVERIFY(h.enableWithAllPermissions());
        auto c = click(true, 1, 1000);
        h.host->dispatchChatMouseDown(990);
        h.host->dispatchMessageClicked(c); // no Backspace held: nothing
        QVERIFY(h.messageActions.deleted.isEmpty());
        c.backspace = true;
        h.host->dispatchChatMouseDown(2000);
        c.timeMs = 2010;
        h.host->dispatchMessageClicked(c);
        QCOMPARE(h.messageActions.deleted, QList<Id> { 4 });
    }

    void othersNeedManageMessages()
    {
        SinglePluginHost h(qord_register_messageclickactions);
        QVERIFY(h.enableWithAllPermissions());
        auto c = click(false, 1, 1010);
        c.backspace = true;
        h.host->dispatchChatMouseDown(1000);
        h.host->dispatchMessageClicked(c);
        QVERIFY(h.messageActions.deleted.isEmpty());
        c.canManageMessages = true;
        h.host->dispatchChatMouseDown(3000);
        c.timeMs = 3010;
        h.host->dispatchMessageClicked(c);
        QCOMPARE(h.messageActions.deleted.size(), 1);
    }

    void doubleClickEditsOwnAndRepliesToOthers()
    {
        SinglePluginHost h(qord_register_messageclickactions);
        QVERIFY(h.enableWithAllPermissions());
        doubleClick(h, click(true, 2, 0), 1000);
        QCOMPARE(h.ui.edits, QList<Id> { 4 });
        doubleClick(h, click(false, 2, 0), 5000);
        QCOMPARE(h.ui.replies.size(), 1);
        QCOMPARE(h.ui.replies[0].mention, std::optional<bool>());
        QVERIFY(!h.ui.replies[0].shiftHeld);
        // The plain first click of a double click never deletes (it waits for the timeout)
        QTest::qWait(400);
        QVERIFY(h.messageActions.deleted.isEmpty());
    }

    void slowSecondClickSelectsText()
    {
        SinglePluginHost h(qord_register_messageclickactions);
        QVERIFY(h.enableWithAllPermissions());
        auto c = click(true, 2, 0);
        h.host->dispatchChatMouseDown(1000);
        c.detail = 1;
        c.timeMs = 1040;
        h.host->dispatchMessageClicked(c);
        h.host->dispatchChatMouseDown(1100);
        c.detail = 2;
        c.timeMs = 1100 + 400; // held the second press past doubleClickHoldThreshold
        h.host->dispatchMessageClicked(c);
        QVERIFY(h.ui.edits.isEmpty());
    }

    void longHoldIsSelection()
    {
        SinglePluginHost h(qord_register_messageclickactions);
        QVERIFY(h.enableWithAllPermissions());
        auto c = click(true, 1, 0);
        c.backspace = true;
        h.host->dispatchChatMouseDown(1000);
        c.timeMs = 1000 + 500; // past selectionHoldTimeout (300)
        h.host->dispatchMessageClicked(c);
        QVERIFY(h.messageActions.deleted.isEmpty());
    }

    void singleClickWithoutModifierWaitsForDoubleClick()
    {
        SinglePluginHost h(qord_register_messageclickactions);
        QVERIFY(h.enableWithAllPermissions());
        auto *s = h.host->settings(h.id);
        s->setValue(QStringLiteral("singleClickAction"), QStringLiteral("COPY_ID"));
        s->setValue(QStringLiteral("singleClickModifier"), QStringLiteral("NONE"));
        auto c = click(true, 1, 1010);
        h.host->dispatchChatMouseDown(1000);
        h.host->dispatchMessageClicked(c);
        QCOMPARE(h.clipboard.writes, 0); // deferred by clickTimeout
        QTRY_COMPARE_WITH_TIMEOUT(h.clipboard.writes, 1, 1000);
        QCOMPARE(h.clipboard.text, QStringLiteral("4"));
    }

    void tripleClickReacts()
    {
        SinglePluginHost h(qord_register_messageclickactions);
        QVERIFY(h.enableWithAllPermissions());
        auto *s = h.host->settings(h.id);
        auto c = click(false, 3, 1010);
        h.host->dispatchChatMouseDown(1000);
        h.host->dispatchMessageClicked(c); // triple clicks only with deferDoubleClickForTriple
        QVERIFY(h.messageActions.reactions.isEmpty());
        s->setValue(QStringLiteral("deferDoubleClickForTriple"), true);
        s->setValue(QStringLiteral("addAdditionalReacts"), true);
        s->setValue(QStringLiteral("additionalReactEmojis"), QStringLiteral("👍, 🎉"));
        h.host->dispatchChatMouseDown(2000);
        c.timeMs = 2010;
        c.myReactions = { QStringLiteral("💀") }; // already reacted: the first one is removed
        h.host->dispatchMessageClicked(c);
        QCOMPARE(h.messageActions.reactions, QStringList { QStringLiteral("-💀") });
        QTRY_COMPARE_WITH_TIMEOUT(h.messageActions.reactions.size(), 3, 2000);
        QCOMPARE(h.messageActions.reactions[1], QStringLiteral("+👍"));
        QCOMPARE(h.messageActions.reactions[2], QStringLiteral("+🎉"));
    }

    void otherActions()
    {
        SinglePluginHost h(qord_register_messageclickactions);
        QVERIFY(h.enableWithAllPermissions());
        auto *s = h.host->settings(h.id);
        s->setValue(QStringLiteral("doubleClickOthersAction"), QStringLiteral("QUOTE"));
        doubleClick(h, click(false, 2, 0), 1000);
        QCOMPARE(h.ui.inserted.value(0), QStringLiteral("> hello\n> world\n"));
        QCOMPARE(h.ui.replies.value(0).mention, std::optional<bool>(false));

        s->setValue(QStringLiteral("doubleClickOthersAction"), QStringLiteral("COPY_LINK"));
        doubleClick(h, click(false, 2, 0), 5000);
        QCOMPARE(h.clipboard.text, QStringLiteral("https://discord.com/channels/2/3/4"));

        s->setValue(QStringLiteral("doubleClickOthersAction"), QStringLiteral("PIN"));
        auto pin = click(false, 2, 0);
        doubleClick(h, pin, 9000); // no Manage Messages: nothing
        QVERIFY(h.messageActions.pins.isEmpty());
        pin.canManageMessages = true;
        pin.pinned = true;
        doubleClick(h, pin, 13000);
        QCOMPARE(h.messageActions.pins.value(0).second, false);
    }

    void disabledInDms()
    {
        SinglePluginHost h(qord_register_messageclickactions);
        QVERIFY(h.enableWithAllPermissions());
        h.host->settings(h.id)->setValue(QStringLiteral("disableInDms"), true);
        auto c = click(true, 2, 0);
        c.message.isDm = true;
        c.message.guildId = 0;
        doubleClick(h, c, 1000);
        QVERIFY(h.ui.edits.isEmpty());
    }

    void needsPermissions()
    {
        SinglePluginHost h(qord_register_messageclickactions);
        h.host->setGranted(h.id, Permission::AddUiElements);
        QVERIFY(h.host->setEnabled(h.id, true));
        auto c = click(true, 1, 1010);
        c.backspace = true;
        h.host->dispatchChatMouseDown(1000);
        h.host->dispatchMessageClicked(c);
        QVERIFY(h.messageActions.deleted.isEmpty()); // deleting needs edit_messages
        doubleClick(h, click(true, 2, 0), 5000);
        QCOMPARE(h.ui.edits.size(), 1); // editing only needs the UI
    }
};

QTEST_GUILESS_MAIN(TestMessageClickActions)
#include "tst_messageclickactions.moc"
