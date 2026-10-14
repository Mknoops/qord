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
    [[nodiscard]] Hooks hooks() const override { return Hook::MessageContextMenu; }

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
