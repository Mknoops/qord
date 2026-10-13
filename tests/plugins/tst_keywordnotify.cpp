#include "KeywordNotify.hpp"
#include "PluginTestUtils.hpp"

#include <QJsonArray>
#include <QSignalSpy>
#include <QTest>

namespace Qord::Plugins {
void qord_register_keywordnotify(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

class TestKeywordNotify : public QObject
{
    Q_OBJECT

    static MessageContext message(Id id, const QString &content, Id author = 20, Id channel = 30, Id guild = 40)
    {
        MessageContext m;
        m.accountId = 1;
        m.messageId = id;
        m.authorId = author;
        m.channelId = channel;
        m.guildId = guild;
        m.content = content;
        m.rawJson = QStringLiteral("{\"id\":\"%1\",\"content\":\"%2\"}").arg(id).arg(content);
        return m;
    }

    static KeywordNotifyPlugin::Entry entry(const QString &regex)
    {
        KeywordNotifyPlugin::Entry e;
        e.regex = regex;
        return e;
    }

private slots:
    void matchingRules()
    {
        using P = KeywordNotifyPlugin;
        QVERIFY(P::matches({ entry(QStringLiteral("qord|acheron")) }, message(1, QStringLiteral("I love Qord? no, qord"))));
        QVERIFY(!P::matches({ entry(QStringLiteral("QORD")) }, message(1, QStringLiteral("qord"))));
        auto ci = entry(QStringLiteral("QORD"));
        ci.ignoreCase = true;
        QVERIFY(P::matches({ ci }, message(1, QStringLiteral("qord"))));
        // Empty and invalid patterns match nothing
        QVERIFY(!P::matches({ entry({}), entry(QStringLiteral("(")) }, message(1, QStringLiteral("("))));

        // Bots are skipped unless whitelisted themselves
        auto bot = message(1, QStringLiteral("qord"));
        bot.authorIsBot = true;
        QVERIFY(!P::matches({ entry(QStringLiteral("qord")) }, bot));
        auto allowBot = entry(QStringLiteral("qord"));
        allowBot.whitelist = { QStringLiteral("20") };
        QVERIFY(P::matches({ allowBot }, bot));

        // Whitelist: only listed users, channels or servers
        auto white = entry(QStringLiteral("qord"));
        white.whitelist = { QStringLiteral("40") };
        QVERIFY(P::matches({ white }, message(1, QStringLiteral("qord"))));
        QVERIFY(!P::matches({ white }, message(1, QStringLiteral("qord"), 20, 30, 41)));
        // Blacklist wins unless the whitelist has priority
        auto both = entry(QStringLiteral("qord"));
        both.whitelist = { QStringLiteral("40") };
        both.blacklist = { QStringLiteral("20") };
        QVERIFY(!P::matches({ both }, message(1, QStringLiteral("qord"))));
        both.whitelistFirst = true;
        QVERIFY(P::matches({ both }, message(1, QStringLiteral("qord"))));

        // Embeds: title, description, fields
        auto embed = message(1, {});
        embed.rawJson = QStringLiteral("{\"embeds\":[{\"fields\":[{\"name\":\"x\",\"value\":\"qord inside\"}]}]}");
        QVERIFY(P::matches({ entry(QStringLiteral("qord")) }, embed));
    }

    void highlightsLogsAndInbox()
    {
        SinglePluginHost h(&qord_register_keywordnotify);
        QVERIFY(h.enableWithAllPermissions());
        KeywordNotifyPlugin::storeEntries(*h.host->settings(h.id), { entry(QStringLiteral("qord")) });
        QSignalSpy inboxChanged(h.host.get(), &PluginHost::inboxChanged);

        QVERIFY(!h.host->dispatchHighlightMessage(message(100, QStringLiteral("hello"))));
        QVERIFY(h.host->dispatchHighlightMessage(message(100, QStringLiteral("about qord"))));
        QVERIFY(h.host->dispatchHighlightMessage(message(300, QStringLiteral("qord again"))));
        QVERIFY(h.host->dispatchHighlightMessage(message(100, QStringLiteral("about qord")))); // once in the log
        auto own = message(400, QStringLiteral("my qord"));
        own.isOwn = true;
        QVERIFY(!h.host->dispatchHighlightMessage(own));
        QCOMPARE(inboxChanged.size(), 2);

        auto tabs = h.host->dispatchInboxTabs(1);
        QCOMPARE(tabs.size(), 1);
        QCOMPARE(tabs[0].tab.label, QStringLiteral("Keywords"));
        QCOMPARE(tabs[0].tab.messages.size(), 2);
        QCOMPARE(tabs[0].tab.messages[0].messageId, Id(300)); // newest first
        QCOMPARE(tabs[0].tab.messages[1].rawJson, message(100, QStringLiteral("about qord")).rawJson);
        QVERIFY(h.host->dispatchInboxTabs(2)[0].tab.messages.isEmpty()); // per account

        h.host->dispatchInboxMessageRemoved(tabs[0].pluginId, tabs[0].tab.id, 1, 300);
        QCOMPARE(h.host->dispatchInboxTabs(1)[0].tab.messages.size(), 1);
        h.host->dispatchInboxCleared(tabs[0].pluginId, tabs[0].tab.id, 1);
        QVERIFY(h.host->dispatchInboxTabs(1)[0].tab.messages.isEmpty());
    }

    void keepsAtMostAmountToKeep()
    {
        SinglePluginHost h(&qord_register_keywordnotify);
        QVERIFY(h.enableWithAllPermissions());
        auto *s = h.host->settings(h.id);
        KeywordNotifyPlugin::storeEntries(*s, { entry(QStringLiteral("qord")) });
        s->setValue(QStringLiteral("amountToKeep"), 2);
        for (Id id = 1; id <= 4; ++id)
            QVERIFY(h.host->dispatchHighlightMessage(message(id, QStringLiteral("qord"))));
        const auto messages = h.host->dispatchInboxTabs(1)[0].tab.messages;
        QCOMPARE(messages.size(), 2);
        QCOMPARE(messages[0].messageId, Id(4));
        QCOMPARE(messages[1].messageId, Id(3));
    }
};

QTEST_GUILESS_MAIN(TestKeywordNotify)
#include "tst_keywordnotify.moc"
