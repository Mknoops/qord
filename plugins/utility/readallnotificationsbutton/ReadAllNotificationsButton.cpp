/*
 * Qord, a native Discord client
 * Port of Equicord's ReadAllNotificationsButton plugin (src/plugins/readAllNotificationsButton)
 * Original: Copyright (c) Vendicated and contributors, authors kemo, KrystalSkull
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "Plugins/Plugin.hpp"
#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

namespace Qord {
namespace Plugins {

class ReadAllNotificationsButtonPlugin : public Plugin
{
public:
    [[nodiscard]] Hooks hooks() const override { return Hook::ServerListButtons; }
    void start(PluginContext &ctx) override { context = &ctx; }
    void stop() override { context = nullptr; }

    // addServerListElement(ServerListRenderPosition.Above, <TextButton>Read All</TextButton>)
    QList<ServerListButton> serverListButtons(const ServerListContext &list) override
    {
        if (!list.accountId)
            return {};
        return { { QStringLiteral("read-all"), QStringLiteral("Read All"), QString() } };
    }

    // Upstream BULK_ACKs every unread SELECTABLE and VOCAL channel and active
    // joined thread of every guild
    void onServerListButtonClicked(const QString &id, const ServerListContext &list) override
    {
        if (id != QLatin1String("read-all") || !context)
            return;
        if (ReadStateService *readStates = context->readStates())
            readStates->markAllServersRead(list.accountId);
    }

private:
    PluginContext *context = nullptr;
};

QByteArray qord_manifest_readallnotificationsbutton();

void qord_register_readallnotificationsbutton(Registry &registry)
{
    registry.add({ QStringLiteral("readallnotificationsbutton/plugin.json"),
                   [] { return std::make_unique<ReadAllNotificationsButtonPlugin>(); },
                   qord_manifest_readallnotificationsbutton() });
}

} // namespace Plugins
} // namespace Qord
