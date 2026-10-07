/*
 * Qord, a native Discord client
 * Port of Equicord's NormalizeMessageLinks plugin (src/equicordplugins/normalizeMessageLinks)
 * Original: Copyright (c) 2023 Vendicated and contributors, author bb010g
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "NormalizeMessageLinks.hpp"

#include "Plugins/Registry.hpp"

#include <QRegularExpression>

namespace Qord {
namespace Plugins {
namespace NormalizeMessageLinks {

QString normalizeHost(const QString &host)
{
    // The unescaped dot is upstream's; kept so behavior matches
    static const QRegularExpression re(QStringLiteral("(^|\\b)(canary\\.|ptb\\.)(discord.com)$"));
    QString out = host;
    return out.replace(re, QStringLiteral("\\1\\3"));
}

QString normalizeLink(const QString &url)
{
    static const QRegularExpression parts(QStringLiteral("^(https?://)([^/?#]*)(.*)$"),
                                          QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpressionMatch m = parts.match(url);
    if (!m.hasMatch())
        return url;
    return m.captured(1) + normalizeHost(m.captured(2)) + m.captured(3);
}

} // namespace NormalizeMessageLinks

QByteArray qord_manifest_normalizemessagelinks();

void qord_register_normalizemessagelinks(Registry &registry)
{
    registry.add({ QStringLiteral("normalizemessagelinks/plugin.json"),
                   [] { return std::make_unique<NormalizeMessageLinksPlugin>(); }, qord_manifest_normalizemessagelinks() });
}

} // namespace Plugins
} // namespace Qord
