#include "JsCompat.hpp"

#include <algorithm>

namespace Qord {
namespace Plugins {
namespace Js {

bool isWhitespace(QChar c)
{
    const char16_t u = c.unicode();
    switch (u) {
    case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: case 0x20: case 0xA0: case 0x1680:
    case 0x2028: case 0x2029: case 0x202F: case 0x205F: case 0x3000: case 0xFEFF:
        return true;
    default:
        return u >= 0x2000 && u <= 0x200A;
    }
}

QString trim(const QString &s)
{
    qsizetype start = 0, end = s.size();
    while (start < end && isWhitespace(s.at(start)))
        ++start;
    while (end > start && isWhitespace(s.at(end - 1)))
        --end;
    return s.mid(start, end - start);
}

static bool isHex(QChar c)
{
    return (c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f') || (c >= u'A' && c <= u'F');
}

static QString translatePattern(const QString &p, bool multiline)
{
    QString out;
    out.reserve(p.size() + 8);
    bool inClass = false;
    for (qsizetype i = 0; i < p.size(); ++i) {
        const QChar c = p.at(i);
        if (c == u'\\' && i + 1 < p.size()) {
            const QChar n = p.at(i + 1);
            if (n == u'u') {
                // \uXXXX
                if (i + 5 < p.size() && isHex(p.at(i + 2)) && isHex(p.at(i + 3)) && isHex(p.at(i + 4)) &&
                    isHex(p.at(i + 5))) {
                    out += QStringLiteral("\\x{") + p.mid(i + 2, 4) + u'}';
                    i += 5;
                    continue;
                }
                // \u{X...}
                if (i + 2 < p.size() && p.at(i + 2) == u'{') {
                    qsizetype close = p.indexOf(u'}', i + 3);
                    if (close > i + 3) {
                        out += QStringLiteral("\\x{") + p.mid(i + 3, close - i - 3) + u'}';
                        i = close;
                        continue;
                    }
                }
            }
            out += c;
            out += n;
            ++i;
            continue;
        }
        if (!inClass && c == u'[') {
            if (p.mid(i, 3) == QLatin1String("[^]")) {
                out += QStringLiteral("[\\s\\S]");
                i += 2;
                continue;
            }
            if (p.mid(i, 2) == QLatin1String("[]")) {
                out += QStringLiteral("(?!)");
                i += 1;
                continue;
            }
            inClass = true;
            out += c;
            // A ']' right after '[' or '[^' is literal in PCRE but closes the
            // class in JS; the empty cases were handled above.
            continue;
        }
        if (inClass && c == u']')
            inClass = false;
        // Without the m flag JS's $ only matches at the very end; PCRE's also
        // matches before a final newline
        if (!inClass && !multiline && c == u'$') {
            out += QStringLiteral("\\z");
            continue;
        }
        out += c;
    }
    return out;
}

std::optional<RegExp> compile(const QString &pattern, const QString &flags, QString *error)
{
    RegExp re;
    QRegularExpression::PatternOptions options;
    QString seen;
    for (QChar f : flags) {
        if (seen.contains(f) || !QStringLiteral("dgimsuvy").contains(f)) {
            if (error)
                *error = QStringLiteral("Invalid flags supplied to RegExp constructor '%1'").arg(flags);
            return std::nullopt;
        }
        seen += f;
        switch (f.unicode()) {
        case u'g':
            re.global = true;
            break;
        case u'i':
            options |= QRegularExpression::CaseInsensitiveOption;
            break;
        case u'm':
            options |= QRegularExpression::MultilineOption;
            break;
        case u's':
            options |= QRegularExpression::DotMatchesEverythingOption;
            break;
        case u'y':
            re.sticky = true;
            break;
        default: // d, u, v: no effect on matching here (Qt is always Unicode)
            break;
        }
    }
    re.regex = QRegularExpression(translatePattern(pattern, options.testFlag(QRegularExpression::MultilineOption)), options);
    if (!re.regex.isValid()) {
        if (error)
            *error = QStringLiteral("Invalid regular expression: /%1/: %2").arg(pattern, re.regex.errorString());
        return std::nullopt;
    }
    return re;
}

// GetSubstitution from the ECMAScript spec.
static QString substitute(const QString &subject, const QString &matched, qsizetype position,
                          const QRegularExpressionMatch *match, const QStringList &groupNames,
                          const QString &replacement)
{
    if (!replacement.contains(u'$'))
        return replacement;
    const int captureCount = match ? match->lastCapturedIndex() : 0;
    const int groupCount = match ? match->regularExpression().captureCount() : 0;
    QString out;
    for (qsizetype i = 0; i < replacement.size(); ++i) {
        const QChar c = replacement.at(i);
        if (c != u'$' || i + 1 >= replacement.size()) {
            out += c;
            continue;
        }
        const QChar n = replacement.at(i + 1);
        if (n == u'$') {
            out += u'$';
            ++i;
        } else if (n == u'&') {
            out += matched;
            ++i;
        } else if (n == u'`') {
            out += subject.left(position);
            ++i;
        } else if (n == u'\'') {
            out += subject.mid(position + matched.size());
            ++i;
        } else if (n.isDigit() && n.unicode() < 128) {
            // Two-digit reference if valid, else one digit
            int one = n.digitValue();
            int two = -1;
            if (i + 2 < replacement.size() && replacement.at(i + 2).isDigit() && replacement.at(i + 2).unicode() < 128)
                two = one * 10 + replacement.at(i + 2).digitValue();
            if (two >= 1 && two <= groupCount) {
                if (two <= captureCount)
                    out += match->captured(two);
                i += 2;
            } else if (one >= 1 && one <= groupCount) {
                if (one <= captureCount)
                    out += match->captured(one);
                i += 1;
            } else {
                out += c;
            }
        } else if (n == u'<' && match && std::any_of(groupNames.begin(), groupNames.end(),
                                                     [](const QString &g) { return !g.isEmpty(); })) {
            qsizetype close = replacement.indexOf(u'>', i + 2);
            if (close < 0) {
                out += c;
            } else {
                out += match->captured(replacement.mid(i + 2, close - i - 2));
                i = close;
            }
        } else {
            out += c;
        }
    }
    return out;
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
static constexpr auto StickyMatch = QRegularExpression::AnchorAtOffsetMatchOption;
#else
static constexpr auto StickyMatch = QRegularExpression::AnchoredMatchOption;
#endif

QString replace(const QString &subject, const RegExp &re, const QString &replacement)
{
    const QStringList names = re.regex.namedCaptureGroups();
    QString out;
    qsizetype last = 0;
    qsizetype offset = 0;
    while (offset <= subject.size()) {
        QRegularExpressionMatch m = re.regex.match(subject, offset, QRegularExpression::NormalMatch,
                                                   re.sticky ? StickyMatch
                                                             : QRegularExpression::NoMatchOption);
        if (!m.hasMatch())
            break;
        const qsizetype start = m.capturedStart();
        const QString matched = m.captured();
        out += subject.mid(last, start - last);
        out += substitute(subject, matched, start, &m, names, replacement);
        last = start + matched.size();
        if (!re.global)
            break;
        // Empty matches advance by one code unit, as AdvanceStringIndex does
        // without the u flag
        offset = matched.isEmpty() ? last + 1 : last;
    }
    out += subject.mid(last);
    return out;
}

QString replaceAllLiteral(const QString &subject, const QString &search, const QString &replacement)
{
    QString out;
    qsizetype last = 0;
    if (search.isEmpty()) {
        // "abc".replaceAll("", "-") === "-a-b-c-"
        for (qsizetype i = 0; i <= subject.size(); ++i) {
            out += substitute(subject, QString(), i, nullptr, {}, replacement);
            if (i < subject.size())
                out += subject.at(i);
        }
        return out;
    }
    qsizetype pos = subject.indexOf(search);
    while (pos >= 0) {
        out += subject.mid(last, pos - last);
        out += substitute(subject, search, pos, nullptr, {}, replacement);
        last = pos + search.size();
        pos = subject.indexOf(search, last);
    }
    out += subject.mid(last);
    return out;
}

} // namespace Js
} // namespace Plugins
} // namespace Qord
