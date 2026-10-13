/*
 * Qord, a native Discord client
 * Port of Equicord's MutualGroupDMs plugin (src/plugins/mutualGroupDMs)
 * Original: Copyright (c) Vendicated and contributors, author amia
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "Plugins/Plugin.hpp"
#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

namespace Qord {
namespace Plugins {

namespace {
const QString TabId = QStringLiteral("MUTUAL_GDMS");
} // namespace

// A "Mutual Groups" tab in profiles: the group DMs you share with the user.
class MutualGroupDMsPlugin : public Plugin
{
public:
    [[nodiscard]] Hooks hooks() const override { return Hook::ProfileTabs; }
    void start(PluginContext &ctx) override { context = &ctx; }
    void stop() override { context = nullptr; }

    QList<ProfileTab> profileTabs(const UserContext &user) override
    {
        const PrivateChannelDirectory *dms = context ? context->privateChannels() : nullptr;
        // Not for bots or yourself
        if (!dms || user.isBot || user.userId == user.accountId)
            return {};
        ProfileTab tab;
        tab.id = TabId;
        tab.emptyText = QStringLiteral("You don't have any group chats in common");
        for (const auto &channel : dms->privateChannels(user.accountId)) {
            if (!channel.isGroup || !channel.recipientIds.contains(user.userId))
                continue;
            ProfileTab::Row row;
            row.id = QString::number(channel.channelId);
            row.title = channel.name;
            row.subtitle = QStringLiteral("%1 Members").arg(channel.recipientIds.size() + 1);
            row.iconUrl = channel.iconUrl;
            row.iconText = channel.name.left(1).toUpper();
            tab.rows.append(row);
        }
        const auto count = tab.rows.size();
        tab.title = QStringLiteral("%1 Mutual Group%2")
                            .arg(count == 0 ? QStringLiteral("No") : QString::number(count),
                                 count != 1 ? QStringLiteral("s") : QString());
        return { tab };
    }

    void onProfileTabRowClicked(const QString &tabId, const QString &rowId, const UserContext &user) override
    {
        UiService *ui = context ? context->ui() : nullptr;
        if (tabId == TabId && ui)
            ui->openChannel(user.accountId, rowId.toULongLong());
    }

private:
    PluginContext *context = nullptr;
};

QByteArray qord_manifest_mutualgroupdms();

void qord_register_mutualgroupdms(Registry &registry)
{
    registry.add({ QStringLiteral("mutualgroupdms/plugin.json"), [] { return std::make_unique<MutualGroupDMsPlugin>(); },
                   qord_manifest_mutualgroupdms() });
}

} // namespace Plugins
} // namespace Qord
