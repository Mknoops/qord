/*
 * Qord, a native Discord client
 * Port of Equicord's Unindent plugin (src/plugins/unindent)
 * Original: Copyright (c) 2022 Vendicated and contributors, author Ven
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "Unindent.hpp"

#include "Plugins/JsCompat.hpp"
#include "Plugins/Registry.hpp"

#include <QRegularExpression>

#include <algorithm>
#include <limits>

namespace Qord {
namespace Plugins {
namespace Unindent {

static bool isLineTerminator(QChar c)
{
    return c == u'\n' || c == u'\r' || c == QChar(0x2028) || c == QChar(0x2029);
}

QString unindent(QString str)
{
    // Users can't send tabs (they become spaces), but bots can
    str.replace(u'\t', QLatin1String("    "));

    // str.match(/^ *(?=\S)/gm): leading spaces of lines with content. JS's
    // multiline ^ starts after any line terminator.
    qsizetype minIndent = std::numeric_limits<qsizetype>::max();
    bool any = false;
    for (qsizetype lineStart = 0; lineStart <= str.size();) {
        qsizetype i = lineStart;
        while (i < str.size() && str.at(i) == u' ')
            ++i;
        if (i < str.size() && !Js::isWhitespace(str.at(i))) {
            minIndent = std::min(minIndent, i - lineStart);
            any = true;
        }
        while (i < str.size() && !isLineTerminator(str.at(i)))
            ++i;
        lineStart = i + 1;
    }
    if (!any || minIndent == 0)
        return str;

    // str.replace(new RegExp(`^ {${minIndent}}`, "gm"), "")
    QString out;
    out.reserve(str.size());
    for (qsizetype lineStart = 0; lineStart <= str.size();) {
        qsizetype end = lineStart;
        while (end < str.size() && !isLineTerminator(str.at(end)))
            ++end;
        qsizetype spaces = 0;
        while (lineStart + spaces < end && str.at(lineStart + spaces) == u' ' && spaces < minIndent)
            ++spaces;
        const qsizetype skip = spaces == minIndent ? minIndent : 0;
        out += str.mid(lineStart + skip, end - lineStart - skip);
        if (end < str.size())
            out += str.at(end);
        lineStart = end + 1;
    }
    return out;
}

QString unindentMessage(const QString &content)
{
    // /```(.|\n)*?```/g, where JS's . matches anything but line terminators
    static const QRegularExpression block(QStringLiteral("```(?:[^\\n\\r\\x{2028}\\x{2029}]|\\n)*?```"));
    QString out;
    qsizetype last = 0;
    auto it = block.globalMatch(content);
    while (it.hasNext()) {
        const auto m = it.next();
        out += content.mid(last, m.capturedStart() - last);
        last = m.capturedEnd();

        QStringList lines = m.captured().split(u'\n');
        if (lines.size() < 2) { // do not affect inline code blocks
            out += m.captured();
            continue;
        }
        QString suffix;
        if (lines.last() == QLatin1String("```"))
            suffix = lines.takeLast();
        out += lines.first() + u'\n' + unindent(lines.mid(1).join(u'\n')) + u'\n' + suffix;
    }
    out += content.mid(last);
    return out;
}

} // namespace Unindent

QByteArray qord_manifest_unindent();

void qord_register_unindent(Registry &registry)
{
    registry.add({ QStringLiteral("unindent/plugin.json"),
                   [] { return std::make_unique<UnindentPlugin>(); }, qord_manifest_unindent() });
}

} // namespace Plugins
} // namespace Qord
