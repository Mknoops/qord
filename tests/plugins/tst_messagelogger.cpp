#include "PluginTestUtils.hpp"

#include "MessageLogger.hpp"

#include "Plugins/TextDiff.hpp"

#include <algorithm>

#include <QTest>

namespace Qord::Plugins {
void qord_register_messagelogger(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

static MessageContext msg(Id id, const QString &content, Id author = 7)
{
    MessageContext m;
    m.accountId = 1;
    m.channelId = 100;
    m.guildId = 1000;
    m.parentChannelId = 50;
    m.messageId = id;
    m.authorId = author;
    m.isOwn = author == 1;
    m.content = content;
    m.timestamp = QDateTime::fromMSecsSinceEpoch(1'700'000'000'000, Qt::UTC);
    return m;
}

static MessageContext edited(MessageContext m, const QString &content, qint64 offsetSecs = 60)
{
    m.content = content;
    m.editedTimestamp = m.timestamp.addSecs(offsetSecs);
    return m;
}

class TestMessageLogger : public QObject
{
    Q_OBJECT

private slots:
    void keepsDeletedMessages()
    {
        SinglePluginHost h(qord_register_messagelogger);
        QVERIFY(h.info()->loadError.isEmpty());
        QVERIFY(!h.host->dispatchMessageDeleted(msg(1, "x"))); // disabled
        QVERIFY(h.enableWithAllPermissions());
        QVERIFY(h.host->dispatchMessageDeleted(msg(1, "secret")));
        auto a = h.host->dispatchAnnotateMessage(msg(1, "secret"));
        QVERIFY(a);
        QVERIFY(a->deleted);
        QCOMPARE(a->deletedStyle, MessageAnnotation::DeletedStyle::Text);
        QVERIFY(a->history.isEmpty());
        QVERIFY(!h.host->dispatchAnnotateMessage(msg(2, "untouched")));

        QVERIFY(h.host->settings(h.id)->setValue("deleteStyle", "overlay").isEmpty());
        QCOMPARE(h.host->dispatchAnnotateMessage(msg(1, "secret"))->deletedStyle, MessageAnnotation::DeletedStyle::Overlay);
        QVERIFY(h.messages.refreshed.contains(1)); // settings changes re-render logged messages
    }

    void ignoreRules()
    {
        SinglePluginHost h(qord_register_messagelogger);
        QVERIFY(h.enableWithAllPermissions());
        MessageContext ephemeral = msg(1, "x");
        ephemeral.flags = 64;
        QVERIFY(!h.host->dispatchMessageDeleted(ephemeral));
        MessageContext bot = msg(2, "x");
        bot.authorIsBot = true;
        QVERIFY(!h.host->dispatchMessageDeleted(bot)); // ignoreBots defaults on
        QVERIFY(h.host->dispatchMessageDeleted(msg(3, "mine", 1))); // ignoreSelf defaults off

        auto *s = h.host->settings(h.id);
        QVERIFY(s->setValue("ignoreSelf", true).isEmpty());
        QVERIFY(!h.host->dispatchMessageDeleted(msg(4, "mine", 1)));
        QVERIFY(s->setValue("ignoreUsers", "7, 8").isEmpty());
        QVERIFY(!h.host->dispatchMessageDeleted(msg(5, "x", 7)));
        QVERIFY(h.host->dispatchMessageDeleted(msg(6, "x", 9)));
        QVERIFY(s->setValue("ignoreChannels", "50").isEmpty()); // matches the parent channel too
        QVERIFY(!h.host->dispatchMessageDeleted(msg(7, "x", 9)));
        QVERIFY(s->setValue("ignoreChannels", "").isEmpty());
        QVERIFY(s->setValue("ignoreGuilds", "1000").isEmpty());
        QVERIFY(!h.host->dispatchMessageDeleted(msg(8, "x", 9)));
        QVERIFY(s->setValue("ignoreGuilds", "").isEmpty());
        QVERIFY(s->setValue("logDeletes", false).isEmpty());
        QVERIFY(!h.host->dispatchMessageDeleted(msg(9, "x", 9)));

        // Support bots in the support channels are left alone
        MessageContext venbot = msg(10, "x", 1017176847865352332ull);
        venbot.parentChannelId = 1108135649699180705ull;
        QVERIFY(MessageLogger::shouldIgnore(MessageLogger::Config(), venbot, true));
    }

    void editHistoryInline()
    {
        SinglePluginHost h(qord_register_messagelogger);
        QVERIFY(h.enableWithAllPermissions());
        MessageContext v1 = msg(1, "first");
        MessageContext v2 = edited(v1, "second", 60);
        MessageContext v3 = edited(v2, "third", 120);
        h.host->dispatchMessageEdited(v1, v2);
        h.host->dispatchMessageEdited(v2, v3);
        // No content change (e.g. an embed update) or no edited timestamp: not an edit
        h.host->dispatchMessageEdited(v3, v3);
        MessageContext noStamp = v3;
        noStamp.editedTimestamp = {};
        noStamp.content = "fourth";
        h.host->dispatchMessageEdited(v3, noStamp);

        auto a = h.host->dispatchAnnotateMessage(v3);
        QVERIFY(a);
        QVERIFY(!a->deleted);
        QCOMPARE(a->history.size(), 2);
        QCOMPARE(a->history[0].content, QStringLiteral("first"));
        QCOMPARE(a->history[0].timestamp, v2.editedTimestamp);
        QCOMPARE(a->history[1].content, QStringLiteral("second"));
        QVERIFY(a->history[0].diff.isEmpty()); // diffs off by default

        QVERIFY(h.host->settings(h.id)->setValue("inlineEdits", false).isEmpty());
        QVERIFY(h.host->dispatchAnnotateMessage(v3)->history.isEmpty());
    }

    void editDiffs()
    {
        SinglePluginHost h(qord_register_messagelogger);
        QVERIFY(h.host->settings(h.id)->setValue("showEditDiffs", true).isEmpty());
        QVERIFY(h.enableWithAllPermissions());
        MessageContext v1 = msg(1, "hello world");
        MessageContext v2 = edited(v1, "hello there");
        h.host->dispatchMessageEdited(v1, v2);
        auto a = h.host->dispatchAnnotateMessage(v2);
        QCOMPARE(a->history.size(), 1);
        QCOMPARE(a->history[0].diff, createWordDiff("hello world", "hello there"));
        QVERIFY(!a->separatedDiffs);

        QVERIFY(h.host->settings(h.id)->setValue("separatedDiffs", true).isEmpty());
        a = h.host->dispatchAnnotateMessage(v2);
        QVERIFY(a->separatedDiffs);
        for (const auto &p : a->history[0].diff)
            QVERIFY(p.kind != DiffPart::Kind::Added);
        for (const auto &p : a->currentDiff)
            QVERIFY(p.kind != DiffPart::Kind::Removed);

        // Per-message toggle from the context menu
        auto items = h.host->dispatchMessageContextMenuItems(v2);
        auto it = std::find_if(items.begin(), items.end(), [](const auto &e) { return e.item.id == "ml-toggle-diff"; });
        QVERIFY(it != items.end());
        QCOMPARE(it->item.label, QStringLiteral("Disable Diff View"));
        h.host->dispatchMessageContextMenuAction(h.id, "ml-toggle-diff", v2);
        a = h.host->dispatchAnnotateMessage(v2);
        QVERIFY(a->history[0].diff.isEmpty());
        QVERIFY(!a->separatedDiffs);
    }

    void contextMenuActions()
    {
        SinglePluginHost h(qord_register_messagelogger);
        QVERIFY(h.enableWithAllPermissions());
        QVERIFY(h.host->dispatchMessageContextMenuItems(msg(1, "x")).isEmpty()); // nothing logged

        MessageContext deleted = msg(1, "gone");
        QVERIFY(h.host->dispatchMessageDeleted(deleted));
        QStringList ids;
        for (const auto &e : h.host->dispatchMessageContextMenuItems(deleted))
            ids.append(e.item.id);
        QCOMPARE(ids, (QStringList { "ml-toggle-style", "ml-remove-history" }));

        h.host->dispatchMessageContextMenuAction(h.id, "ml-toggle-style", deleted);
        QVERIFY(!h.host->dispatchAnnotateMessage(deleted)->deleted);
        QVERIFY(h.messages.refreshed.contains(1));

        h.host->dispatchMessageContextMenuAction(h.id, "ml-remove-history", deleted);
        QCOMPARE(h.messages.removed, QList<Id> { 1 });
        QVERIFY(!h.host->dispatchAnnotateMessage(deleted));

        MessageContext v1 = msg(2, "a");
        MessageContext v2 = edited(v1, "b");
        h.host->dispatchMessageEdited(v1, v2);
        h.host->dispatchMessageContextMenuAction(h.id, "ml-view-history", v2);
        QCOMPARE(h.ui.dialogs, 1);
        QCOMPARE(h.ui.dialogSections.size(), 2);
        QCOMPARE(h.ui.dialogSections[0].text, QStringLiteral("a"));
        QCOMPARE(h.ui.dialogSections[1].text, QStringLiteral("b"));

        h.host->dispatchMessageContextMenuAction(h.id, "ml-remove-history", v2);
        QVERIFY(!h.host->dispatchAnnotateMessage(v2));
        QCOMPARE(h.messages.removed.size(), 1); // edited (not deleted) messages are re-rendered, not removed
    }

    void disablingDropsKeptMessages()
    {
        SinglePluginHost h(qord_register_messagelogger);
        QVERIFY(h.enableWithAllPermissions());
        QVERIFY(h.host->dispatchMessageDeleted(msg(1, "gone")));
        MessageContext v1 = msg(2, "a");
        h.host->dispatchMessageEdited(v1, edited(v1, "b"));
        QVERIFY(h.host->setEnabled(h.id, false));
        QCOMPARE(h.messages.removed, QList<Id> { 1 });
        QVERIFY(h.messages.refreshed.contains(2));
    }

    void memoryIsBounded()
    {
        SinglePluginHost h(qord_register_messagelogger);
        QVERIFY(h.enableWithAllPermissions());
        for (Id id = 1; id <= Id(MessageLoggerPlugin::MaxRecords) + 10; ++id)
            QVERIFY(h.host->dispatchMessageDeleted(msg(id, "x")));
        QVERIFY(!h.host->dispatchAnnotateMessage(msg(1, "x"))); // oldest evicted
        QVERIFY(h.host->dispatchAnnotateMessage(msg(Id(MessageLoggerPlugin::MaxRecords) + 10, "x")));
    }
};

QTEST_GUILESS_MAIN(TestMessageLogger)
#include "tst_messagelogger.moc"
