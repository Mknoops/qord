/*
 * Qord, a native Discord client
 * Port of Equicord's CopyUserMention plugin (src/equicordplugins/copyUserMention)
 * Original: Copyright (c) Vendicated and contributors, authors Cortex, castdrian
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "CopyUserMention.hpp"

#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

namespace Qord {
namespace Plugins {

QString CopyUserMentionPlugin::textFor(Id userId)
{
    return QStringLiteral("<@%1>").arg(userId);
}

QList<MenuItem> CopyUserMentionPlugin::userContextMenuItems(const UserContext &user)
{
    if (!user.userId)
        return {};
    return { { QStringLiteral("vc-copy-user-mention"), QStringLiteral("Copy User Mention") } };
}

void CopyUserMentionPlugin::onUserContextMenuAction(const QString &id, const UserContext &user)
{
    if (id != QLatin1String("vc-copy-user-mention") || !user.userId)
        return;
    if (ClipboardService *clipboard = context->clipboard())
        clipboard->setText(textFor(user.userId));
}

QByteArray qord_manifest_copyusermention();

void qord_register_copyusermention(Registry &registry)
{
    registry.add({ QStringLiteral("copyusermention/plugin.json"),
                   [] { return std::make_unique<CopyUserMentionPlugin>(); }, qord_manifest_copyusermention() });
}

} // namespace Plugins
} // namespace Qord
