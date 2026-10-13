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

// An eye (Material Icons' "visibility", Apache-2.0), standing in for Discord's
const QByteArray EyeIcon = QByteArrayLiteral(
        R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path fill="currentColor" d="M12 4.5C7 4.5 2.73 7.61 1 12c1.73 4.39 6 7.5 11 7.5s9.27-3.11 11-7.5c-1.73-4.39-6-7.5-11-7.5zM12 17c-2.76 0-5-2.24-5-5s2.24-5 5-5 5 2.24 5 5-2.24 5-5 5zm0-8c-1.66 0-3 1.34-3 3s1.34 3 3 3 3-1.34 3-3-1.34-3-3-3z"/></svg>)");

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
        if (id != QLatin1String("preview") || !context || chat.draft.isEmpty())
            return;
        if (UiService *ui = context->ui())
            ui->showLocalMessage(chat.accountId, chat.channelId, chat.draft);
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
