/*
 * Qord, a native Discord client
 * Port of Equicord's PolishWording plugin (src/equicordplugins/polishWording)
 * Original: Copyright (c) Vendicated and contributors, authors Samwich, WKoA
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "PolishWording.hpp"

#include "Plugins/JsCompat.hpp"
#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

#include <QRandomGenerator>

#include <utility>

namespace Qord {
namespace Plugins {
namespace PolishWording {

namespace {

// Order matters: it is the alternation order of the regexes below
const QList<std::pair<QString, QString>> &contractions()
{
    static const QList<std::pair<QString, QString>> map = {
        { "wasn't", "was not" }, { "can't", "cannot" }, { "don't", "do not" }, { "won't", "will not" },
        { "isn't", "is not" }, { "aren't", "are not" }, { "haven't", "have not" }, { "hasn't", "has not" },
        { "hadn't", "had not" }, { "doesn't", "does not" }, { "didn't", "did not" },
        { "shouldn't", "should not" }, { "wouldn't", "would not" }, { "couldn't", "could not" },
        { "that's", "that is" }, { "what's", "what is" }, { "there's", "there is" }, { "how's", "how is" },
        { "where's", "where is" }, { "when's", "when is" }, { "who's", "who is" }, { "why's", "why is" },
        { "you'll", "you will" }, { "i'll", "I will" }, { "they'll", "they will" }, { "it'll", "it will" },
        { "i'm", "I am" }, { "you're", "you are" }, { "they're", "they are" }, { "he's", "he is" },
        { "she's", "she is" }, { "i've", "I have" }, { "you've", "you have" }, { "we've", "we have" },
        { "they've", "they have" }, { "you'd", "you would" }, { "he'd", "he would" }, { "she'd", "she would" },
        { "it'd", "it would" }, { "we'd", "we would" }, { "they'd", "they would" }, { "y'all", "you all" },
        { "here's", "here is" },
    };
    return map;
}

bool isAsciiLetter(QChar c)
{
    return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z');
}

QList<bool> capData(const QString &s)
{
    QList<bool> out;
    for (QChar c : s)
        if (isAsciiLetter(c))
            out.append(c.isUpper());
    return out;
}

QString restoreCap(const QString &s, const QList<bool> &data)
{
    QString out;
    qsizetype index = 0;
    for (QChar c : s) {
        if (!isAsciiLetter(c)) {
            out += c;
            continue;
        }
        const bool upper = index < data.size() && data.at(index);
        out += upper ? c.toUpper() : c.toLower();
        // Past the end of the data, keep using the last entry
        if (index < data.size() - 1)
            ++index;
    }
    return out;
}

Js::RegExp mustCompile(const QString &pattern, const QString &flags)
{
    return *Js::compile(pattern, flags);
}

QString alternation(bool withoutApostrophes)
{
    QStringList keys;
    for (const auto &[contraction, expansion] : contractions()) {
        Q_UNUSED(expansion);
        keys.append(withoutApostrophes ? QString(contraction).remove(u'\'') : contraction);
    }
    return QStringLiteral("\\b(") + keys.join(u'|') + QStringLiteral(")\\b");
}

} // namespace

QString ensureApostrophe(const QString &text)
{
    static const Js::RegExp re = mustCompile(alternation(true), QStringLiteral("gi"));
    return Js::replace(text, re, [](const QRegularExpressionMatch &m) {
        const QString match = m.captured();
        const QString lower = match.toLower();
        for (const auto &[contraction, expansion] : contractions())
            if (QString(contraction).remove(u'\'') == lower)
                return restoreCap(contraction, capData(match));
        return match;
    });
}

QString expandContractions(const QString &text)
{
    static const Js::RegExp re = mustCompile(alternation(false), QStringLiteral("gi"));
    return Js::replace(text, re, [](const QRegularExpressionMatch &m) {
        const QString match = m.captured();
        const QString lower = match.toLower();
        for (const auto &[contraction, expansion] : contractions())
            if (contraction == lower)
                return restoreCap(expansion, capData(match));
        return match;
    });
}

QString capitalize(const QString &text, const QString &blockedWords)
{
    static const Js::RegExp splitter = mustCompile(
            QStringLiteral(R"(((?<!\w\.\w.)(?<!\b[A-Z][a-z]\.)(?<![A-Z]\.)(?<!\.)(?<=[.?!])\s+|\n+))"), QString());
    static const Js::RegExp firstWord = mustCompile(QStringLiteral(R"(^\s*([\w'-]+))"), QString());
    static const Js::RegExp firstChar = mustCompile(QStringLiteral(R"(^(\s*)(\S))"), QString());
    static const Js::RegExp lonelyI = mustCompile(QStringLiteral(R"(\bi\b(?!\s+is\b)(?=['\s]|$))"), QStringLiteral("g"));
    static const Js::RegExp blockedSplit = mustCompile(QStringLiteral(R"(,\s?)"), QString());

    QStringList blocked;
    for (const auto &word : Js::split(blockedWords, blockedSplit))
        if (word && !word->isEmpty())
            blocked.append(word->toLower());

    QString result;
    for (const auto &part : Js::split(text, splitter)) {
        if (!part)
            continue;
        const QString &element = *part;
        const bool isSentence = !Js::test(element, splitter);
        if (!isSentence) {
            result += element;
            continue;
        }
        if (element.isEmpty())
            continue;
        if (Js::trim(element).isEmpty()) {
            result += element;
            continue;
        }
        const QRegularExpressionMatch fw = firstWord.regex.match(element);
        const QString word = fw.hasMatch() ? fw.captured(1).toLower() : QString();
        const bool isBlocked = !word.isEmpty() && blocked.contains(word);
        if (!isBlocked && !element.startsWith(QLatin1String("http"))) {
            result += Js::replace(element, firstChar, [](const QRegularExpressionMatch &m) {
                return m.captured(1) + m.captured(2).toUpper();
            });
        } else {
            result += element;
        }
    }
    return Js::replace(result, lonelyI, QStringLiteral("I"));
}

QString addPeriods(const QString &text)
{
    if (text.isEmpty())
        return {};
    static const Js::RegExp url = mustCompile(QStringLiteral(R"(https?:\/\/\S+$|www\.\S+$)"), QString());
    const QStringList lines = text.split(u'\n');
    QStringList processed;
    for (qsizetype i = 0; i < lines.size(); ++i) {
        // trimEnd()
        QString stripped = lines.at(i);
        while (!stripped.isEmpty() && Js::isWhitespace(stripped.back()))
            stripped.chop(1);
        if (stripped.isEmpty()) {
            if (i < lines.size() - 1)
                processed.append(QString());
            continue;
        }
        const QChar last = stripped.back();
        const bool alnum = last.unicode() < 128 && (isAsciiLetter(last) || last.isDigit());
        if (alnum && !Js::test(stripped, url))
            processed.append(stripped + u'.');
        else
            processed.append(stripped);
    }
    return processed.join(u'\n');
}

QString process(const QString &input, const Options &options, const Random &random)
{
    if (options.quickDisable)
        return input;

    static const Js::RegExp codeBlock = mustCompile(QStringLiteral(R"(```[\s\S]*?```|`[\s\S]*?`)"), QStringLiteral("g"));
    static const Js::RegExp placeholder = mustCompile(QStringLiteral(R"(__CODE_BLOCK_(\d+)__)"), QStringLiteral("g"));

    QStringList blocks;
    QString text = Js::replace(input, codeBlock, [&blocks](const QRegularExpressionMatch &m) {
        blocks.append(m.captured());
        return QStringLiteral("__CODE_BLOCK_%1__").arg(blocks.size() - 1);
    });

    if (options.fixApostrophes || options.expandContractions)
        text = ensureApostrophe(text);
    if (options.fixCapitalization)
        text = capitalize(text, options.blockedWords);
    if (options.fixPunctuation && random() * 100 < options.fixPunctuationFrequency)
        text = addPeriods(text);
    if (options.expandContractions)
        text = expandContractions(text);

    return Js::replace(text, placeholder, [&blocks](const QRegularExpressionMatch &m) {
        const qsizetype index = m.captured(1).toLongLong();
        // Upstream would insert "undefined" for a placeholder the user typed
        // themselves; leave such text alone instead
        return index >= 0 && index < blocks.size() ? blocks.at(index) : m.captured();
    });
}

} // namespace PolishWording

void PolishWordingPlugin::start(PluginContext &ctx)
{
    context = &ctx;
    onSettingChanged({});
}

void PolishWordingPlugin::onSettingChanged(const QString &key)
{
    Q_UNUSED(key);
    PluginSettings &s = context->settings();
    options.quickDisable = s.boolValue(QStringLiteral("quickDisable"));
    options.blockedWords = s.stringValue(QStringLiteral("blockedWords"));
    options.fixApostrophes = s.boolValue(QStringLiteral("fixApostrophes"));
    options.expandContractions = s.boolValue(QStringLiteral("expandContractions"));
    options.fixCapitalization = s.boolValue(QStringLiteral("fixCapitalization"));
    options.fixPunctuation = s.boolValue(QStringLiteral("fixPunctuation"));
    options.fixPunctuationFrequency = int(s.intValue(QStringLiteral("fixPunctuationFrequency")));
}

void PolishWordingPlugin::onBeforeSend(OutgoingMessage &message)
{
    message.content = PolishWording::process(message.content, options,
                                             [] { return QRandomGenerator::global()->generateDouble(); });
}

QByteArray qord_manifest_polishwording();

void qord_register_polishwording(Registry &registry)
{
    registry.add({ QStringLiteral("polishwording/plugin.json"),
                   [] { return std::make_unique<PolishWordingPlugin>(); }, qord_manifest_polishwording() });
}

} // namespace Plugins
} // namespace Qord
