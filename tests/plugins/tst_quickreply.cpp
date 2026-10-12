#include "PluginTestUtils.hpp"

#include "QuickReply.hpp"

#include <QTest>

namespace Qord::Plugins {
void qord_register_quickreply(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

namespace {
constexpr Id Self = 1;

ChatMessageInfo msg(Id id, Id author, int type = 0, quint64 flags = 0, bool deleted = false)
{
    return { id, author, type, flags, deleted };
}

// 10 (theirs), 11 (yours), 12 (blocked user), 13 (yours, deleted), 14 (system join, replyable),
// 15 (pinned notice, not replyable), 16 (yours), 17 (ephemeral)
QList<ChatMessageInfo> channel()
{
    return { msg(10, 7), msg(11, Self), msg(12, 20), msg(13, Self, 0, 0, true), msg(14, 8, 7),
             msg(15, 7, 6), msg(16, Self, 19), msg(17, 9, 20, 1 << 6) };
}

ShortcutContext key(ShortcutContext::Key k, bool shift, Id replying = 0, Id editing = 0)
{
    ShortcutContext s;
    s.accountId = Self;
    s.guildId = 100;
    s.channelId = 5;
    s.key = k;
    s.shift = shift;
    s.replyingTo = replying;
    s.editing = editing;
    s.messages = channel();
    return s;
}
} // namespace

class TestQuickReply : public QObject
{
    Q_OBJECT

    using K = ShortcutContext::Key;

private slots:
    void repliesUpAndDown()
    {
        SinglePluginHost h(qord_register_quickreply);
        QVERIFY(h.info()->loadError.isEmpty());
        QVERIFY(h.enableWithAllPermissions());
        h.relationships.blocked = { 20 };

        // From nothing: the newest replyable message (16; 17 is ephemeral)
        QVERIFY(h.host->dispatchShortcut(key(K::Up, false)));
        QCOMPARE(h.ui.replies.size(), 1);
        QCOMPARE(h.ui.replies[0].messageId, Id(16));
        QVERIFY(!h.ui.replies[0].mention.has_value()); // NoReplyMention or Discord decides
        QCOMPARE(h.ui.focuses, 1);
        QCOMPARE(h.ui.revealed, QList<Id> { 16 });
        // Up from 16 skips 15 (not replyable): 14
        QVERIFY(h.host->dispatchShortcut(key(K::Up, false, 16)));
        QCOMPARE(h.ui.replies.last().messageId, Id(14));
        // Up from 14 skips 13 (deleted) and 12 (blocked): 11
        QVERIFY(h.host->dispatchShortcut(key(K::Up, false, 14)));
        QCOMPARE(h.ui.replies.last().messageId, Id(11));
        // Down from 16: past the newest ends the reply
        QVERIFY(h.host->dispatchShortcut(key(K::Down, false, 16)));
        QCOMPARE(h.ui.replyCancels, 1);
        // Up from the oldest: also nothing more
        QVERIFY(h.host->dispatchShortcut(key(K::Up, false, 10)));
        QCOMPARE(h.ui.replyCancels, 2);
    }

    void editsYourOwn()
    {
        SinglePluginHost h(qord_register_quickreply);
        QVERIFY(h.enableWithAllPermissions());
        QVERIFY(h.host->dispatchShortcut(key(K::Up, true)));
        QCOMPARE(h.ui.edits, QList<Id> { 16 });
        QVERIFY(h.host->dispatchShortcut(key(K::Up, true, 0, 16)));
        QCOMPARE(h.ui.edits.last(), Id(11)); // 13 is deleted
        QVERIFY(h.host->dispatchShortcut(key(K::Down, true, 0, 16)));
        QCOMPARE(h.ui.editCancels, 1);
        QVERIFY(h.ui.replies.isEmpty());
    }

    void settings()
    {
        SinglePluginHost h(qord_register_quickreply);
        QVERIFY(h.enableWithAllPermissions());
        h.relationships.blocked = { 20 };
        h.host->settings(h.id)->setValue(QStringLiteral("ignoreBlockedAndIgnored"), false);
        h.host->settings(h.id)->setValue(QStringLiteral("shouldMention"), QStringLiteral("disabled"));
        // Up from 14: the blocked user's 12 counts now
        QVERIFY(h.host->dispatchShortcut(key(K::Up, false, 14)));
        QCOMPARE(h.ui.replies.last().messageId, Id(12));
        QCOMPARE(h.ui.replies.last().mention, std::optional<bool>(false));
        // A reply to a message that isn't in the list starts from the newest again
        QVERIFY(h.host->dispatchShortcut(key(K::Up, false, 13)));
        QCOMPARE(h.ui.replies.last().messageId, Id(16));
        h.host->settings(h.id)->setValue(QStringLiteral("shouldMention"), QStringLiteral("enabled"));
        QVERIFY(h.host->dispatchShortcut(key(K::Up, false)));
        QCOMPARE(h.ui.replies.last().mention, std::optional<bool>(true));
    }

    void cantSendDoesNothing()
    {
        SinglePluginHost h(qord_register_quickreply);
        QVERIFY(h.enableWithAllPermissions());
        ShortcutContext s = key(K::Up, false);
        s.canSend = false;
        QVERIFY(h.host->dispatchShortcut(s));
        QVERIFY(h.ui.replies.isEmpty());
        s.guildId = 0; // DMs aren't checked
        QVERIFY(h.host->dispatchShortcut(s));
        QCOMPARE(h.ui.replies.size(), 1);
    }
};

QTEST_GUILESS_MAIN(TestQuickReply)
#include "tst_quickreply.moc"
