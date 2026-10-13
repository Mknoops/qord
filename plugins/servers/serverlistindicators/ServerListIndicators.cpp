/*
 * Qord, a native Discord client
 * Port of Equicord's ServerListIndicators plugin (src/plugins/serverListIndicators)
 * Original: Copyright (c) Vendicated and contributors, authors rini, Panniku
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "Plugins/Plugin.hpp"
#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"
#include "Plugins/Services.hpp"

namespace Qord {
namespace Plugins {

namespace {

// Upstream's icons
const QByteArray FriendIcon = QByteArrayLiteral(
        R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path fill="currentColor" d="M13 10a4 4 0 1 0 0-8 4 4 0 0 0 0 8Z"/><path fill="currentColor" d="M3 5v-.75C3 3.56 3.56 3 4.25 3s1.24.56 1.33 1.25C6.12 8.65 9.46 12 13 12h1a8 8 0 0 1 8 8 2 2 0 0 1-2 2 .21.21 0 0 1-.2-.15 7.65 7.65 0 0 0-1.32-2.3c-.15-.2-.42-.06-.39.17l.25 2c.02.15-.1.28-.25.28H9a2 2 0 0 1-2-2v-2.22c0-1.57-.67-3.05-1.53-4.37A15.85 15.85 0 0 1 3 5Z"/></svg>)");
const QByteArray ServerIcon = QByteArrayLiteral(
        R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path fill="currentColor" d="M19.73 4.87a18.2 18.2 0 0 0-4.6-1.44c-.21.4-.4.8-.58 1.21-1.69-.25-3.4-.25-5.1 0-.18-.41-.37-.82-.59-1.2-1.6.27-3.14.75-4.6 1.43A19.04 19.04 0 0 0 .96 17.7a18.43 18.43 0 0 0 5.63 2.87c.46-.62.86-1.28 1.2-1.98-.65-.25-1.29-.55-1.9-.92.17-.12.32-.24.47-.37 3.58 1.7 7.7 1.7 11.28 0l.46.37c-.6.36-1.25.67-1.9.92.35.7.75 1.35 1.2 1.98 2.03-.63 3.94-1.6 5.64-2.87.47-4.87-.78-9.09-3.3-12.83ZM8.3 15.12c-1.1 0-2-1.02-2-2.27 0-1.24.88-2.26 2-2.26s2.02 1.02 2 2.26c0 1.25-.89 2.27-2 2.27Zm7.4 0c-1.1 0-2-1.02-2-2.27 0-1.24.88-2.26 2-2.26s2.02 1.02 2 2.26c0 1.25-.88 2.27-2 2.27Z"/></svg>)");

// Mode, as upstream's flags
constexpr int Server = 1;
constexpr int Friend = 2;

// How many friends are online and how many servers you are in, above the
// server list
class ServerListIndicatorsPlugin : public Plugin
{
public:
    [[nodiscard]] Hooks hooks() const override { return Hook::ServerListButtons; }

    void start(PluginContext &ctx) override { context = &ctx; }
    void stop() override { context = nullptr; }

    QList<ServerListButton> serverListButtons(const ServerListContext &list) override
    {
        if (!context)
            return {};
        const int mode = context->settings().stringValue(QStringLiteral("mode")).toInt();
        const bool compact = context->settings().boolValue(QStringLiteral("useCompact"));
        const int friends = onlineFriends(list.accountId);
        QString tooltip;
        if (mode == (Server | Friend))
            tooltip = QStringLiteral("%1 Friends, %2 Servers").arg(friends).arg(list.serverCount);
        else if (mode == Server)
            tooltip = QStringLiteral("%1 Servers").arg(list.serverCount);
        else
            tooltip = QStringLiteral("%1 Friends").arg(friends);
        QList<ServerListButton> out;
        const auto add = [&](const QString &id, int count, const QString &word, const QByteArray &icon) {
            ServerListButton b;
            b.id = id;
            b.tooltip = tooltip;
            if (compact) {
                b.label = QStringLiteral("%1 %2").arg(count).arg(word);
            } else {
                b.label = QString::number(count);
                b.svg = icon;
            }
            out.append(b);
        };
        if (mode & Friend)
            add(QStringLiteral("friends"), friends, QStringLiteral("Friends"), FriendIcon);
        if (mode & Server)
            add(QStringLiteral("servers"), list.serverCount, QStringLiteral("Servers"), ServerIcon);
        return out;
    }

    void onSettingChanged(const QString &key) override
    {
        Q_UNUSED(key);
        if (context)
            context->refreshServerList();
    }

private:
    int onlineFriends(Id accountId) const
    {
        const RelationshipDirectory *relationships = context->relationships();
        const PresenceDirectory *presences = context->presences();
        if (!relationships || !presences)
            return 0;
        int count = 0;
        for (const Id id : relationships->relationshipIds(accountId, RelationshipDirectory::Kind::Friend))
            if (presences->status(accountId, id) != QLatin1String("offline"))
                ++count;
        return count;
    }

    PluginContext *context = nullptr;
};
} // namespace

QByteArray qord_manifest_serverlistindicators();

void qord_register_serverlistindicators(Registry &registry)
{
    registry.add({ QStringLiteral("serverlistindicators/plugin.json"),
                   [] { return std::make_unique<ServerListIndicatorsPlugin>(); },
                   qord_manifest_serverlistindicators() });
}

} // namespace Plugins
} // namespace Qord
