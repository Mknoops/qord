/*
 * Qord, a native Discord client
 * Port of Equicord's CopyUserURLs plugin (src/plugins/copyUserURLs)
 * Original: Copyright (c) Vendicated and contributors, author castdrian
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "CopyUserUrls.hpp"

#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

namespace Qord {
namespace Plugins {

QString CopyUserUrlsPlugin::textFor(Id userId)
{
    return QStringLiteral("<https://discord.com/users/%1>").arg(userId);
}

QList<MenuItem> CopyUserUrlsPlugin::userContextMenuItems(const UserContext &user)
{
    if (!user.userId)
        return {};
    return { { QStringLiteral("vc-copy-user-url"), QStringLiteral("Copy User URL") } };
}

void CopyUserUrlsPlugin::onUserContextMenuAction(const QString &id, const UserContext &user)
{
    if (id != QLatin1String("vc-copy-user-url") || !user.userId)
        return;
    if (ClipboardService *clipboard = context->clipboard())
        clipboard->setText(textFor(user.userId));
}

QByteArray qord_manifest_copyuserurls();

void qord_register_copyuserurls(Registry &registry)
{
    registry.add({ QStringLiteral("copyuserurls/plugin.json"),
                   [] { return std::make_unique<CopyUserUrlsPlugin>(); }, qord_manifest_copyuserurls() });
}

} // namespace Plugins
} // namespace Qord
