/*
 * Qord, a native Discord client
 * Port of Equicord's ViewIcons plugin (src/plugins/viewIcons)
 * Original: Copyright (c) Vendicated and contributors, authors Ven, TheKodeToad, Nuckyz, nyx
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ViewIcons.hpp"

#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

namespace Qord {
namespace Plugins {

namespace {
const QString viewAvatar = QStringLiteral("view-avatar");
const QString viewServerAvatar = QStringLiteral("view-server-avatar");
const QString viewIcon = QStringLiteral("view-icon");
const QString viewBanner = QStringLiteral("view-banner");
const QString viewGroupIcon = QStringLiteral("view-group-channel-icon");
constexpr int GroupDm = 3;
} // namespace

QString ViewIcons::imageUrl(const QString &url, const QString &format, const QString &size)
{
    QUrl u(url);
    QUrlQuery query(u);
    QString chosen;
    if (u.path().startsWith(QLatin1String("/embed/avatars/")))
        chosen = QStringLiteral("png");
    else if (query.queryItemValue(QStringLiteral("animated")) == QLatin1String("true"))
        chosen = QStringLiteral("webp");
    else
        chosen = format;

    // URLSearchParams.set(): replace the first "size" in place and drop the
    // rest, or append it
    QList<QPair<QString, QString>> items = query.queryItems(QUrl::FullyDecoded);
    bool found = false;
    for (int i = 0; i < items.size();) {
        if (items[i].first == QLatin1String("size")) {
            if (found) {
                items.removeAt(i);
                continue;
            }
            items[i].second = size;
            found = true;
        }
        ++i;
    }
    if (!found)
        items.append({ QStringLiteral("size"), size });
    query.setQueryItems(items);
    u.setQuery(query);

    static const QRegularExpression ext(QStringLiteral("\\.(png|jpe?g|webp)$"));
    QString path = u.path();
    path.replace(ext, QStringLiteral(".") + chosen);
    u.setPath(path);
    return u.toString(QUrl::FullyEncoded);
}

void ViewIconsPlugin::open(const QString &url)
{
    if (!context || url.isEmpty())
        return;
    UiService *ui = context->ui();
    if (!ui)
        return;
    const QString format = context->settings().stringValue(QStringLiteral("format"));
    const QString size = context->settings().stringValue(QStringLiteral("imgSize"));
    ui->openImage(QUrl(ViewIcons::imageUrl(url, format, size)));
}

QList<MenuItem> ViewIconsPlugin::userContextMenuItems(const UserContext &user)
{
    if (!user.userId || user.avatarUrl.isEmpty())
        return {};
    QList<MenuItem> items { { viewAvatar, QStringLiteral("View Avatar") } };
    if (!user.memberAvatarUrl.isEmpty() && user.guildId)
        items.append({ viewServerAvatar, QStringLiteral("View Server Avatar") });
    return items;
}

void ViewIconsPlugin::onUserContextMenuAction(const QString &id, const UserContext &user)
{
    if (id == viewAvatar)
        open(user.avatarUrl);
    else if (id == viewServerAvatar)
        open(user.memberAvatarUrl);
}

QList<MenuItem> ViewIconsPlugin::channelContextMenuItems(const ChannelContext &channel)
{
    QList<MenuItem> items;
    if (channel.channelId == 0 && channel.guildId != 0) {
        // guild-context
        if (!channel.iconUrl.isEmpty())
            items.append({ viewIcon, QStringLiteral("View Icon") });
        if (!channel.bannerUrl.isEmpty())
            items.append({ viewBanner, QStringLiteral("View Banner") });
    } else if (channel.type == GroupDm && !channel.iconUrl.isEmpty()) {
        // gdm-context
        items.append({ viewGroupIcon, QStringLiteral("View Icon") });
    }
    return items;
}

void ViewIconsPlugin::onChannelContextMenuAction(const QString &id, const ChannelContext &channel)
{
    if (id == viewIcon || id == viewGroupIcon)
        open(channel.iconUrl);
    else if (id == viewBanner)
        open(channel.bannerUrl);
}

bool ViewIconsPlugin::onProfileImageClicked(const ProfileImageContext &image)
{
    if (image.url.isEmpty() || !context || !context->ui())
        return false;
    open(image.url);
    return true;
}

QByteArray qord_manifest_viewicons();

void qord_register_viewicons(Registry &registry)
{
    registry.add({ QStringLiteral("viewicons/plugin.json"), [] { return std::make_unique<ViewIconsPlugin>(); },
                   qord_manifest_viewicons() });
}

} // namespace Plugins
} // namespace Qord
