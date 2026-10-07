#include "TextDiff.hpp"

#include <vector>

namespace Qord {
namespace Plugins {

namespace {

QStringList tokenize(const QString &text)
{
    QStringList tokens;
    qsizetype i = 0;
    while (i < text.size()) {
        if (text.at(i) == u'<') {
            const QString next2 = text.mid(i + 1, 2);
            // Custom emoji <:name:id> / <a:name:id>, mentions <@..>, <#..>
            // (upstream's check for "<:" compares two characters to ":", so
            // only "<a:" emoji take this branch; "<:" falls through to the
            // per-character path, which we keep for identical diffs)
            const bool emoji = next2 == QLatin1String(":") || next2 == QLatin1String("a:");
            const bool mention = i + 1 < text.size() && (text.at(i + 1) == u'@' || text.at(i + 1) == u'#');
            if (emoji || mention) {
                const qsizetype end = text.indexOf(u'>', i);
                if (end != -1) {
                    tokens.append(text.mid(i, end - i + 1));
                    i = end + 1;
                    continue;
                }
            }
        }
        // One code point
        const qsizetype len = text.at(i).isHighSurrogate() && i + 1 < text.size() && text.at(i + 1).isLowSurrogate() ? 2 : 1;
        tokens.append(text.mid(i, len));
        i += len;
    }
    return tokens;
}

void push(QList<DiffPart> &parts, DiffPart::Kind kind, const QString &text)
{
    parts.append({ kind, text });
}

} // namespace

QList<DiffPart> createWordDiff(const QString &oldText, const QString &newText)
{
    using K = DiffPart::Kind;
    QList<DiffPart> parts;

    if (oldText.size() < newText.size() && newText.startsWith(oldText)) {
        if (!oldText.isEmpty())
            push(parts, K::Unchanged, oldText);
        push(parts, K::Added, newText.mid(oldText.size()));
        return parts;
    }
    if (oldText.size() < newText.size() && newText.endsWith(oldText)) {
        push(parts, K::Added, newText.left(newText.size() - oldText.size()));
        if (!oldText.isEmpty())
            push(parts, K::Unchanged, oldText);
        return parts;
    }
    if (newText.size() < oldText.size() && oldText.startsWith(newText)) {
        if (!newText.isEmpty())
            push(parts, K::Unchanged, newText);
        push(parts, K::Removed, oldText.mid(newText.size()));
        return parts;
    }
    if (newText.size() < oldText.size() && oldText.endsWith(newText)) {
        push(parts, K::Removed, oldText.left(oldText.size() - newText.size()));
        if (!newText.isEmpty())
            push(parts, K::Unchanged, newText);
        return parts;
    }

    const QStringList a = tokenize(oldText);
    const QStringList b = tokenize(newText);
    const qsizetype n = a.size(), m = b.size();
    std::vector<std::vector<int>> dp(size_t(n + 1), std::vector<int>(size_t(m + 1), 0));
    for (qsizetype i = 1; i <= n; ++i)
        for (qsizetype j = 1; j <= m; ++j)
            dp[i][j] = a.at(i - 1) == b.at(j - 1) ? dp[i - 1][j - 1] + 1 : std::max(dp[i - 1][j], dp[i][j - 1]);

    QList<DiffPart> reversed;
    qsizetype i = n, j = m;
    while (i > 0 || j > 0) {
        if (i > 0 && j > 0 && a.at(i - 1) == b.at(j - 1)) {
            reversed.append({ K::Unchanged, a.at(i - 1) });
            --i;
            --j;
        } else if (j > 0 && (i == 0 || dp[i][j - 1] > dp[i - 1][j])) {
            reversed.append({ K::Added, b.at(j - 1) });
            --j;
        } else if (i > 0) {
            reversed.append({ K::Removed, a.at(i - 1) });
            --i;
        }
    }

    for (auto it = reversed.crbegin(); it != reversed.crend(); ++it) {
        if (!parts.isEmpty() && parts.last().kind == it->kind)
            parts.last().text += it->text;
        else
            parts.append(*it);
    }
    return parts;
}

} // namespace Plugins
} // namespace Qord
