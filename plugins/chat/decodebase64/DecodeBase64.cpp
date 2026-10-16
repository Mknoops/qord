/*
 * Qord, a native Discord client
 * Port of Equicord's DecodeBase64 plugin (src/equicordplugins/baseDecoder)
 * Original: Copyright (c) Vendicated and contributors, author ThePirateStoner
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "Plugins/Plugin.hpp"
#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"
#include "Plugins/Services.hpp"

#include <QRegularExpression>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QStringDecoder>
#else
#include <QTextCodec>
#endif

namespace Qord {
namespace Plugins {

// Upstream's candidates: runs of 4+ base64 characters with optional padding,
// kept when they decode to valid UTF-8 text
QStringList decodeBase64Strings(const QString &content)
{
    static const QRegularExpression candidate(
            QStringLiteral("\\b[A-Za-z0-9+/]{4,}(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?\\b"));
    QStringList out;
    auto it = candidate.globalMatch(content);
    while (it.hasNext()) {
        const QString text = it.next().captured(0);
        // atob refuses a length that leaves one character over
        if (QString(text).remove(QLatin1Char('=')).size() % 4 == 1)
            continue;
        const auto result = QByteArray::fromBase64Encoding(text.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        if (!result || result.decoded.isEmpty())
            continue;
        
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QStringDecoder utf8(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    const QString decoded = utf8.decode(result.decoded);
    if (!utf8.hasError())
        out.append(decoded);
#else
    QTextCodec::ConverterState state;
    const QString decoded =
        QTextCodec::codecForName("UTF-8")->toUnicode(result.decoded.constData(), result.decoded.size(), &state);
    if (state.invalidChars == 0)
        out.append(decoded);
#endif
        
    }
    return out;
}

namespace {

// "Decode Base64" and "Copy Decoded" for any message
class DecodeBase64Plugin : public Plugin
{
public:
    [[nodiscard]] Hooks hooks() const override { return Hook::MessageContextMenu | Hook::MessagePopover; }

    void start(PluginContext &ctx) override { context = &ctx; }
    void stop() override { context = nullptr; }

    QList<MenuItem> messageContextMenuItems(const MessageContext &message) override
    {
        if (!context || message.content.isEmpty())
            return {};
        const MenuItem decode{ QStringLiteral("decode"), QStringLiteral("Decode Base64") };
        const MenuItem copy{ QStringLiteral("copy"), QStringLiteral("Copy Decoded") };
        // Upstream's click method: which action is the main one
        if (context->settings().stringValue(QStringLiteral("clickMethod")) == QLatin1String("Right"))
            return { copy, decode };
        return { decode, copy };
    }

    // Upstream's button, on every message: one click decodes, the other copies
    QList<ChatBarButton> messagePopoverButtons(const MessageContext &message) override
    {
        Q_UNUSED(message);
        if (!context)
            return {};
        ChatBarButton button;
        button.id = QStringLiteral("decode");
        button.tooltip = rightDecodes() ? QStringLiteral("Copy Decoded (Left Click) / Decode Base64 (Right Click)")
                                        : QStringLiteral("Decode Base64 (Left Click) / Copy Decoded (Right Click)");
        button.svg = QByteArrayLiteral(
                R"(<svg width="24" height="24" viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">)"
                R"(<path d="M6.5 9.50026H14.0385C15.4063 9.50026 16.0902 9.50026 16.5859 9.82073C16.8235 9.97438 17.0259 )"
                R"(10.1767 17.1795 10.4144C17.5 10.91 17.5 11.5939 17.5 12.9618C17.5 14.3297 17.5 15.0136 17.1795 )"
                R"(15.5092C17.0259 15.7469 16.8235 15.9492 16.5859 16.1029C16.0902 16.4233 15.4063 16.4233 14.0385 )"
                R"(16.4233H9.5M6.5 9.50026L8.75 7.42334M6.5 9.50026L8.75 11.5772" stroke="currentColor" )"
                R"(stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" fill="none"/></svg>)");
        return { button };
    }

    void onMessagePopoverClicked(const QString &id, const MessageContext &message, bool rightClick) override
    {
        Q_UNUSED(id);
        const bool decode = rightClick == rightDecodes();
        onMessageContextMenuAction(decode ? QStringLiteral("decode") : QStringLiteral("copy"), message);
    }

    void onMessageContextMenuAction(const QString &id, const MessageContext &message) override
    {
        UiService *ui = context ? context->ui() : nullptr;
        if (!ui)
            return;
        const QStringList decoded = decodeBase64Strings(message.content);
        if (decoded.isEmpty()) {
            ui->showToast(QStringLiteral("No Base64 content found in this message."), UiService::ToastType::Failure);
            return;
        }
        if (id == QLatin1String("decode")) {
            QList<UiService::TextSection> sections;
            for (int i = 0; i < decoded.size(); ++i)
                sections.append({ QStringLiteral("Decoded Content %1").arg(i + 1), decoded[i], true,
                                  QStringLiteral("Decoded content copied to clipboard!") });
            ui->showTextDialog(QStringLiteral("Decoded Base64 Content"), sections);
        } else if (id == QLatin1String("copy")) {
            ClipboardService *clipboard = context->clipboard();
            if (!clipboard)
                return;
            // Upstream copies each in turn, leaving the last; all of them is more useful
            clipboard->setText(decoded.join(QLatin1Char('\n')));
            ui->showToast(QStringLiteral("Copied to clipboard!"), UiService::ToastType::Success);
        }
    }

private:
    [[nodiscard]] bool rightDecodes() const
    {
        return context && context->settings().stringValue(QStringLiteral("clickMethod")) == QLatin1String("Right");
    }

    PluginContext *context = nullptr;
};
} // namespace

QByteArray qord_manifest_decodebase64();

void qord_register_decodebase64(Registry &registry)
{
    registry.add({ QStringLiteral("decodebase64/plugin.json"), [] { return std::make_unique<DecodeBase64Plugin>(); },
                   qord_manifest_decodebase64() });
}

} // namespace Plugins
} // namespace Qord
