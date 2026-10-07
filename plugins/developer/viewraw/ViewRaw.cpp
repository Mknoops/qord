/*
 * Qord, a native Discord client
 * Port of Equicord's ViewRaw plugin (src/plugins/viewRaw)
 * Original: Copyright (c) Vendicated and contributors, authors KingFish, Ven, rad, ImLvna
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ViewRaw.hpp"

#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

namespace Qord {
namespace Plugins {

namespace {
const QString ViewItem = QStringLiteral("vc-view-message-raw");
const QString CopyItem = QStringLiteral("vc-copy-message-raw");
} // namespace

QList<MenuItem> ViewRawPlugin::messageContextMenuItems(const MessageContext &message)
{
    Q_UNUSED(message);
    return { { ViewItem, QStringLiteral("View Raw"), {} }, { CopyItem, QStringLiteral("Copy Raw Content"), {} } };
}

void ViewRawPlugin::onMessageContextMenuAction(const QString &id, const MessageContext &message)
{
    if (id == CopyItem) {
        if (ClipboardService *clipboard = context->clipboard())
            clipboard->setText(message.content);
        return;
    }
    if (id != ViewItem)
        return;
    UiService *ui = context->ui();
    if (!ui)
        return;
    QList<UiService::TextSection> sections;
    if (!message.content.isEmpty())
        sections.append({ QStringLiteral("Message Content"), message.content, true });
    sections.append({ QStringLiteral("Message Data"), message.rawJson, true });
    ui->showTextDialog(QStringLiteral("Raw Message Data"), sections);
}

QByteArray qord_manifest_viewraw();

void qord_register_viewraw(Registry &registry)
{
    registry.add({ QStringLiteral("viewraw/plugin.json"), [] { return std::make_unique<ViewRawPlugin>(); },
                   qord_manifest_viewraw() });
}

} // namespace Plugins
} // namespace Qord
