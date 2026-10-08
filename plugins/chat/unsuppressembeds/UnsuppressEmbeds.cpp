/*
 * Qord, a native Discord client
 * Port of Equicord's UnsuppressEmbeds plugin (src/plugins/unsuppressEmbeds)
 * Original: Copyright (c) Vendicated and contributors, authors rad, HypedDomi
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "Plugins/Plugin.hpp"
#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

namespace Qord {
namespace Plugins {

namespace {
constexpr quint64 EmbedSuppressed = 1ull << 2;
}

class UnsuppressEmbedsPlugin : public Plugin
{
public:
    [[nodiscard]] Hooks hooks() const override { return Hook::MessageContextMenu; }
    void start(PluginContext &ctx) override { context = &ctx; }

    QList<MenuItem> messageContextMenuItems(const MessageContext &message) override
    {
        const bool suppressed = (message.flags & EmbedSuppressed) != 0;
        if (!suppressed && !message.hasEmbeds)
            return {};
        // Your own message in a channel where you can't embed links
        if (message.isOwn && !message.canEmbedLinks)
            return {};
        return { { QStringLiteral("unsuppress-embeds"),
                   suppressed ? QStringLiteral("Unsuppress Embeds") : QStringLiteral("Suppress Embeds"),
                   {} } };
    }

    void onMessageContextMenuAction(const QString &id, const MessageContext &message) override
    {
        if (id != QLatin1String("unsuppress-embeds"))
            return;
        auto *actions = context->messageActions();
        if (!actions)
            return;
        const bool suppressed = (message.flags & EmbedSuppressed) != 0;
        actions->setMessageFlags(message.accountId, message.channelId, message.messageId,
                                 suppressed ? message.flags & ~EmbedSuppressed : message.flags | EmbedSuppressed);
    }

private:
    PluginContext *context = nullptr;
};

QByteArray qord_manifest_unsuppressembeds();

void qord_register_unsuppressembeds(Registry &registry)
{
    registry.add({ QStringLiteral("unsuppressembeds/plugin.json"),
                   [] { return std::make_unique<UnsuppressEmbedsPlugin>(); }, qord_manifest_unsuppressembeds() });
}

} // namespace Plugins
} // namespace Qord
