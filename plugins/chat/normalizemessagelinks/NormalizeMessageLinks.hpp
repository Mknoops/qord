/*
 * Qord, a native Discord client
 * Port of Equicord's NormalizeMessageLinks plugin (src/equicordplugins/normalizeMessageLinks)
 * Original: Copyright (c) 2023 Vendicated and contributors, author bb010g
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "Plugins/Plugin.hpp"

namespace Qord {
namespace Plugins {
namespace NormalizeMessageLinks {

// host.replace(/(^|\b)(canary\.|ptb\.)(discord.com)$/, "$1$3")
QString normalizeHost(const QString &host);
// Applies normalizeHost to the host of an http(s) URL.
QString normalizeLink(const QString &url);

} // namespace NormalizeMessageLinks

class NormalizeMessageLinksPlugin : public Plugin
{
public:
    [[nodiscard]] Hooks hooks() const override { return Hook::TransformCopiedMessageLink; }
    QString transformCopiedMessageLink(const QString &url) override
    {
        return NormalizeMessageLinks::normalizeLink(url);
    }
};

} // namespace Plugins
} // namespace Qord
