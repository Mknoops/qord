#include "PluginTestUtils.hpp"

#include <QTest>

namespace Qord::Plugins {
void qord_register_blockkeywords(Registry &registry);
QStringList splitKeywordPatterns(const QString &input);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

class TestBlockKeywords : public QObject
{
    Q_OBJECT

    static MessageContext message(const QString &content, const QString &rawJson = {})
    {
        MessageContext m;
        m.accountId = 1;
        m.channelId = 2;
        m.messageId = 3;
        m.content = content;
        m.rawJson = rawJson;
        m.hasEmbeds = !rawJson.isEmpty();
        return m;
    }

    static void setWords(SinglePluginHost &h, const QString &words)
    {
        h.host->settings(h.id)->setValue(QStringLiteral("blockedWords"), words);
    }

private slots:
    void splitsLikeUpstream()
    {
        QCOMPARE(splitKeywordPatterns(QStringLiteral(" spoiler , , leak ")),
                 (QStringList{ QStringLiteral("spoiler"), QStringLiteral("leak") }));
        // Commas inside [...] and {n,m} stay in the pattern
        QCOMPARE(splitKeywordPatterns(QStringLiteral("[a,b]x,y{1,3},z")),
                 (QStringList{ QStringLiteral("[a,b]x"), QStringLiteral("y{1,3}"), QStringLiteral("z") }));
        QVERIFY(splitKeywordPatterns(QString()).isEmpty());
    }

    void hidesWholeWordsIgnoringCase()
    {
        SinglePluginHost h(&qord_register_blockkeywords);
        QVERIFY(h.enableWithAllPermissions());
        QVERIFY(!h.host->dispatchShouldHideMessage(message(QStringLiteral("anything")))); // no words: nothing hidden
        setWords(h, QStringLiteral("spoiler, a.b"));
        QCOMPARE(h.messages.filterRefreshes, 1); // the chat filters again at once
        QVERIFY(h.host->dispatchShouldHideMessage(message(QStringLiteral("big SPOILER ahead"))));
        QVERIFY(!h.host->dispatchShouldHideMessage(message(QStringLiteral("spoilers are fine")))); // whole words only
        QVERIFY(h.host->dispatchShouldHideMessage(message(QStringLiteral("see a.b here"))));
        QVERIFY(!h.host->dispatchShouldHideMessage(message(QStringLiteral("see axb here")))); // escaped, not a regex
        QVERIFY(!h.host->dispatchShouldHideMessage(message(QStringLiteral("hello"))));

        h.host->settings(h.id)->setValue(QStringLiteral("caseSensitive"), true);
        QVERIFY(!h.host->dispatchShouldHideMessage(message(QStringLiteral("big SPOILER ahead"))));
        QVERIFY(h.host->dispatchShouldHideMessage(message(QStringLiteral("big spoiler ahead"))));
    }

    void checksEmbedTitlesAndDescriptions()
    {
        SinglePluginHost h(&qord_register_blockkeywords);
        QVERIFY(h.enableWithAllPermissions());
        setWords(h, QStringLiteral("leak"));
        QVERIFY(h.host->dispatchShouldHideMessage(
                message({}, QStringLiteral("{\"embeds\":[{\"title\":\"Game leak\"}]}"))));
        QVERIFY(h.host->dispatchShouldHideMessage(
                message({}, QStringLiteral("{\"embeds\":[{\"description\":\"the leak\"}]}"))));
        QVERIFY(!h.host->dispatchShouldHideMessage(
                message({}, QStringLiteral("{\"embeds\":[{\"footer\":{\"text\":\"leak\"}}]}"))));
    }

    void regexesAndInvalidOnes()
    {
        SinglePluginHost h(&qord_register_blockkeywords);
        QVERIFY(h.enableWithAllPermissions());
        h.host->settings(h.id)->setValue(QStringLiteral("useRegex"), true);
        setWords(h, QStringLiteral("(unclosed, ^!buy\\b, \\d{3,4}"));
        QVERIFY(h.host->dispatchShouldHideMessage(message(QStringLiteral("!buy now"))));
        QVERIFY(h.host->dispatchShouldHideMessage(message(QStringLiteral("call 5555"))));
        QVERIFY(!h.host->dispatchShouldHideMessage(message(QStringLiteral("please !buy"))));
        QVERIFY(!h.host->dispatchShouldHideMessage(message(QStringLiteral("(unclosed")))); // skipped, the rest work
    }

    void regexHelper()
    {
        SinglePluginHost h(&qord_register_blockkeywords);
        QVERIFY(h.enableWithAllPermissions());
        h.host->settings(h.id)->setValue(QStringLiteral("useRegex"), true);
        setWords(h, QStringLiteral("cat, (dog"));
        h.host->dispatchSettingAction(h.id, QStringLiteral("regexHelper"));
        QCOMPARE(h.ui.promptTitle, QStringLiteral("Regex Helper"));
        h.ui.pendingPrompt(QStringLiteral("a cat"));
        QCOMPARE(h.ui.dialogTitle, QStringLiteral("Regex Helper"));
        QCOMPARE(h.ui.dialogSections.size(), 2);
        const QString results = h.ui.dialogSections[1].text;
        QVERIFY(results.startsWith(QStringLiteral("✓ cat\n⚠ (dog — ")));
        // The last input is offered again
        h.host->dispatchSettingAction(h.id, QStringLiteral("regexHelper"));
        QCOMPARE(h.ui.promptInitial, QStringLiteral("a cat"));

        setWords(h, QString());
        h.host->dispatchSettingAction(h.id, QStringLiteral("regexHelper"));
        h.ui.pendingPrompt(QStringLiteral("x"));
        QCOMPARE(h.ui.dialogSections[1].text, QStringLiteral("No patterns configured"));
    }
};

QTEST_GUILESS_MAIN(TestBlockKeywords)
#include "tst_blockkeywords.moc"
