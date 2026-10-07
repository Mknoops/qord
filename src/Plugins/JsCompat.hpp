#pragma once

#include <QRegularExpression>
#include <QString>

#include <optional>

namespace Qord {
namespace Plugins {
namespace Js {

// Helpers that reproduce the JavaScript semantics Equicord plugins rely on,
// so ported logic behaves the same as upstream.

// Characters JavaScript's \s matches.
inline constexpr const char *WhitespaceClass =
        R"(\t\n\x{0B}\f\r \x{A0}\x{1680}\x{2000}-\x{200A}\x{2028}\x{2029}\x{202F}\x{205F}\x{3000}\x{FEFF})";

bool isWhitespace(QChar c);
// String.prototype.trim
QString trim(const QString &s);

// A compiled JavaScript RegExp: pattern + flags.
struct RegExp
{
    QRegularExpression regex;
    bool global = false;
    bool sticky = false;
};

// Translates JS regex syntax that PCRE2 spells differently (\uXXXX, \u{X},
// [^], []) and maps flags. Returns nullopt with `error` set when the
// pattern or flags are invalid, like `new RegExp` throwing.
std::optional<RegExp> compile(const QString &pattern, const QString &flags, QString *error = nullptr);

// String.prototype.replace(regexp, replacement) including $&, $1..$99,
// $<name>, $`, $' and $$ in the replacement.
QString replace(const QString &subject, const RegExp &re, const QString &replacement);

// String.prototype.replaceAll(searchString, replacement): literal search,
// replacement still expands $&, $`, $' and $$.
QString replaceAllLiteral(const QString &subject, const QString &search, const QString &replacement);

} // namespace Js
} // namespace Plugins
} // namespace Qord
