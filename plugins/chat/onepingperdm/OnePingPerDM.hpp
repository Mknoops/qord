/*
 * Qord, a native Discord client
 * Port of Equicord's OnePingPerDM plugin (src/plugins/onePingPerDM)
 * Original: Copyright (c) Vendicated and contributors, author ProffDea
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "Plugins/Plugin.hpp"

namespace Qord {
namespace Plugins {

namespace OnePingPerDM {

struct Config
{
    QString channelToAffect = QStringLiteral("both_dms");
    bool allowMentions = false;
    bool allowEveryone = false;
    QString ignoreUsers;
};

// upstream isPrivateChannelRead: true = notify as usual
bool isPrivateChannelRead(const Config &config, const NotificationContext &notification);

} // namespace OnePingPerDM

} // namespace Plugins
} // namespace Qord
