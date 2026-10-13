#pragma once

// Permission lists shared by plugins (PermissionsViewer, ShowHiddenChannels)

#include "Services.hpp"
#include "Types.hpp"

#include <optional>

namespace Qord {
namespace Plugins {

// A role as a row of a permissions view, with its permissions
[[nodiscard]] PermissionsEntry roleEntry(const RoleInfo &role);

// A channel's overwrites as PermissionsViewer lists them: roles by position,
// highest first (unknown roles last), then members in Discord's order.
// Members the client hasn't loaded are requested from Discord.
[[nodiscard]] std::optional<PermissionsView> channelPermissionsView(GuildDirectory &guilds, Id accountId,
                                                                    Id channelId);

} // namespace Plugins
} // namespace Qord
