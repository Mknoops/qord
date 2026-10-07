/*
 * Qord, a native Discord client
 * Port of Equicord's TextReplace plugin (src/plugins/textReplace)
 * Original: Copyright (c) 2023 Vendicated and contributors,
 * authors AutumnVN, TheKodeToad, Etorix, Ape
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "TextReplace.hpp"

#include "Plugins/JsCompat.hpp"

#include <QJsonObject>
#include <QRegularExpression>
#include <QUuid>

namespace Qord {
namespace Plugins {
namespace TextReplace {

Scope scopeFromString(const QString &s)
{
    if (s == QLatin1String("othersMessages"))
        return Scope::OthersMessages;
    if (s == QLatin1String("allMessages"))
        return Scope::AllMessages;
    return Scope::MyMessages;
}

QString scopeToString(Scope scope)
{
    switch (scope) {
    case Scope::OthersMessages:
        return QStringLiteral("othersMessages");
    case Scope::AllMessages:
        return QStringLiteral("allMessages");
    case Scope::MyMessages:
        break;
    }
    return QStringLiteral("myMessages");
}

QList<Rule> rulesFromJson(const QJsonArray &array)
{
    QList<Rule> rules;
    for (const auto &v : array) {
        const QJsonObject o = v.toObject();
        Rule r;
        r.id = o.value(QLatin1String("id")).toString();
        if (r.id.isEmpty())
            r.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        r.name = o.value(QLatin1String("name")).toString();
        r.find = o.value(QLatin1String("find")).toString();
        r.replace = o.value(QLatin1String("replace")).toString();
        r.onlyIfIncludes = o.value(QLatin1String("onlyIfIncludes")).toString();
        r.scope = scopeFromString(o.value(QLatin1String("scope")).toString());
        rules.append(r);
    }
    return rules;
}

QJsonArray rulesToJson(const QList<Rule> &rules)
{
    QJsonArray array;
    for (const auto &r : rules) {
        QJsonObject o;
        o.insert(QLatin1String("id"), r.id);
        o.insert(QLatin1String("name"), r.name);
        o.insert(QLatin1String("find"), r.find);
        o.insert(QLatin1String("replace"), r.replace);
        o.insert(QLatin1String("onlyIfIncludes"), r.onlyIfIncludes);
        o.insert(QLatin1String("scope"), scopeToString(r.scope));
        array.append(o);
    }
    return array;
}

// upstream: str.match(/^(\/)?(.+?)(?:\/([gimsuyv]*))?$/), flags deduplicated,
// "g" when no flags part; otherwise new RegExp(str).
static std::optional<Js::RegExp> stringToRegex(const QString &str, QString *error)
{
    // \z: JS's $ only matches at the very end
    static const QRegularExpression meta(QStringLiteral("^(\\/)?(.+?)(?:\\/([gimsuyv]*))?\\z"));
    const QRegularExpressionMatch m = meta.match(str);
    if (!m.hasMatch())
        return Js::compile(str, QString(), error);
    QString flags = QStringLiteral("g");
    if (m.capturedStart(3) >= 0) {
        flags.clear();
        for (QChar c : m.captured(3))
            if (!flags.contains(c))
                flags += c;
    }
    return Js::compile(m.captured(2), flags, error);
}

QString checkRegex(const QString &find)
{
    QString error;
    return stringToRegex(find, &error) ? QString() : error;
}

static bool scopeApplies(Scope ruleScope, Scope scope)
{
    return ruleScope == Scope::AllMessages || ruleScope == scope || scope == Scope::AllMessages;
}

static QString expandNewlines(const QString &replacement)
{
    QString r = replacement;
    return r.replace(QLatin1String("\\n"), QLatin1String("\n"));
}

RuleSet RuleSet::build(QList<Rule> stringRules, QList<Rule> regexRules)
{
    RuleSet set;
    set.stringRules = std::move(stringRules);
    set.regexRules = std::move(regexRules);
    for (const Rule &rule : set.regexRules)
        set.compiled.append(rule.find.isEmpty() ? std::nullopt : stringToRegex(rule.find, nullptr));
    return set;
}

QString applyRules(QString content, Scope scope, const QList<Rule> &stringRules, const QList<Rule> &regexRules,
                   const ErrorSink &onRegexError)
{
    return applyRules(std::move(content), scope, RuleSet::build(stringRules, regexRules), onRegexError);
}

QString applyRules(QString content, Scope scope, const RuleSet &rules, const ErrorSink &onRegexError)
{
    if (content.isEmpty())
        return content;

    for (const Rule &rule : rules.stringRules) {
        if (rule.find.isEmpty())
            continue;
        if (!rule.onlyIfIncludes.isEmpty() && !content.contains(rule.onlyIfIncludes))
            continue;
        if (!scopeApplies(rule.scope, scope))
            continue;
        // ` ${content} `.replaceAll(find, replace).replace(/^\s|\s$/g, "")
        QString padded = Js::replaceAllLiteral(u' ' + content + u' ', rule.find, expandNewlines(rule.replace));
        if (!padded.isEmpty() && Js::isWhitespace(padded.front()))
            padded.remove(0, 1);
        if (!padded.isEmpty() && Js::isWhitespace(padded.back()))
            padded.chop(1);
        content = padded;
    }

    for (qsizetype i = 0; i < rules.regexRules.size(); ++i) {
        const Rule &rule = rules.regexRules[i];
        if (rule.find.isEmpty())
            continue;
        if (!rule.onlyIfIncludes.isEmpty() && !content.contains(rule.onlyIfIncludes))
            continue;
        if (!scopeApplies(rule.scope, scope))
            continue;
        const auto &re = rules.compiled.at(i);
        if (!re) {
            if (onRegexError)
                onRegexError(QStringLiteral("Invalid regex: %1").arg(rule.find));
            continue;
        }
        content = Js::replace(content, *re, expandNewlines(rule.replace));
    }

    return Js::trim(content);
}

const QSet<Id> &exemptChannelIds()
{
    static const QSet<Id> ids = {
        1102784112584040479ull, // Vencord's Text Replace Rules channel
        1419347113745059961ull, // Equicord's requests channel
        1297590739911573585ull, // Equicord support
        1026515880080842772ull, // Vencord support
    };
    return ids;
}

} // namespace TextReplace
} // namespace Plugins
} // namespace Qord
