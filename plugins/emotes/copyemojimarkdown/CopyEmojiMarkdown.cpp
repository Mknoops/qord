/*
 * Qord, a native Discord client
 * Port of Equicord's CopyEmojiMarkdown plugin (src/plugins/copyEmojiMarkdown)
 * Original: Copyright (c) Vendicated and contributors, authors Happy enderman, Vishnya
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "Plugins/Plugin.hpp"
#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"
#include "Plugins/Services.hpp"

#include <QRegularExpression>

namespace Qord {
namespace Plugins {

// What upstream copies: <:name:id> (<a:...> when animated) for a custom
// emoji, and the character itself or :name: for a Unicode one
QString emojiMarkdown(const EmojiInfo &emoji, bool copyUnicode)
{
    if (!emoji.customId)
        return copyUnicode && !emoji.surrogates.isEmpty() ? emoji.surrogates
                                                          : QStringLiteral(":%1:").arg(emoji.name);
    // Duplicate names show as name~1; the markdown uses the real name
    static const QRegularExpression suffix(QStringLiteral("~\\d+$"));
    QString name = emoji.name;
    name.remove(suffix);
    return QStringLiteral("<%1:%2:%3>").arg(emoji.animated ? QStringLiteral("a") : QString(), name,
                                            QString::number(emoji.customId));
}

namespace {

// "Copy Emoji Markdown" in the emoji picker's right-click menu
class CopyEmojiMarkdownPlugin : public Plugin
{
public:
    [[nodiscard]] Hooks hooks() const override { return Hook::EmojiSuggestions; }

    void start(PluginContext &ctx) override { context = &ctx; }
    void stop() override { context = nullptr; }

    QList<MenuItem> emojiContextMenuItems(const EmojiContext &emoji) override
    {
        Q_UNUSED(emoji);
        return { { QStringLiteral("vc-copy-emoji-markdown"), QStringLiteral("Copy Emoji Markdown") } };
    }

    void onEmojiContextMenuAction(const QString &id, const EmojiContext &emoji) override
    {
        if (!context || id != QLatin1String("vc-copy-emoji-markdown"))
            return;
        ClipboardService *clipboard = context->clipboard();
        if (!clipboard)
            return;
        clipboard->setText(emojiMarkdown(emoji.emoji, context->settings().boolValue(QStringLiteral("copyUnicode"))));
        if (UiService *ui = context->ui())
            ui->showToast(QStringLiteral("Success! Copied emoji markdown."), UiService::ToastType::Success);
    }

private:
    PluginContext *context = nullptr;
};
} // namespace

QByteArray qord_manifest_copyemojimarkdown();

void qord_register_copyemojimarkdown(Registry &registry)
{
    registry.add({ QStringLiteral("copyemojimarkdown/plugin.json"),
                   [] { return std::make_unique<CopyEmojiMarkdownPlugin>(); }, qord_manifest_copyemojimarkdown() });
}

} // namespace Plugins
} // namespace Qord
