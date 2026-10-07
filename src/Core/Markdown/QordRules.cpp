// Qord additions to Acheron's Discord markdown parser: the rules Discord
// renders that Acheron did not have yet. Patterns follow Discord's client
// rules (simple-markdown based); output is HTML that QTextDocument renders.

#include "Parser.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QLocale>
#include <QRegularExpression>

namespace Acheron {
namespace Core {
namespace Markdown {

namespace {

using RenderChildren = std::function<QString(const QList<AstNode> &)>;

MatchFn anyScope(const QRegularExpression &regex)
{
    return [regex](const QString &source, const ParseState &) -> Capture {
        return regex.match(source, 0, QRegularExpression::NormalMatch, QRegularExpression::AnchorAtOffsetMatchOption);
    };
}

// Block-ish rules only apply at the start of a line, like Discord's
bool atLineStart(const ParseState &state)
{
    return state.prevCapture.isEmpty() || state.prevCapture.endsWith(QLatin1Char('\n'));
}

MatchFn lineStart(const QRegularExpression &regex)
{
    return [regex](const QString &source, const ParseState &state) -> Capture {
        if (!atLineStart(state))
            return Capture();
        return regex.match(source, 0, QRegularExpression::NormalMatch, QRegularExpression::AnchorAtOffsetMatchOption);
    };
}

ParseState inlineChild(ParseState state)
{
    state.isInline = true;
    state.prevCapture.clear();
    return state;
}

QString unitText(qint64 n, const char *one, const char *many)
{
    // Spelled out so it reads right without a translation catalogue
    return n == 1 ? QCoreApplication::translate("Markdown", one)
                  : QCoreApplication::translate("Markdown", many).arg(n);
}

QString relativeTime(const QDateTime &when, const QDateTime &now)
{
    const qint64 secs = now.secsTo(when);
    const qint64 a = qAbs(secs);
    QString amount;
    if (a < 60)
        amount = unitText(a, "a second", "%1 seconds");
    else if (a < 3600)
        amount = unitText(a / 60, "a minute", "%1 minutes");
    else if (a < 86400)
        amount = unitText(a / 3600, "an hour", "%1 hours");
    else if (a < 86400 * 30)
        amount = unitText(a / 86400, "a day", "%1 days");
    else if (a < 86400 * 365)
        amount = unitText(a / (86400 * 30), "a month", "%1 months");
    else
        amount = unitText(a / (86400 * 365), "a year", "%1 years");
    return secs >= 0 ? QCoreApplication::translate("Markdown", "in %1").arg(amount)
                     : QCoreApplication::translate("Markdown", "%1 ago").arg(amount);
}

} // namespace

QString formatDiscordTimestamp(qint64 unixSeconds, QChar style, const QDateTime &now, const QLocale &locale)
{
    const QDateTime when = QDateTime::fromSecsSinceEpoch(unixSeconds).toLocalTime();
    switch (style.unicode()) {
    case 't':
        return locale.toString(when.time(), QLocale::ShortFormat);
    case 'T': {
        QString format = locale.timeFormat(QLocale::ShortFormat);
        if (!format.contains(QLatin1String("ss")))
            format.replace(QLatin1String("mm"), QLatin1String("mm:ss"));
        return locale.toString(when.time(), format);
    }
    case 'd':
        return locale.toString(when.date(), QLocale::ShortFormat);
    case 'D':
        return locale.toString(when.date(), QStringLiteral("MMMM d, yyyy"));
    case 'F':
        return locale.toString(when.date(), QStringLiteral("dddd, MMMM d, yyyy")) + QLatin1Char(' ')
               + locale.toString(when.time(), QLocale::ShortFormat);
    case 'R':
        return relativeTime(when, now);
    case 'f':
    default:
        return locale.toString(when.date(), QStringLiteral("MMMM d, yyyy")) + QLatin1Char(' ')
               + locale.toString(when.time(), QLocale::ShortFormat);
    }
}

void Parser::setRoleResolver(RoleResolverFn resolver)
{
    roleResolver = std::move(resolver);
}

void Parser::setupQordRules()
{
    // ```lang\ncode``` — anywhere, like Discord
    MarkdownRule codeBlock;
    codeBlock.name = "codeBlock";
    codeBlock.order = 4;
    codeBlock.regex = QRegularExpression(R"(^```(?:([a-z0-9_+\-.#]+?)\n)?\n*([^\n][\s\S]*?)\n*```\n?)",
                                         QRegularExpression::CaseInsensitiveOption);
    codeBlock.match = anyScope(codeBlock.regex);
    codeBlock.parse = [](const Capture &match, NestedParseFn, ParseState) -> AstNode {
        AstNode node;
        node.type = "codeBlock";
        node.content = match.captured(2);
        node.attributes["lang"] = match.captured(1);
        return node;
    };
    codeBlock.html = [](const AstNode &node, RenderChildren) -> QString {
        return QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"8\" class=\"codeblock\">"
                              "<tr><td><pre><code>%1</code></pre></td></tr></table>")
                .arg(node.content.toHtmlEscaped());
    };
    rules.append(codeBlock);

