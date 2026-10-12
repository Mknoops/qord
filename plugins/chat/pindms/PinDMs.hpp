/*
 * Qord, a native Discord client
 * Port of Equicord's PinDMs plugin (src/plugins/pinDms)
 * Original: Copyright (c) Vendicated and contributors, authors Ven, Aria
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "Plugins/Plugin.hpp"

#include <QJsonObject>

namespace Qord {
namespace Plugins {

class PluginContext;

// Categories of pinned DMs at the top of the DM list, per account.
class PinDMsPlugin : public Plugin
{
public:
    static constexpr quint32 DefaultColor = 10070709;
    static const QList<quint32> &swatches();

    struct Category
    {
        QString id;
        QString name;
        quint32 color = DefaultColor;
        bool collapsed = false;
        QList<Id> channels;
    };

    [[nodiscard]] Hooks hooks() const override { return Hook::DmPins | Hook::ChannelContextMenu; }
    void start(PluginContext &context) override;
    void stop() override;
    void onSettingChanged(const QString &key) override;

    std::optional<DmPinLayout> dmPinLayout(Id accountId) override;
    void onDmPinHeaderClicked(Id accountId, const QString &headerId) override;
    QList<MenuItem> dmPinHeaderMenu(Id accountId, const QString &headerId) override;
    void onDmPinHeaderMenuAction(Id accountId, const QString &headerId, const QString &itemId) override;
    QList<MenuItem> channelContextMenuItems(const ChannelContext &channel) override;
    void onChannelContextMenuAction(const QString &id, const ChannelContext &channel) override;

    [[nodiscard]] QList<Category> categories(Id accountId) const;

private:
    void save(Id accountId, const QList<Category> &list);
    void openCategoryModal(Id accountId, const QString &categoryId, Id channelId);
    [[nodiscard]] bool customOrder() const;

    PluginContext *context = nullptr;
};

} // namespace Plugins
} // namespace Qord
