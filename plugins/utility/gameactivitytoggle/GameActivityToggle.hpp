/*
 * Qord, a native Discord client
 * Port of Equicord's GameActivityToggle plugin (src/plugins/gameActivityToggle)
 * Original: Copyright (c) Vendicated and contributors, authors Nuckyz, RuukuLada
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "Plugins/Plugin.hpp"

#include <QHash>

#include <memory>

namespace Qord {
namespace Plugins {

class PluginContext;

// A button next to mute and deafen that switches whether Discord shares
// your game activity; right-click it to switch Spotify's sharing.
class GameActivityTogglePlugin : public Plugin
{
public:
    static QByteArray iconSvg(bool showCurrentGame, bool oldIcon);

    [[nodiscard]] Hooks hooks() const override { return Hook::UserPanelButtons; }
    void start(PluginContext &context) override;
    void stop() override;
    void onSettingChanged(const QString &key) override;

    QList<UserPanelButton> userPanelButtons(const UserPanelContext &panel) override;
    void onUserPanelButtonClicked(const QString &id, const UserPanelContext &panel) override;
    QList<MenuItem> userPanelButtonMenu(const QString &id, const UserPanelContext &panel) override;
    void onUserPanelButtonMenuAction(const QString &id, const QString &itemId, const UserPanelContext &panel) override;

private:
    void fetchSpotify(Id accountId);

    PluginContext *context = nullptr;
    QHash<Id, QList<ActivityConnection>> spotify; // by account
    // The change takes a moment to come back, so the menu shows it at once
    QHash<QString, bool> overrides; // by connection id
    std::shared_ptr<bool> alive;
};

} // namespace Plugins
} // namespace Qord
