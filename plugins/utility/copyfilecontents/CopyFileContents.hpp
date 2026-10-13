/*
 * Qord, a native Discord client
 * Port of Equicord's CopyFileContents plugin (src/plugins/copyFileContents)
 * Original: Copyright (c) Vendicated and contributors, authors Obsidian, Nuckyz
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "Plugins/Plugin.hpp"

#include <QSet>

#include <memory>

namespace Qord {
namespace Plugins {

class PluginContext;

// A copy button on text file attachments.
class CopyFileContentsPlugin : public Plugin
{
public:
    static constexpr int CopiedMs = 2000; // how long the check mark shows

    [[nodiscard]] Hooks hooks() const override { return Hook::AttachmentButtons; }
    void start(PluginContext &context) override;
    void stop() override;
    QList<ChatBarButton> attachmentButtons(const AttachmentContext &attachment) override;
    void onAttachmentButtonClicked(const QString &id, const AttachmentContext &attachment) override;

private:
    PluginContext *context = nullptr;
    QSet<Id> recentlyCopied;
    std::shared_ptr<bool> alive;
};

} // namespace Plugins
} // namespace Qord
