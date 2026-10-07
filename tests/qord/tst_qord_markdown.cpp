// Tests for the Discord markdown rules Qord adds to Acheron's parser
// (src/Core/Markdown/QordRules.cpp).

#include "Core/Markdown/Parser.hpp"

#include <QTest>

using namespace Acheron::Core::Markdown;

static QString render(const QString &source)
{
    Parser parser;
    ParseState state;
    state.isInline = true;
    return parser.toHtml(parser.parse(source, state));
}

class TestQordMarkdown : public QObject
{
    Q_OBJECT

private slots:
    void spoilers()
    {
        const QString hidden = QString::fromLatin1(SpoilerHiddenStyle);
        QCOMPARE(render("||secret||"),
                 QStringLiteral("<a href=\"qord-spoiler:\" style=\"%1\">secret</a>").arg(hidden));
        // Discord allows spaces inside the bars
        QCOMPARE(render("a || spoiler || b"),
                 QStringLiteral("a <a href=\"qord-spoiler:\" style=\"%1\"> spoiler </a> b").arg(hidden));
        // Formatting nests inside
        QVERIFY(render("||**bold**||").contains("<strong>bold</strong>"));
        // An unclosed spoiler stays text
        QCOMPARE(render("||open"), QStringLiteral("||open"));
    }

    void codeBlocks()
    {
        const QString html = render("before\n```cpp\nint main() {\n    return 0;\n}\n```\nafter");
        QVERIFY(html.startsWith("before<table")); // no blank line before the block
        QVERIFY(html.contains("class=\"codeblock\""));
        QVERIFY(html.contains("<pre><code>int main() {\n    return 0;\n}</code></pre>"));
        // The newline after the closing fence is consumed, so no blank line follows
        QVERIFY(html.endsWith("</table>after"));
        // No language line: the first line is code
        QVERIFY(render("```x = 1```").contains("<code>x = 1</code>"));
        // Markdown inside is not parsed, HTML is escaped
        QVERIFY(render("```**a** <b>```").contains("<code>**a** &lt;b&gt;</code>"));
    }

    void blockQuotes()
    {
        const QString one = render("> quoted **text**\nnot quoted");
        QVERIFY(one.contains("class=\"blockquote\""));
        QVERIFY(one.contains("quoted <strong>text</strong>"));
        QVERIFY(one.endsWith("</table>not quoted"));
        // Consecutive > lines join one quote
        const QString multi = render("> a\n> b");
        QCOMPARE(multi.count("class=\"blockquote\""), 1);
        QVERIFY(multi.contains("a<br>b"));
        // >>> quotes the rest of the message
        QVERIFY(render(">>> x\ny").contains("x<br>y"));
        // Only at the start of a line, and a space is required
        QVERIFY(!render("a > b").contains("blockquote"));
        QVERIFY(!render(">nospace").contains("blockquote"));
    }

    void headingsSubtextLists()
    {
        QCOMPARE(render("# Big\ntext"),
                 QStringLiteral("<span style=\"font-size: x-large; font-weight: 700;\">Big</span><br>text"));
        QVERIFY(render("### Small").contains("font-size: medium"));
        QVERIFY(!render("#### four").contains("font-weight: 700")); // only 1-3 #
        QVERIFY(!render("a # b").contains("font-weight: 700"));
        QCOMPARE(render("-# fine print"),
                 QStringLiteral("<span class=\"subtext\" style=\"font-size: small;\">fine print</span>"));
        QCOMPARE(render("- one\n- two"),
                 QStringLiteral("&nbsp;&nbsp;•&nbsp;one<br>&nbsp;&nbsp;•&nbsp;two"));
        QVERIFY(render("1. first").contains("1.&nbsp;first"));
    }

    void timestamps()
    {
        const QLocale us(QLocale::English, QLocale::UnitedStates);
        const QDateTime now = QDateTime::fromSecsSinceEpoch(1700000000);
        QCOMPARE(formatDiscordTimestamp(1700000000 - 300, QChar('R'), now, us), QStringLiteral("5 minutes ago"));
        QCOMPARE(formatDiscordTimestamp(1700000000 + 7200, QChar('R'), now, us), QStringLiteral("in 2 hours"));
        QCOMPARE(formatDiscordTimestamp(1700000000 - 86400 * 3, QChar('R'), now, us), QStringLiteral("3 days ago"));
        const QDateTime local = QDateTime::fromSecsSinceEpoch(1700000000).toLocalTime();
        QCOMPARE(formatDiscordTimestamp(1700000000, QChar('D'), now, us), us.toString(local.date(), "MMMM d, yyyy"));
        QVERIFY(formatDiscordTimestamp(1700000000, QChar('T'), now, us).count(':') == 2);

        const QString html = render("at <t:1700000000:R>!");
        QVERIFY(html.startsWith("at <span class=\"timestamp\""));
        QVERIFY(html.endsWith("</span>!"));
        // Out of range stays visible as text
        QVERIFY(render("<t:99999999999999999>").contains("&lt;t:"));
    }

    void mentions()
    {
        Parser parser;
        parser.setRoleResolver([](Acheron::Core::Snowflake id, Acheron::Core::Snowflake channel) {
            RoleMention r;
            if (quint64(id) == 5 && quint64(channel) == 9) {
                r.name = "Mods";
                r.color = QColor(0x3498db);
            }
            return r;
        });
        ParseState state;
        state.isInline = true;
        state.customState["channelId"] = quint64(9);
        QCOMPARE(parser.toHtml(parser.parse("<@&5>", state)),
                 QStringLiteral("<span style=\"color: #3498db; background-color: rgba(52, 152, 219, 0.1);\">@Mods</span>"));
        QCOMPARE(parser.toHtml(parser.parse("<@&6>", state)),
                 QStringLiteral("<span class=\"mention\">@deleted-role</span>"));
        QCOMPARE(render("hi @everyone"), QStringLiteral("hi <span class=\"mention\">@everyone</span>"));
        QCOMPARE(render("</ban user:123>"), QStringLiteral("<span class=\"mention\">/ban user</span>"));
        // Existing rules still win where they apply
        QVERIFY(render("<@123>").contains("@123"));
    }

    void plainTextUnchanged()
    {
        QCOMPARE(render("just text, nothing special"), QStringLiteral("just text, nothing special"));
        QCOMPARE(render("a | b"), QStringLiteral("a | b"));
        QCOMPARE(render("C# and 1.5"), QStringLiteral("C# and 1.5"));
    }
};

QTEST_GUILESS_MAIN(TestQordMarkdown)
#include "tst_qord_markdown.moc"
