/*
 * Qord, a native Discord client
 * Port of Equicord's CopyFileContents plugin (src/plugins/copyFileContents)
 * Original: Copyright (c) Vendicated and contributors, authors Obsidian, Nuckyz
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "CopyFileContents.hpp"

#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

#include <QTimer>

namespace Qord {
namespace Plugins {

namespace {
const QString ButtonId = QStringLiteral("cfc-copy");

// Vencord's CopyIcon, NoEntrySignIcon and the plugin's check mark
const QByteArray CopySvg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"18\" height=\"18\" viewBox=\"0 0 24 24\" fill=\"currentColor\">"
        "<path d=\"M3 16a1 1 0 0 1-1-1v-5a8 8 0 0 1 8-8h5a1 1 0 0 1 1 1v.5a.5.5 0 0 1-.5.5H10a6 6 0 0 0-6 6v5.5a.5.5 0 0 "
        "1-.5.5H3Z\"/><path d=\"M6 18a4 4 0 0 0 4 4h8a4 4 0 0 0 4-4v-4h-3a5 5 0 0 1-5-5V6h-4a4 4 0 0 0-4 4v8Z\"/>"
        "<path d=\"M21.73 12a3 3 0 0 0-.6-.88l-4.25-4.24a3 3 0 0 0-.88-.61V9a3 3 0 0 0 3 3h2.73Z\"/></svg>";
const QByteArray NoEntrySvg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"18\" height=\"18\" viewBox=\"0 0 24 24\">"
        "<path fill=\"currentColor\" d=\"M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm0 18c-4.42 "
        "0-8-3.58-8-8 0-1.85.63-3.55 1.69-4.9L16.9 18.31C15.55 19.37 13.85 20 12 20zm6.31-3.1L7.1 5.69C8.45 4.63 10.15 4 "
        "12 4c4.42 0 8 3.58 8 8 0 1.85-.63 3.55-1.69 4.9z\"/></svg>";
const QByteArray CheckSvg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"18\" height=\"18\" viewBox=\"0 0 24 24\">"
        "<path fill=\"currentColor\" d=\"M21.7 5.3a1 1 0 0 1 0 1.4l-12 12a1 1 0 0 1-1.4 0l-6-6a1 1 0 1 1 1.4-1.4L9 "
        "16.58l11.3-11.3a1 1 0 0 1 1.4 0Z\"/></svg>";
} // namespace

void CopyFileContentsPlugin::start(PluginContext &context)
{
    this->context = &context;
    alive = std::make_shared<bool>(true);
}

void CopyFileContentsPlugin::stop()
{
    if (alive)
        *alive = false;
    alive.reset();
    recentlyCopied.clear();
    context = nullptr;
}

QList<ChatBarButton> CopyFileContentsPlugin::attachmentButtons(const AttachmentContext &attachment)
{
    if (recentlyCopied.contains(attachment.attachmentId))
        return { { ButtonId, QStringLiteral("Copied!"), CheckSvg, false } };
    if (attachment.bytesLeft > 0)
        return { { ButtonId, QStringLiteral("File too large to copy"), NoEntrySvg, false } };
    return { { ButtonId, QStringLiteral("Copy File Contents"), CopySvg, false } };
}

void CopyFileContentsPlugin::onAttachmentButtonClicked(const QString &id, const AttachmentContext &attachment)
{
    if (id != ButtonId || !context || recentlyCopied.contains(attachment.attachmentId) || attachment.bytesLeft > 0)
        return;
    ClipboardService *clipboard = context->clipboard();
    if (!clipboard)
        return;
    clipboard->setText(attachment.text);
    UiService *ui = context->ui();
    if (ui)
        ui->showToast(QStringLiteral("Copied to clipboard!"), UiService::ToastType::Success);

    recentlyCopied.insert(attachment.attachmentId);
    if (ui)
        ui->refreshAttachments(attachment.accountId, attachment.channelId, attachment.messageId);
    std::weak_ptr<bool> weak = alive;
    QTimer::singleShot(CopiedMs, [this, weak, attachment]() {
        if (auto a = weak.lock(); !a || !*a)
            return;
        recentlyCopied.remove(attachment.attachmentId);
        if (UiService *ui = context ? context->ui() : nullptr)
            ui->refreshAttachments(attachment.accountId, attachment.channelId, attachment.messageId);
    });
}

QByteArray qord_manifest_copyfilecontents();

void qord_register_copyfilecontents(Registry &registry)
{
    registry.add({ QStringLiteral("copyfilecontents/plugin.json"),
                   [] { return std::make_unique<CopyFileContentsPlugin>(); }, qord_manifest_copyfilecontents() });
}

} // namespace Plugins
} // namespace Qord
