/*
 * Qord, a native Discord client
 * Port of Equicord's ClearURLs plugin (src/plugins/clearURLs)
 * Original: Copyright (c) 2022 Vendicated and contributors, authors adryd and thororen
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <QByteArray>
#include <QList>
#include <QRegularExpression>
#include <QString>

namespace Qord {
namespace Plugins {
namespace ClearUrls {

// Where upstream fetches the rules from.
inline constexpr const char *RulesUrl = "https://raw.githubusercontent.com/ClearURLs/Rules/master/data.min.json";

struct Provider
{
    QString name;
    QRegularExpression urlPattern;
    QList<QRegularExpression> rules;
    QList<QRegularExpression> rawRules;
    QList<QRegularExpression> exceptions;
};

// ClearURLs' data.min.json compiled into providers, applied the way
// Equicord's plugin does: providers in file order, case-insensitive, query
// params whose name matches a rule are deleted, rawRules are cut from the
// URL, providers whose exception matches are skipped.
class RuleSet
{
public:
    // Returns false (and keeps nothing) if the data is not a ClearURLs rules
    // file. Individual invalid patterns are skipped and counted.
    bool load(const QByteArray &json, QString *error = nullptr);
    [[nodiscard]] int providerCount() const { return int(providers.size()); }
    [[nodiscard]] int skippedPatterns() const { return skipped; }
    [[nodiscard]] bool isEmpty() const { return providers.isEmpty(); }

    // Cleans one URL. URLs without query parameters, or that don't parse,
    // are returned unchanged.
    [[nodiscard]] QString cleanUrl(const QString &url) const;
    // Cleans every http(s) URL in a message.
    [[nodiscard]] QString cleanMessage(const QString &content) const;

private:
    QList<Provider> providers;
    int skipped = 0;
};

// Exposed for tests: the URL pieces Equicord's logic works on, using WHATWG
// URL serialization rules for the parts it touches.
struct ParsedUrl
{
    QString prefix; // normalized scheme://host[:port]/path
    bool hasQuery = false;
    QString query; // without '?'
    QString fragment; // with '#', or empty

    [[nodiscard]] QString href() const;
};
bool parseUrl(const QString &input, ParsedUrl &out);

struct QueryParam
{
    QString name;
    QString value;
};
QList<QueryParam> parseQuery(const QString &query);
QString serializeQuery(const QList<QueryParam> &params);

} // namespace ClearUrls
} // namespace Plugins
} // namespace Qord