    // > quote / >>> rest-of-message quote
    MarkdownRule blockQuote;
    blockQuote.name = "blockQuote";
    blockQuote.order = 5;
    blockQuote.regex = QRegularExpression(R"(^( *>>> +([\s\S]*))|^( *>(?!>>) +[^\n]*(\n *>(?!>>) +[^\n]*)*\n?))");
    blockQuote.match = lineStart(blockQuote.regex);
    blockQuote.parse = [](const Capture &match, NestedParseFn nestedParse, ParseState state) -> AstNode {
        AstNode node;
        node.type = "blockQuote";
        QString inner;
        if (!match.captured(1).isEmpty()) {
            inner = match.captured(2);
        } else {
            static const QRegularExpression prefix(R"(^ *> ?)", QRegularExpression::MultilineOption);
            inner = match.captured(3);
            inner.replace(prefix, QString());
            if (inner.endsWith(QLatin1Char('\n')))
                inner.chop(1);
        }
        ParseState child = inlineChild(state);
        child.excludedRules.insert(QStringLiteral("blockQuote"));
        node.children = nestedParse(inner, child);
        return node;
    };
    blockQuote.html = [](const AstNode &node, RenderChildren renderChildren) -> QString {
        return QStringLiteral("<table cellspacing=\"0\" cellpadding=\"0\" class=\"blockquote\"><tr>"
                              "<td width=\"4\" class=\"blockquote-bar\"></td>"
                              "<td style=\"padding-left: 12px;\">%1</td></tr></table>")
                .arg(renderChildren(node.children));
    };
    rules.append(blockQuote);

    // # Heading, ## Heading, ### Heading
    MarkdownRule heading;
    heading.name = "heading";
    heading.order = 5;
    heading.regex = QRegularExpression(R"(^ *(#{1,3}) +([^\n]+?) *(\n|$))");
    heading.match = lineStart(heading.regex);
    heading.parse = [](const Capture &match, NestedParseFn nestedParse, ParseState state) -> AstNode {
        AstNode node;
        node.type = "heading";
        node.attributes["level"] = match.captured(1).size();
        node.attributes["break"] = !match.captured(3).isEmpty();
        node.children = nestedParse(match.captured(2), inlineChild(state));
        return node;
    };
    heading.html = [](const AstNode &node, RenderChildren renderChildren) -> QString {
        static const char *const sizes[] = { "", "x-large", "large", "medium" }; // QTextDocument ignores %
        const int level = qBound(1, node.attributes["level"].toInt(), 3);
        return QStringLiteral("<span style=\"font-size: %1; font-weight: 700;\">%2</span>%3")
                .arg(QLatin1String(sizes[level]), renderChildren(node.children),
                     node.attributes["break"].toBool() ? QStringLiteral("<br>") : QString());
    };
    rules.append(heading);

