/*
 * Qord, a native Discord client
 * Port of Equicord's WriteUpperCase plugin (src/equicordplugins/writeUpperCase)
 * Original: Copyright (c) Vendicated and contributors, authors Samwich, KrystalSkull
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "WriteUpperCase.hpp"

#include "Plugins/JsCompat.hpp"
#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

namespace Qord {
namespace Plugins {
namespace WriteUpperCase {

QStringList parseBlockedWords(const QString &setting)
{
    QStringList out;
    for (const QString &word : setting.split(QStringLiteral(", ")))
        if (!word.isEmpty())
            out.append(word);
    return out;
}

static bool isCloser(QChar c)
{
    return c == u'\'' || c == u'"' || c == u')' || c == u']';
}

static bool isTerminator(QChar c)
{
    return c == u'.' || c == u'!' || c == u'?';
}

// content.split(/(?<=[.!?]+['")\]]*)(\s+)/): even indexes are sentences,
// odd ones the whitespace between them.
static QStringList splitSentences(const QString &s)
{
    QStringList parts;
    qsizetype start = 0;
    qsizetype i = 0;
    while (i < s.size()) {
        if (!Js::isWhitespace(s.at(i))) {
            ++i;
            continue;
        }
        // Lookbehind: closers, then at least one terminator
        qsizetype j = i;
        while (j > 0 && isCloser(s.at(j - 1)))
            --j;
        if (j == 0 || !isTerminator(s.at(j - 1))) {
            ++i;
            continue;
        }
        qsizetype end = i;
        while (end < s.size() && Js::isWhitespace(s.at(end)))
            ++end;
        parts.append(s.mid(start, i - start));
        parts.append(s.mid(i, end - i));
        start = end;
        i = end;
    }
    parts.append(s.mid(start));
    return parts;
}

QString capitalize(const QString &content, const QStringList &blockedWords)
{
    const QStringList parts = splitSentences(content);
    QString out;
    for (qsizetype i = 0; i < parts.size(); ++i) {
        const QString &element = parts.at(i);
        if (i % 2 == 1 || element.isEmpty()) {
            out += element;
            continue;
        }
        const QString lower = element.toLower();
        bool blocked = false;
        for (const QString &word : blockedWords)
            if (lower.startsWith(word.toLower())) {
                blocked = true;
                break;
            }
        // element.charAt(0).toUpperCase() + element.slice(1)
        out += blocked ? element : QString(element.at(0)).toUpper() + element.mid(1);
    }
    return out;
}

} // namespace WriteUpperCase

void WriteUpperCasePlugin::start(PluginContext &ctx)
{
    context = &ctx;
    onSettingChanged({});
}

void WriteUpperCasePlugin::onSettingChanged(const QString &key)
{
    Q_UNUSED(key);
    blocked = WriteUpperCase::parseBlockedWords(context->settings().stringValue(QStringLiteral("blockedWords")));
}

void WriteUpperCasePlugin::onBeforeSend(OutgoingMessage &message)
{
    message.content = WriteUpperCase::capitalize(message.content, blocked);
}

QByteArray qord_manifest_writeuppercase();

void qord_register_writeuppercase(Registry &registry)
{
    registry.add({ QStringLiteral("writeuppercase/plugin.json"),
                   [] { return std::make_unique<WriteUpperCasePlugin>(); }, qord_manifest_writeuppercase() });
}

} // namespace Plugins
} // namespace Qord
