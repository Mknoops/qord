/*
 * Qord, a native Discord client
 * Port of Equicord's PreviewMessage plugin (src/plugins/previewMessage)
 * Original: Copyright (c) Vendicated and contributors, author Syncxv
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "Plugins/Plugin.hpp"
#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"
#include "Plugins/Services.hpp"

namespace Qord {
namespace Plugins {

namespace {

// Vencord's EyeIcon (Discord's eye)
const QByteArray EyeIcon = QByteArrayLiteral(
        R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24">)"
        R"(<path fill="currentColor" d="M15.56 11.77c.2-.1.44.02.44.23a4 4 0 1 1-4-4c.21 0 .33.25.23.44a2.5 2.5 0 0 0 3.32 3.32Z"/>)"
        R"(<path fill="currentColor" fill-rule="evenodd" clip-rule="evenodd" d="M22.89 11.7c.07.2.07.4 0 .6C22.27 13.9 19.1 21 12 21c-7.11 0-10.27-7.11-10.89-8.7a.83.83 0 0 1 0-.6C1.73 10.1 4.9 3 12 3c7.11 0 10.27 7.11 10.89 8.7Zm-4.5-3.62A15.11 15.11 0 0 1 20.85 12c-.38.88-1.18 2.47-2.46 3.92C16.87 17.62 14.8 19 12 19c-2.8 0-4.87-1.38-6.39-3.08A15.11 15.11 0 0 1 3.15 12c.38-.88 1.18-2.47 2.46-3.92C7.13 6.38 9.2 5 12 5c2.8 0 4.87 1.38 6.39 3.08Z"/>)"
        R"(</svg>)");

// A "Preview Message" button by the message box: shows what you typed as a
// message only you can see, before sending it
class PreviewMessagePlugin : public Plugin
{
public:
    [[nodiscard]] Hooks hooks() const override { return Hook::ChatBarButtons; }

    void start(PluginContext &ctx) override { context = &ctx; }
    void stop() override { context = nullptr; }

    QList<ChatBarButton> chatBarButtons(const ChatContext &chat) override
    {
        // Only while there is something to preview
        if (!chat.channelId || (chat.draft.isEmpty() && !chat.hasAttachments))
            return {};
        ChatBarButton button;
        button.id = QStringLiteral("preview");
        button.tooltip = QStringLiteral("Preview Message");
        button.svg = EyeIcon;
        return { button };
    }

    void onChatBarButtonClicked(const QString &id, const ChatContext &chat) override
    {
        if (id != QLatin1String("preview") || !context || (chat.draft.isEmpty() && !chat.hasAttachments))
            return;
        if (UiService *ui = context->ui())
            ui->previewDraft(chat.accountId, chat.channelId, chat.draft);
    }

private:
    PluginContext *context = nullptr;
};
} // namespace

QByteArray qord_manifest_previewmessage();

void qord_register_previewmessage(Registry &registry)
{
    registry.add({ QStringLiteral("previewmessage/plugin.json"), [] { return std::make_unique<PreviewMessagePlugin>(); },
                   qord_manifest_previewmessage() });
}

} // namespace Plugins
} // namespace Qord