    // -# subtext
    MarkdownRule subtext;
    subtext.name = "subtext";
    subtext.order = 5;
    subtext.regex = QRegularExpression(R"(^-# +([^\n]+?) *(\n|$))");
    subtext.match = lineStart(subtext.regex);
    subtext.parse = [](const Capture &match, NestedParseFn nestedParse, ParseState state) -> AstNode {
        AstNode node;
        node.type = "subtext";
        node.attributes["break"] = !match.captured(2).isEmpty();
        node.children = nestedParse(match.captured(1), inlineChild(state));
        return node;
    };
    subtext.html = [](const AstNode &node, RenderChildren renderChildren) -> QString {
        return QStringLiteral("<span class=\"subtext\" style=\"font-size: small;\">%1</span>%2")
                .arg(renderChildren(node.children), node.attributes["break"].toBool() ? QStringLiteral("<br>") : QString());
    };
    rules.append(subtext);

    // - item / * item / 1. item (one line per match)
    MarkdownRule listItem;
    listItem.name = "listItem";
    listItem.order = 5;
    listItem.regex = QRegularExpression(R"(^( *)([*-]|\d{1,9}\.) +([^\n]+?) *(\n|$))");
    listItem.match = lineStart(listItem.regex);
    listItem.parse = [](const Capture &match, NestedParseFn nestedParse, ParseState state) -> AstNode {
        AstNode node;
        node.type = "listItem";
        node.attributes["depth"] = match.captured(1).size() / 2;
        node.attributes["marker"] = match.captured(2);
        node.attributes["break"] = !match.captured(4).isEmpty();
        node.children = nestedParse(match.captured(3), inlineChild(state));
        return node;
    };
    listItem.html = [](const AstNode &node, RenderChildren renderChildren) -> QString {
        const QString marker = node.attributes["marker"].toString();
        const QString bullet = marker.endsWith(QLatin1Char('.')) ? marker : QStringLiteral("•");
        QString indent;
        for (int i = 0; i <= node.attributes["depth"].toInt(); ++i)
            indent += QStringLiteral("&nbsp;&nbsp;");
        return indent + bullet + QStringLiteral("&nbsp;") + renderChildren(node.children)
               + (node.attributes["break"].toBool() ? QStringLiteral("<br>") : QString());
    };
    rules.append(listItem);

    // ||spoiler||
    MarkdownRule spoiler;
    spoiler.name = "spoiler";
    spoiler.order = 19;
    spoiler.regex = QRegularExpression(R"(^\|\|([\s\S]+?)\|\|)");
    spoiler.match = anyScope(spoiler.regex);
    spoiler.parse = [](const Capture &match, NestedParseFn nestedParse, ParseState state) -> AstNode {
        AstNode node;
        node.type = "spoiler";
        node.children = nestedParse(match.captured(1), inlineChild(state));
        return node;
    };
    spoiler.html = [](const AstNode &node, RenderChildren renderChildren) -> QString {
        return QStringLiteral("<a href=\"%1\" style=\"%2\">%3</a>")
                .arg(QLatin1String(SpoilerHref), QLatin1String(SpoilerHiddenStyle), renderChildren(node.children));
    };
    rules.append(spoiler);

