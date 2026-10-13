/*
 * Qord, a native Discord client
 * Port of Equicord's ShowHiddenChannels plugin (src/plugins/showHiddenChannels)
 * Original: Copyright (c) Vendicated and contributors, authors BigDuck, Average React Enjoyer, D3SOX, Ven,
 * Nuckyz, Nickyux, rini, Oggetto
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "Plugins/Plugin.hpp"

#include <memory>

namespace Qord {
namespace Plugins {

class PluginContext;

// Shows channels you can't see in the channel list, and a lock screen with
// their settings and who can see them instead of their messages.
class ShowHiddenChannelsPlugin : public Plugin
{
public:
    [[nodiscard]] Hooks hooks() const override { return Hook::HiddenChannels; }
    void start(PluginContext &context) override;
    void stop() override;

    std::optional<HiddenChannelDisplay> hiddenChannelDisplay() override;
    std::optional<LockScreen> hiddenChannelLockScreen(const HiddenChannelContext &channel) override;

    // Vencord's formatDurationVerbose ("1 hour, 0 minutes and 5 seconds")
    enum class Unit { Seconds, Minutes };
    [[nodiscard]] static QString formatDuration(qint64 amount, Unit unit);
    // The roles and users that can see (or, for a locked voice channel,
    // join) a channel: @everyone alone when it can
    using MemberLookup = std::function<std::optional<MemberInfo>(Id userId)>;
    [[nodiscard]] static QList<PermissionsEntry> allowed(const GuildInfo &guild, const ChannelInfo &channel,
                                                         quint64 permission, const MemberLookup &member);
    [[nodiscard]] static QDateTime snowflakeTime(Id id);

private:
    PluginContext *context = nullptr;
    std::shared_ptr<bool> alive;
};

} // namespace Plugins
} // namespace Qord
