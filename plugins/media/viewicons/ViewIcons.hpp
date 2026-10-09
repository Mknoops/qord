#pragma once

#include "Plugins/Plugin.hpp"

namespace Qord {
namespace Plugins {

namespace ViewIcons {
// Upstream's openImage(): non-animated images get the chosen format, animated
// ones (animated=true) webp, Discord's own default assets png; the size query
// is set to the chosen size. Qord's default avatars come from the CDN's
// /embed/avatars/ instead of a local /assets/ path, so they count as assets.
QString imageUrl(const QString &url, const QString &format, const QString &size);
} // namespace ViewIcons

class ViewIconsPlugin : public Plugin
{
public:
    [[nodiscard]] Hooks hooks() const override
    {
        return Hook::UserContextMenu | Hook::ChannelContextMenu | Hook::ProfileImages;
    }
    void start(PluginContext &ctx) override { context = &ctx; }
    void stop() override { context = nullptr; }

    QList<MenuItem> userContextMenuItems(const UserContext &user) override;
    void onUserContextMenuAction(const QString &id, const UserContext &user) override;
    QList<MenuItem> channelContextMenuItems(const ChannelContext &channel) override;
    void onChannelContextMenuAction(const QString &id, const ChannelContext &channel) override;
    bool onProfileImageClicked(const ProfileImageContext &image) override;

private:
    void open(const QString &url);

    PluginContext *context = nullptr;
};

} // namespace Plugins
} // namespace Qord