    // <t:1700000000:R>
    MarkdownRule timestamp;
    timestamp.name = "timestamp";
    timestamp.order = 21;
    timestamp.regex = QRegularExpression(R"(^<t:(-?\d{1,17})(?::([DFRTdft]))?>)");
    timestamp.match = anyScope(timestamp.regex);
    timestamp.parse = [](const Capture &match, NestedParseFn, ParseState) -> AstNode {
        AstNode node;
        node.type = "timestamp";
        node.content = match.captured(1);
        node.attributes["style"] = match.captured(2);
        return node;
    };
    timestamp.html = [](const AstNode &node, RenderChildren) -> QString {
        bool ok = false;
        const qint64 secs = node.content.toLongLong(&ok);
        if (!ok || qAbs(secs) > 8640000000000LL)
            return QStringLiteral("&lt;t:%1&gt;").arg(node.content.toHtmlEscaped());
        const QString style = node.attributes["style"].toString();
        const QDateTime when = QDateTime::fromSecsSinceEpoch(secs);
        const QString text = formatDiscordTimestamp(secs, style.isEmpty() ? QChar('f') : style.at(0),
                                                    QDateTime::currentDateTime(), QLocale());
        return QStringLiteral("<span class=\"timestamp\" title=\"%1\">%2</span>")
                .arg(QLocale().toString(when.toLocalTime(), QLocale::LongFormat).toHtmlEscaped(), text.toHtmlEscaped());
    };
    rules.append(timestamp);

    // <@&roleId>
    MarkdownRule role;
    role.name = "roleMention";
    role.order = 20; // before "channel", whose pattern also matches <@&...> loosely
    role.regex = QRegularExpression(R"(^<@&(\d+)>)");
    role.match = anyScope(role.regex);
    role.parse = [](const Capture &match, NestedParseFn, ParseState state) -> AstNode {
        AstNode node;
        node.type = "roleMention";
        node.content = match.captured(1);
        node.attributes["sourceChannelId"] = state.customState.value("channelId");
        return node;
    };
    // .html handled in toHtmlInternal() because of role resolution
    rules.append(role);

    // @everyone / @here
    MarkdownRule everyone;
    everyone.name = "everyone";
    everyone.order = 21;
    everyone.regex = QRegularExpression(R"(^@(everyone|here)\b)");
    everyone.match = anyScope(everyone.regex);
    everyone.parse = [](const Capture &match, NestedParseFn, ParseState) -> AstNode {
        AstNode node;
        node.type = "everyone";
        node.content = match.captured(1);
        return node;
    };
    everyone.html = [](const AstNode &node, RenderChildren) -> QString {
        return QStringLiteral("<span class=\"mention\">@%1</span>").arg(node.content.toHtmlEscaped());
    };
    rules.append(everyone);

    // </command name:id> slash command mentions
    MarkdownRule command;
    command.name = "commandMention";
    command.order = 21;
    command.regex = QRegularExpression(R"(^<\/([\w-]+(?: [\w-]+){0,2}):(\d+)>)");
    command.match = anyScope(command.regex);
    command.parse = [](const Capture &match, NestedParseFn, ParseState) -> AstNode {
        AstNode node;
        node.type = "commandMention";
        node.content = match.captured(1);
        return node;
    };
    command.html = [](const AstNode &node, RenderChildren) -> QString {
        return QStringLiteral("<span class=\"mention\">/%1</span>").arg(node.content.toHtmlEscaped());
    };
    rules.append(command);
}

QString Parser::roleMentionHtml(const AstNode &node) const
{
    RoleMention role;
    if (roleResolver)
        role = roleResolver(Snowflake(node.content.toULongLong()),
                            Snowflake(node.attributes["sourceChannelId"].toULongLong()));
    if (role.name.isEmpty())
        role.name = QStringLiteral("deleted-role");
    if (!role.color.isValid())
        return QStringLiteral("<span class=\"mention\">@%1</span>").arg(role.name.toHtmlEscaped());
    return QStringLiteral("<span style=\"color: %1; background-color: rgba(%2, %3, %4, 0.1);\">@%5</span>")
            .arg(role.color.name())
            .arg(role.color.red())
            .arg(role.color.green())
            .arg(role.color.blue())
            .arg(role.name.toHtmlEscaped());
}

} // namespace Markdown
} // namespace Core
} // namespace Acheron
