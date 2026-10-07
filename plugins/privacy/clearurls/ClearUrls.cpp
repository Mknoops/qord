/*
 * Qord, a native Discord client
 * Port of Equicord's ClearURLs plugin (src/plugins/clearURLs)
 * Original: Copyright (c) 2022 Vendicated and contributors, authors adryd and thororen
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ClearUrls.hpp"

#include "Plugins/JsCompat.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QUrl>

#include <algorithm>
#include <cctype>
#include <cstring>

namespace Qord {
namespace Plugins {
namespace ClearUrls {

namespace {

// ---- JSON key order -------------------------------------------------------
// QJsonObject sorts keys, but upstream applies providers in file order
// (Object.entries), so read the order from the raw text.
class KeyScanner
{
public:
    explicit KeyScanner(const QByteArray &d)
        : data(d)
    {
    }

    QStringList providerKeys()
    {
        QStringList keys;
        skipWs();
        if (!consume('{'))
            return keys;
        while (true) {
            skipWs();
            if (peek() == '}' || atEnd())
                break;
            QString key = readString();
            skipWs();
            consume(':');
            skipWs();
            if (key == QLatin1String("providers") && peek() == '{') {
                ++pos;
                while (true) {
                    skipWs();
                    if (peek() == '}' || atEnd())
                        break;
                    keys.append(readString());
                    skipWs();
                    consume(':');
                    skipValue();
                    skipWs();
                    consume(',');
                }
                return keys;
            }
            skipValue();
            skipWs();
            consume(',');
        }
        return keys;
    }

private:
    bool atEnd() const { return pos >= data.size(); }
    char peek() const { return atEnd() ? '\0' : data.at(pos); }
    bool consume(char c)
    {
        if (peek() != c)
            return false;
        ++pos;
        return true;
    }
    void skipWs()
    {
        while (!atEnd() && (data.at(pos) == ' ' || data.at(pos) == '\n' || data.at(pos) == '\r' || data.at(pos) == '\t'))
            ++pos;
    }
    QString readString()
    {
        const qsizetype start = pos;
        if (!consume('"'))
            return {};
        while (!atEnd() && data.at(pos) != '"')
            pos += data.at(pos) == '\\' ? 2 : 1;
        ++pos;
        const QByteArray raw = "[" + data.mid(start, pos - start) + "]";
        return QJsonDocument::fromJson(raw).array().at(0).toString();
    }
    void skipValue()
    {
        skipWs();
        const char c = peek();
        if (c == '"') {
            readString();
        } else if (c == '{' || c == '[') {
            int depth = 0;
            while (!atEnd()) {
                const char d = data.at(pos);
                if (d == '"') {
                    readString();
                    continue;
                }
                if (d == '{' || d == '[')
                    ++depth;
                else if (d == '}' || d == ']')
                    --depth;
                ++pos;
                if (depth == 0)
                    break;
            }
        } else {
            while (!atEnd() && data.at(pos) != ',' && data.at(pos) != '}' && data.at(pos) != ']')
                ++pos;
        }
    }

    const QByteArray &data;
    qsizetype pos = 0;
};

// ---- WHATWG-style serialization -------------------------------------------

void appendPercentEncoded(QString &out, QChar c, const char *extra)
{
    const char16_t u = c.unicode();
    if (u > 0x20 && u < 0x7F && !std::strchr(extra, char(u))) {
        out += c;
        return;
    }
    const QByteArray utf8 = QString(c).toUtf8();
    for (unsigned char b : utf8)
        out += QStringLiteral("%%1").arg(b, 2, 16, QLatin1Char('0')).toUpper();
}

QString encodeSet(const QString &s, const char *extra)
{
    QString out;
    out.reserve(s.size());
    for (qsizetype i = 0; i < s.size(); ++i) {
        // Keep surrogate pairs together
        if (s.at(i).isHighSurrogate() && i + 1 < s.size() && s.at(i + 1).isLowSurrogate()) {
            for (unsigned char b : s.mid(i, 2).toUtf8())
                out += QStringLiteral("%%1").arg(b, 2, 16, QLatin1Char('0')).toUpper();
            ++i;
            continue;
        }
        appendPercentEncoded(out, s.at(i), extra);
    }
    return out;
}

bool isDotSegment(const QString &seg)
{
    const QString l = seg.toLower();
    return l == QLatin1String(".") || l == QLatin1String("%2e");
}

bool isDoubleDotSegment(const QString &seg)
{
    const QString l = seg.toLower();
    return l == QLatin1String("..") || l == QLatin1String(".%2e") || l == QLatin1String("%2e.") ||
           l == QLatin1String("%2e%2e");
}

QString normalizePath(const QString &path)
{
    if (path.isEmpty())
        return QStringLiteral("/");
    const QStringList segments = path.mid(1).split(u'/');
    QStringList out;
    for (qsizetype i = 0; i < segments.size(); ++i) {
        const QString &seg = segments[i];
        const bool last = i == segments.size() - 1;
        if (isDoubleDotSegment(seg)) {
            if (!out.isEmpty())
                out.removeLast();
            if (last)
                out.append(QString());
        } else if (isDotSegment(seg)) {
            if (last)
                out.append(QString());
        } else {
            out.append(seg);
        }
    }
    return u'/' + encodeSet(out.join(u'/'), "\"<>`{}");
}

} // namespace

QString ParsedUrl::href() const
{
    QString out = prefix;
    if (hasQuery)
        out += u'?' + query;
    out += fragment;
    return out;
}

bool parseUrl(const QString &input, ParsedUrl &out)
{
    const qsizetype schemeEnd = input.indexOf(QLatin1String("://"));
    if (schemeEnd <= 0)
        return false;
    const QString scheme = input.left(schemeEnd).toLower();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https"))
        return false;

    qsizetype i = schemeEnd + 3;
    qsizetype authorityEnd = i;
    while (authorityEnd < input.size() && input.at(authorityEnd) != u'/' && input.at(authorityEnd) != u'?' &&
           input.at(authorityEnd) != u'#' && input.at(authorityEnd) != u'\\')
        ++authorityEnd;
    QString authority = input.mid(i, authorityEnd - i);

    QString userinfo;
    const qsizetype at = authority.lastIndexOf(u'@');
    if (at >= 0) {
        userinfo = authority.left(at + 1);
        authority = authority.mid(at + 1);
    }

    QString host = authority;
    QString port;
    if (host.startsWith(u'[')) {
        const qsizetype close = host.indexOf(u']');
        if (close < 0)
            return false;
        port = host.mid(close + 1);
        host = host.left(close + 1);
        if (!port.isEmpty()) {
            if (!port.startsWith(u':'))
                return false;
            port = port.mid(1);
        }
    } else {
        const qsizetype colon = host.lastIndexOf(u':');
        if (colon >= 0) {
            port = host.mid(colon + 1);
            host = host.left(colon);
        }
    }
    if (host.isEmpty())
        return false;

    if (!host.startsWith(u'[')) {
        static const QString forbidden = QStringLiteral(" #%/:<>?@[\\]^|");
        for (QChar c : host)
            if (forbidden.contains(c) || c.unicode() < 0x20 || c.unicode() == 0x7F)
                return false;
        const QByteArray ace = QUrl::toAce(host);
        if (ace.isEmpty())
            return false;
        host = QString::fromLatin1(ace).toLower();
    } else if (QUrl(QStringLiteral("http://") + host + u'/').host().isEmpty()) {
        return false;
    }

    if (!port.isEmpty()) {
        bool ok = false;
        for (QChar c : port)
            if (!c.isDigit() || c.unicode() > 127)
                return false;
        const uint n = port.toUInt(&ok);
        if (!ok || n > 65535)
            return false;
        port = ((scheme == QLatin1String("http") && n == 80) || (scheme == QLatin1String("https") && n == 443))
                ? QString()
                : QString::number(n);
    }

    qsizetype pathEnd = authorityEnd;
    while (pathEnd < input.size() && input.at(pathEnd) != u'?' && input.at(pathEnd) != u'#')
        ++pathEnd;
    QString path = input.mid(authorityEnd, pathEnd - authorityEnd);
    path.replace(u'\\', u'/');

    // username[:password]@, each part in the userinfo percent-encode set
    QString credentials;
    if (!userinfo.isEmpty()) {
        const QString raw = userinfo.chopped(1); // drop '@'
        const qsizetype colon = raw.indexOf(u':');
        const char *userinfoSet = "\"#<>?`{}/:;=@[\\]^|";
        const QString user = encodeSet(colon < 0 ? raw : raw.left(colon), userinfoSet);
        const QString pass = colon < 0 ? QString() : encodeSet(raw.mid(colon + 1), userinfoSet);
        if (!user.isEmpty() || !pass.isEmpty())
            credentials = user + (pass.isEmpty() ? QString() : u':' + pass) + u'@';
    }

    out = {};
    out.prefix = scheme + QLatin1String("://") + credentials + host +
                 (port.isEmpty() ? QString() : u':' + port) + normalizePath(path);

    qsizetype fragStart = input.indexOf(u'#', pathEnd);
    const qsizetype queryEnd = fragStart < 0 ? input.size() : fragStart;
    if (pathEnd < input.size() && input.at(pathEnd) == u'?') {
        out.hasQuery = true;
        out.query = encodeSet(input.mid(pathEnd + 1, queryEnd - pathEnd - 1), "\"#<>'");
    }
    if (fragStart >= 0)
        out.fragment = u'#' + encodeSet(input.mid(fragStart + 1), "\"<>`");
    return true;
}

static QString percentDecode(const QString &s)
{
    QByteArray bytes = s.toUtf8();
    QByteArray out;
    out.reserve(bytes.size());
    for (qsizetype i = 0; i < bytes.size(); ++i) {
        const char c = bytes.at(i);
        if (c == '+') {
            out += ' ';
        } else if (c == '%' && i + 2 < bytes.size() && std::isxdigit(uchar(bytes.at(i + 1))) &&
                   std::isxdigit(uchar(bytes.at(i + 2)))) {
            out += char(QByteArray(bytes.constData() + i + 1, 2).toInt(nullptr, 16));
            i += 2;
        } else {
            out += c;
        }
    }
    return QString::fromUtf8(out);
}

QList<QueryParam> parseQuery(const QString &query)
{
    QList<QueryParam> params;
    for (const QString &part : query.split(u'&')) {
        if (part.isEmpty())
            continue;
        const qsizetype eq = part.indexOf(u'=');
        params.append({ percentDecode(eq < 0 ? part : part.left(eq)), eq < 0 ? QString() : percentDecode(part.mid(eq + 1)) });
    }
    return params;
}

static QString formEncode(const QString &s)
{
    QString out;
    for (unsigned char b : s.toUtf8()) {
        if (b == 0x2A || b == 0x2D || b == 0x2E || b == 0x5F || (b >= 0x30 && b <= 0x39) ||
            (b >= 0x41 && b <= 0x5A) || (b >= 0x61 && b <= 0x7A))
            out += QChar(b);
        else if (b == 0x20)
            out += u'+';
        else
            out += QStringLiteral("%%1").arg(b, 2, 16, QLatin1Char('0')).toUpper();
    }
    return out;
}

QString serializeQuery(const QList<QueryParam> &params)
{
    QStringList parts;
    for (const auto &p : params)
        parts.append(formEncode(p.name) + u'=' + formEncode(p.value));
    return parts.join(u'&');
}

bool RuleSet::load(const QByteArray &json, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error)
            *error = parseError.errorString();
        return false;
    }
    const QJsonObject providerObj = doc.object().value(QLatin1String("providers")).toObject();
    if (providerObj.isEmpty()) {
        if (error)
            *error = QStringLiteral("no providers in rules data");
        return false;
    }

    QStringList order = KeyScanner(json).providerKeys();
    // Fall back to sorted order for anything the scanner missed
    for (const QString &k : providerObj.keys())
        if (!order.contains(k))
            order.append(k);

    QList<Provider> loaded;
    int skippedCount = 0;
    const auto compile = [&skippedCount](const QString &pattern, QRegularExpression &out) {
        auto re = Js::compile(pattern, QStringLiteral("i"));
        if (!re) {
            ++skippedCount;
            return false;
        }
        out = re->regex;
        return true;
    };

    for (const QString &name : order) {
        if (!providerObj.contains(name))
            continue;
        const QJsonObject p = providerObj.value(name).toObject();
        Provider provider;
        provider.name = name;
        if (!compile(p.value(QLatin1String("urlPattern")).toString(), provider.urlPattern))
            continue;
        const auto compileList = [&](const char *key, QList<QRegularExpression> &out) {
            for (const auto &v : p.value(QLatin1String(key)).toArray()) {
                QRegularExpression re;
                if (compile(v.toString(), re))
                    out.append(re);
            }
        };
        compileList("rules", provider.rules);
        compileList("rawRules", provider.rawRules);
        compileList("exceptions", provider.exceptions);
        loaded.append(std::move(provider));
    }

    providers = std::move(loaded);
    skipped = skippedCount;
    return true;
}

QString RuleSet::cleanUrl(const QString &input) const
{
    ParsedUrl url;
    if (!parseUrl(input, url))
        return input;
    // Upstream: return the match untouched when there are no search params
    if (!url.hasQuery || parseQuery(url.query).isEmpty())
        return input;

    for (const Provider &provider : providers) {
        const QString href = url.href();
        if (!provider.urlPattern.match(href).hasMatch())
            continue;
        bool excepted = false;
        for (const auto &ex : provider.exceptions)
            if (ex.match(href).hasMatch()) {
                excepted = true;
                break;
            }
        if (excepted)
            continue;

        if (!provider.rules.isEmpty()) {
            QList<QueryParam> params = parseQuery(url.query);
            QSet<QString> toDelete;
            for (const auto &param : params)
                for (const auto &rule : provider.rules)
                    if (rule.match(param.name).hasMatch()) {
                        toDelete.insert(param.name);
                        break;
                    }
            if (!toDelete.isEmpty()) {
                params.erase(std::remove_if(params.begin(), params.end(),
                                            [&](const QueryParam &p) { return toDelete.contains(p.name); }),
                             params.end());
                url.query = serializeQuery(params);
                url.hasQuery = !url.query.isEmpty();
            }
        }

        QString cleaned = url.href();
        for (const auto &raw : provider.rawRules) {
            const QRegularExpressionMatch m = raw.match(cleaned);
            if (m.hasMatch())
                cleaned.remove(m.capturedStart(), m.capturedLength());
        }
        if (!parseUrl(cleaned, url))
            return input; // upstream would throw here and leave the message alone
    }
    return url.href();
}

QString RuleSet::cleanMessage(const QString &content) const
{
    static const QRegularExpression hasUrl(QStringLiteral("http(s)?://"));
    if (!hasUrl.match(content).hasMatch())
        return content;
    static const QRegularExpression urlRe(QStringLiteral("https?://[^%1<]+[^<.,:;\"'>)|\\]%1]")
                                                  .arg(QLatin1String(Js::WhitespaceClass)));
    QString out;
    qsizetype last = 0;
    auto it = urlRe.globalMatch(content);
    while (it.hasNext()) {
        const auto m = it.next();
        out += content.mid(last, m.capturedStart() - last);
        out += cleanUrl(m.captured());
        last = m.capturedEnd();
    }
    out += content.mid(last);
    return out;
}

} // namespace ClearUrls
} // namespace Plugins
} // namespace Qord
