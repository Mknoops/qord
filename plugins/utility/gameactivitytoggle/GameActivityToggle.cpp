/*
 * Qord, a native Discord client
 * Port of Equicord's GameActivityToggle plugin (src/plugins/gameActivityToggle)
 * Original: Copyright (c) Vendicated and contributors, authors Nuckyz, RuukuLada
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "GameActivityToggle.hpp"

#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

namespace Qord {
namespace Plugins {

namespace {
const QString ButtonId = QStringLiteral("game-activity-toggle");
const QString SpotifyItemPrefix = QStringLiteral("vc-toggle-spotify-");
// --status-danger
const char Danger[] = "#f23f43";

// Paths copied from upstream's Icon
const char controllerPath[] =
        "M3.06 20.4q-1.53 0-2.37-1.065T.06 16.74l1.26-9q.27-1.8 1.605-2.97T6.06 3.6h11.88q1.8 0 3.135 1.17t1.605 2.97l1.26 "
        "9q.21 1.53-.63 2.595T20.94 20.4q-.63 0-1.17-.225T18.78 19.5l-2.7-2.7H7.92l-2.7 2.7q-.45.45-.99.675t-1.17.225Zm14.94-"
        "7.2q.51 0 .855-.345T19.2 12q0-.51-.345-.855T18 10.8q-.51 0-.855.345T16.8 12q0 .51.345 .855T18 13.2Zm-2.4-3.6q.51 0 "
        ".855-.345T16.8 8.4q0-.51-.345-.855T15.6 7.2q-.51 0-.855.345T14.4 8.4q0 .51.345 .855T15.6 9.6ZM6.9 13.2h1.8v-2.1h2.1v-"
        "1.8h-2.1v-2.1h-1.8v2.1h-2.1v1.8h2.1v2.1Z";
const char redLinePath[] = "M22.7 2.7a1 1 0 0 0-1.4-1.4l-20 20a1 1 0 1 0 1.4 1.4Z";
const char oldRedLinePath[] = "M23 2.27 21.73 1 1 21.73 2.27 23 23 2.27Z";
} // namespace

QByteArray GameActivityTogglePlugin::iconSvg(bool showCurrentGame, bool oldIcon)
{
    // The new icon turns the controller red while activity is hidden
    const QString fill = !showCurrentGame && !oldIcon ? QString::fromLatin1(Danger) : QStringLiteral("currentColor");
    QString svg = QStringLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"20\" "
                                 "viewBox=\"0 0 24 24\"><path fill=\"%1\" d=\"%2\"/>")
                          .arg(fill, QString::fromLatin1(controllerPath));
    if (!showCurrentGame)
        svg += QStringLiteral("<path fill=\"%1\" d=\"%2\"/>")
                       .arg(QString::fromLatin1(Danger), QString::fromLatin1(oldIcon ? oldRedLinePath : redLinePath));
    svg += QStringLiteral("</svg>");
    return svg.toUtf8();
}

void GameActivityTogglePlugin::start(PluginContext &ctx)
{
    context = &ctx;
    alive = std::make_shared<bool>(true);
}

void GameActivityTogglePlugin::stop()
{
    alive.reset();
    spotify.clear();
    overrides.clear();
    context = nullptr;
}

void GameActivityTogglePlugin::onSettingChanged(const QString &key)
{
    Q_UNUSED(key);
    if (context)
        context->refreshUserPanel();
}

void GameActivityTogglePlugin::fetchSpotify(Id accountId)
{
    ActivitySettingsService *activity = context ? context->activity() : nullptr;
    if (!activity)
        return;
    std::weak_ptr<bool> weak = alive;
    activity->fetchConnections(accountId, [this, weak, accountId](bool ok, const QList<ActivityConnection> &list) {
        if (weak.expired() || !ok)
            return;
        // ConnectedAccountsStore's Spotify accounts that still work
        QList<ActivityConnection> accounts;
        for (const auto &c : list)
            if (c.type == QLatin1String("spotify") && !c.revoked)
                accounts.append(c);
        spotify.insert(accountId, accounts);
    });
}

QList<UserPanelButton> GameActivityTogglePlugin::userPanelButtons(const UserPanelContext &panel)
{
    ActivitySettingsService *activity = context ? context->activity() : nullptr;
    if (!activity)
        return {};
    if (!spotify.contains(panel.accountId))
        fetchSpotify(panel.accountId);
    const bool show = activity->showCurrentGame(panel.accountId);
    UserPanelButton button;
    button.id = ButtonId;
    button.tooltip = show ? QStringLiteral("Disable Game Activity") : QStringLiteral("Enable Game Activity");
    button.svg = iconSvg(show, context->settings().boolValue(QStringLiteral("oldIcon")));
    button.danger = !show;
    return { button };
}

void GameActivityTogglePlugin::onUserPanelButtonClicked(const QString &id, const UserPanelContext &panel)
{
    ActivitySettingsService *activity = context ? context->activity() : nullptr;
    if (id != ButtonId || !activity)
        return;
    activity->setShowCurrentGame(panel.accountId, !activity->showCurrentGame(panel.accountId));
    context->refreshUserPanel();
}

QList<MenuItem> GameActivityTogglePlugin::userPanelButtonMenu(const QString &id, const UserPanelContext &panel)
{
    if (id != ButtonId)
        return {};
    const QList<ActivityConnection> accounts = spotify.value(panel.accountId);
    QList<MenuItem> items;
    for (const auto &account : accounts) {
        MenuItem item;
        item.id = SpotifyItemPrefix + account.id;
        item.label = accounts.size() == 1 ? QStringLiteral("Share Spotify Activity")
                                          : QStringLiteral("Share Spotify Activity (%1)").arg(account.name);
        item.checked = overrides.value(account.id, account.showActivity);
        items.append(item);
    }
    // Pick up accounts connected since
    fetchSpotify(panel.accountId);
    return items;
}

void GameActivityTogglePlugin::onUserPanelButtonMenuAction(const QString &id, const QString &itemId,
                                                           const UserPanelContext &panel)
{
    ActivitySettingsService *activity = context ? context->activity() : nullptr;
    if (id != ButtonId || !activity || !itemId.startsWith(SpotifyItemPrefix))
        return;
    const QString accountId = itemId.mid(SpotifyItemPrefix.size());
    for (const auto &account : spotify.value(panel.accountId)) {
        if (account.id != accountId)
            continue;
        const bool checked = overrides.value(account.id, account.showActivity);
        activity->setConnectionShowActivity(panel.accountId, account.type, account.id, !checked, {});
        overrides.insert(account.id, !checked);
        return;
    }
}

QByteArray qord_manifest_gameactivitytoggle();

void qord_register_gameactivitytoggle(Registry &registry)
{
    registry.add({ QStringLiteral("gameactivitytoggle/plugin.json"),
                   [] { return std::make_unique<GameActivityTogglePlugin>(); }, qord_manifest_gameactivitytoggle() });
}

} // namespace Plugins
} // namespace Qord
