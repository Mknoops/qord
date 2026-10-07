/*
 * Qord, a native Discord client
 * Port of Equicord's SilentMessageToggle plugin (src/plugins/silentMessageToggle)
 * Original: Copyright (c) Vendicated and contributors, authors Nuckyz, CatNoir, Z1xus
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "SilentMessageToggle.hpp"

#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

namespace Qord {
namespace Plugins {

namespace {
const QString Prefix = QStringLiteral("@silent ");

// Upstream's bell icon; struck through in red when off
QByteArray icon(bool on)
{
    QByteArray svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"20\" viewBox=\"0 0 24 24\">";
    if (!on)
        svg += "<mask id=\"m\"><path fill=\"#fff\" d=\"M0 0h24v24H0Z\"/><path stroke=\"#000\" stroke-width=\"5.99068\" d=\"M0 24 24 0\"/></mask>";
    svg += QByteArray("<path fill=\"currentColor\"") + (on ? "" : " mask=\"url(#m)\"") +
           " d=\"M18 10.7101C15.1085 9.84957 13 7.17102 13 4c0-.30736.0198-.6101.0582-.907C12.7147 3.03189 12.3611 3 12 3 8.686 3 6 5.686 6 9v5c0 1.657-1.344 3-3 3v1h18v-1c-1.656 0-3-1.343-3-3v-3.2899ZM8.55493 19c.693 1.19 1.96897 2 3.44497 2s2.752-.81 3.445-2H8.55493ZM18.2624 5.50209 21 2.5V1h-4.9651v1.49791h2.4411L16 5.61088V7h5V5.50209h-2.7376Z\"/>";
    if (!on)
        svg += "<path fill=\"#f23f43\" d=\"m21.178 1.70703 1.414 1.414L4.12103 21.593l-1.414-1.415L21.178 1.70703Z\"/>";
    svg += "</svg>";
    return svg;
}

SilentMessageTogglePlugin::Persist persistFromString(const QString &s)
{
    if (s == QLatin1String("channels"))
        return SilentMessageTogglePlugin::Persist::Channels;
    if (s == QLatin1String("restarts"))
        return SilentMessageTogglePlugin::Persist::Restarts;
    return SilentMessageTogglePlugin::Persist::None;
}
} // namespace

void SilentMessageTogglePlugin::start(PluginContext &ctx)
{
    context = &ctx;
    persist = persistFromString(ctx.settings().stringValue(QStringLiteral("persistState")));
    enabled = persist == Persist::Restarts && ctx.settings().boolValue(QStringLiteral("lastState"));
}

void SilentMessageTogglePlugin::onSettingChanged(const QString &key)
{
    if (key != QLatin1String("persistState"))
        return;
    persist = persistFromString(context->settings().stringValue(key));
    // Upstream: lastState = newValue !== "none" && lastState
    if (persist == Persist::None)
        setEnabled(false);
    else
        setEnabled(enabled);
}

void SilentMessageTogglePlugin::setEnabled(bool on)
{
    enabled = on;
    if (persist == Persist::Restarts)
        context->settings().setValue(QStringLiteral("lastState"), on);
    else
        context->settings().reset(QStringLiteral("lastState"));
    context->refreshChatBar();
}

void SilentMessageTogglePlugin::followChannel(Id channelId)
{
    // "Don't persist" resets the toggle whenever the channel changes
    if (channelId != channel && persist == Persist::None)
        enabled = false;
    channel = channelId;
}

void SilentMessageTogglePlugin::onBeforeSend(OutgoingMessage &message)
{
    followChannel(message.channelId);
    if (!enabled)
        return;
    if (context->settings().boolValue(QStringLiteral("autoDisable")))
        setEnabled(false);
    if (!message.content.startsWith(Prefix))
        message.content = Prefix + message.content;
}

QList<ChatBarButton> SilentMessageTogglePlugin::chatBarButtons(const ChatContext &chat)
{
    followChannel(chat.channelId);
    ChatBarButton b;
    b.id = QStringLiteral("toggle");
    b.tooltip = enabled ? QStringLiteral("Disable Silent Message") : QStringLiteral("Enable Silent Message");
    b.svg = icon(enabled);
    b.active = enabled;
    return { b };
}

void SilentMessageTogglePlugin::onChatBarButtonClicked(const QString &id, const ChatContext &chat)
{
    if (id != QLatin1String("toggle"))
        return;
    followChannel(chat.channelId);
    setEnabled(!enabled);
}

QByteArray qord_manifest_silentmessagetoggle();

void qord_register_silentmessagetoggle(Registry &registry)
{
    registry.add({ QStringLiteral("silentmessagetoggle/plugin.json"),
                   [] { return std::make_unique<SilentMessageTogglePlugin>(); },
                   qord_manifest_silentmessagetoggle() });
}

} // namespace Plugins
} // namespace Qord
