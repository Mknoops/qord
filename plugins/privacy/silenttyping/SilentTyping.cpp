/*
 * Qord, a native Discord client
 * Port of Equicord's SilentTyping plugin (src/plugins/silentTyping)
 * Original: Copyright (c) Vendicated and contributors, authors V, rini, Im_Banana, Etorix
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "SilentTyping.hpp"

#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

namespace Qord {
namespace Plugins {

namespace SilentTyping {

QStringList parseLocations(const QString &setting)
{
    QStringList out;
    for (const QString &part : setting.split(QLatin1Char(','))) {
        const QString t = part.trimmed();
        if (!t.isEmpty())
            out.append(t);
    }
    return out;
}

const QStringList &effectiveList(const Config &config)
{
    return config.defaultHidden ? config.disabledLocations : config.enabledLocations;
}

bool isExplicitlyDisallowed(const Config &config, Id channelId, Id guildId)
{
    if (!config.enabledGlobally)
        return false;
    const QStringList &list = effectiveList(config);
    // upstream compares string ids; an undefined guild never matches
    const bool listed = (guildId && list.contains(QString::number(guildId))) || list.contains(QString::number(channelId));
    return config.defaultHidden ? !listed : listed;
}

int implicitlyAllowed(const Config &config, const TypingContext &typing, const QDateTime &now)
{
    if (config.alwaysEnableInActiveVoiceChat && typing.connectedVoiceChannelId == typing.channelId)
        return 1;
    const int threshold = typing.guildId == 0 ? config.thresholdDirectMessages : config.thresholdServers;
    if (threshold == 0)
        return 0;
    if (typing.lastOwnMessageAt.isValid() && now < typing.lastOwnMessageAt.addSecs(threshold))
        return 2;
    return 0;
}

bool shouldSendTyping(const Config &config, const TypingContext &typing, const QDateTime &now)
{
    return !(isExplicitlyDisallowed(config, typing.channelId, typing.guildId) && !implicitlyAllowed(config, typing, now));
}

QString tooltip(const Config &config, Id channelId, Id guildId, int implicit)
{
    if (!config.enabledGlobally)
        return QStringLiteral("Typing Visible (Global)");
    if (implicit == 1)
        return QStringLiteral("Typing Always Visible In The Connected Voice Channel Chat");
    if (implicit == 2)
        return QStringLiteral("Typing Temporarily Visible In This Channel Due To A Recent Message");
    const QStringList &list = effectiveList(config);
    const QString location = guildId && list.contains(QString::number(guildId)) ? QStringLiteral("Guild")
                             : list.contains(QString::number(channelId))       ? QStringLiteral("Channel")
                                                                                : QStringLiteral("Global");
    return isExplicitlyDisallowed(config, channelId, guildId) ? QStringLiteral("Typing Hidden (%1)").arg(location)
                                                              : QStringLiteral("Typing Visible (%1)").arg(location);
}

QStringList toggled(QStringList list, const QString &id)
{
    if (list.contains(id))
        list.removeAt(list.indexOf(id));
    else
        list.append(id);
    return list;
}

} // namespace SilentTyping

namespace {
// Upstream's keyboard icon, struck through in red while typing is hidden
QByteArray icon(bool hidden)
{
    QByteArray svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"20\" viewBox=\"0 0 24 24\">";
    if (hidden)
        svg += "<mask id=\"m\"><path fill=\"#fff\" d=\"M0 0h24v24H0Z\"/><path stroke=\"#000\" stroke-width=\"5.99068\" d=\"M0 24 24 0\"/></mask>";
    svg += QByteArray("<path fill=\"currentColor\"") + (hidden ? " mask=\"url(#m)\"" : "")
           + " transform=\"translate(2, 3)\" d=\"M18.333 15.556H1.667a1.667 1.667 0 0 1 -1.667 -1.667v-10a1.667 1.667 0 0 1 1.667 -1.667h16.667a1.667 1.667 0 0 1 1.667 1.667v10a1.667 1.667 0 0 1 -1.667 1.667M4.444 6.25V4.861a0.417 0.417 0 0 0 -0.417 -0.417H2.639a0.417 0.417 0 0 0 -0.417 0.417V6.25a0.417 0.417 0 0 0 0.417 0.417h1.389a0.417 0.417 0 0 0 0.417 -0.417m3.333 0V4.861a0.417 0.417 0 0 0 -0.417 -0.417H5.973a0.417 0.417 0 0 0 -0.417 0.417V6.25a0.417 0.417 0 0 0 0.417 0.417h1.389a0.417 0.417 0 0 0 0.417 -0.417m3.333 0V4.861a0.417 0.417 0 0 0 -0.417 -0.417h-1.389a0.417 0.417 0 0 0 -0.417 0.417V6.25a0.417 0.417 0 0 0 0.417 0.417h1.389a0.417 0.417 0 0 0 0.417 -0.417m3.333 0V4.861a0.417 0.417 0 0 0 -0.417 -0.417h-1.389a0.417 0.417 0 0 0 -0.417 0.417V6.25a0.417 0.417 0 0 0 0.417 0.417h1.389a0.417 0.417 0 0 0 0.417 -0.417m3.333 0V4.861a0.417 0.417 0 0 0 -0.417 -0.417h-1.389a0.417 0.417 0 0 0 -0.417 0.417V6.25a0.417 0.417 0 0 0 0.417 0.417h1.389a0.417 0.417 0 0 0 0.417 -0.417m-11.667 3.333V8.194a0.417 0.417 0 0 0 -0.417 -0.417H4.306a0.417 0.417 0 0 0 -0.417 0.417V9.583a0.417 0.417 0 0 0 0.417 0.417h1.389a0.417 0.417 0 0 0 0.417 -0.417m3.333 0V8.194a0.417 0.417 0 0 0 -0.417 -0.417H7.639a0.417 0.417 0 0 0 -0.417 0.417V9.583a0.417 0.417 0 0 0 0.417 0.417h1.389a0.417 0.417 0 0 0 0.417 -0.417m3.333 0V8.194a0.417 0.417 0 0 0 -0.417 -0.417h-1.389a0.417 0.417 0 0 0 -0.417 0.417V9.583a0.417 0.417 0 0 0 0.417 0.417h1.389a0.417 0.417 0 0 0 0.417 -0.417m3.333 0V8.194a0.417 0.417 0 0 0 -0.417 -0.417h-1.389a0.417 0.417 0 0 0 -0.417 0.417V9.583a0.417 0.417 0 0 0 0.417 0.417h1.389a0.417 0.417 0 0 0 0.417 -0.417m-11.667 3.333v-1.389a0.417 0.417 0 0 0 -0.417 -0.417H2.639a0.417 0.417 0 0 0 -0.417 0.417V12.917a0.417 0.417 0 0 0 0.417 0.417h1.389a0.417 0.417 0 0 0 0.417 -0.417m10 0v-1.389a0.417 0.417 0 0 0 -0.417 -0.417H5.973a0.417 0.417 0 0 0 -0.417 0.417V12.917a0.417 0.417 0 0 0 0.417 0.417h8.056a0.417 0.417 0 0 0 0.417 -0.417m3.333 0v-1.389a0.417 0.417 0 0 0 -0.417 -0.417h-1.389a0.417 0.417 0 0 0 -0.417 0.417V12.917a0.417 0.417 0 0 0 0.417 0.417h1.389a0.417 0.417 0 0 0 0.417 -0.417\"/>";
    if (hidden)
        svg += "<path fill=\"#f23f43\" d=\"m21.178 1.70703 1.414 1.414L4.12103 21.593l-1.414-1.415L21.178 1.70703Z\"/>";
    svg += "</svg>";
    return svg;
}
} // namespace

SilentTyping::Config SilentTypingPlugin::readConfig() const
{
    auto &s = context->settings();
    SilentTyping::Config c;
    c.enabledGlobally = s.boolValue(QStringLiteral("enabledGlobally"));
    c.defaultHidden = s.boolValue(QStringLiteral("defaultHidden"));
    c.alwaysEnableInActiveVoiceChat = s.boolValue(QStringLiteral("alwaysEnableInActiveVoiceChat"));
    c.thresholdServers = int(s.intValue(QStringLiteral("temporaryEnableThresholdServers")));
    c.thresholdDirectMessages = int(s.intValue(QStringLiteral("temporaryEnableThresholdDirectMessages")));
    c.enabledLocations = SilentTyping::parseLocations(s.stringValue(QStringLiteral("enabledLocations")));
    c.disabledLocations = SilentTyping::parseLocations(s.stringValue(QStringLiteral("disabledLocations")));
    return c;
}

void SilentTypingPlugin::start(PluginContext &ctx)
{
    context = &ctx;
    config = readConfig();
}

void SilentTypingPlugin::onSettingChanged(const QString &key)
{
    Q_UNUSED(key);
    config = readConfig();
    context->refreshChatBar();
}

std::optional<bool> SilentTypingPlugin::shouldSendTyping(const TypingContext &typing)
{
    return SilentTyping::shouldSendTyping(config, typing, QDateTime::currentDateTimeUtc());
}

std::optional<bool> SilentTypingPlugin::shouldShowTypers(const ChatContext &chat)
{
    Q_UNUSED(chat);
    if (context->settings().boolValue(QStringLiteral("hideChatBoxTypingIndicators")))
        return false;
    return std::nullopt;
}

std::optional<bool> SilentTypingPlugin::shouldShowMemberListTypers(const ChatContext &chat)
{
    Q_UNUSED(chat);
    if (context->settings().boolValue(QStringLiteral("hideMembersListTypingIndicators")))
        return false;
    return std::nullopt;
}

QList<ChatBarButton> SilentTypingPlugin::chatBarButtons(const ChatContext &chat)
{
    if (!context->settings().boolValue(QStringLiteral("chatIcon")))
        return {};
    // The voice and recent-message exceptions need the typing context; the
    // button shows the explicit state, as upstream does before re-rendering
    TypingContext t;
    t.channelId = chat.channelId;
    t.guildId = chat.guildId;
    const int implicit = SilentTyping::implicitlyAllowed(config, t, QDateTime::currentDateTimeUtc());
    const bool hidden = SilentTyping::isExplicitlyDisallowed(config, chat.channelId, chat.guildId) && !implicit;
    ChatBarButton b;
    b.id = QStringLiteral("toggle");
    b.tooltip = SilentTyping::tooltip(config, chat.channelId, chat.guildId, implicit);
    b.svg = icon(hidden);
    b.active = hidden;
    return { b };
}

void SilentTypingPlugin::onChatBarButtonClicked(const QString &id, const ChatContext &chat)
{
    if (id != QLatin1String("toggle"))
        return;
    const char *key = chat.click == ChatContext::Click::Middle  ? "chatIconMiddleClickAction"
                      : chat.click == ChatContext::Click::Right ? "chatIconRightClickAction"
                                                                : "chatIconLeftClickAction";
    perform(context->settings().stringValue(QString::fromLatin1(key)), chat);
}

std::optional<QString> SilentTypingPlugin::onSlashCommand(const QString &name, const QJsonObject &options,
                                                          const ChatContext &chat)
{
    if (name != QLatin1String("silent-typing"))
        return std::nullopt;
    // As upstream's execute: the toggle and three of the settings
    auto &s = context->settings();
    bool updated = false;
    if (options.value(QStringLiteral("toggle")).isString()) {
        updated = true;
        perform(options.value(QStringLiteral("toggle")).toString(), chat);
    }
    const std::pair<const char *, const char *> booleans[] = {
        { "chat-icon", "chatIcon" },
        { "chat-context-menu", "chatContextMenu" },
        { "default-hidden", "defaultHidden" },
    };
    for (const auto &[option, key] : booleans) {
        const QJsonValue value = options.value(QLatin1String(option));
        if (value.isBool()) {
            updated = true;
            s.setValue(QLatin1String(key), value.toBool());
        }
    }
    if (UiService *ui = context->ui())
        ui->showLocalMessage(chat.accountId, chat.channelId,
                             updated ? QStringLiteral("Silent typing settings updated.")
                                     : QStringLiteral("No changes made to silent typing settings."));
    return std::nullopt;
}

namespace {
// The message box menu's checkboxes: item id -> setting
const std::pair<const char *, const char *> MenuToggles[] = {
    { "vc-silent-typing-enabled", "enabledGlobally" },
    { "vc-silent-typing-chat-bar-indicators", "hideChatBoxTypingIndicators" },
    { "vc-silent-typing-members-list-indicators", "hideMembersListTypingIndicators" },
    { "vc-silent-typing-chat-icon", "chatIcon" },
    { "vc-silent-typing-default", "defaultHidden" },
};
} // namespace

QList<MenuItem> SilentTypingPlugin::chatInputContextMenuItems(const ChatContext &chat)
{
    Q_UNUSED(chat);
    auto &s = context->settings();
    if (!s.boolValue(QStringLiteral("chatContextMenu")))
        return {};
    const QString labels[] = { QStringLiteral("Enabled"), QStringLiteral("Chat Bar Indicators"),
                               QStringLiteral("Members List Indicators"), QStringLiteral("Chat Icon"),
                               QStringLiteral("Default Hidden") };
    MenuItem menu { QStringLiteral("vc-silent-typing"), QStringLiteral("Silent Typing") };
    for (size_t i = 0; i < std::size(MenuToggles); ++i) {
        MenuItem item { QString::fromLatin1(MenuToggles[i].first), labels[i] };
        item.checked = s.boolValue(QLatin1String(MenuToggles[i].second));
        menu.children.append(item);
    }
    return { menu };
}

void SilentTypingPlugin::onChatInputContextMenuAction(const QString &id, const ChatContext &chat)
{
    Q_UNUSED(chat);
    auto &s = context->settings();
    for (const auto &[itemId, key] : MenuToggles)
        if (id == QLatin1String(itemId))
            s.setValue(QLatin1String(key), !s.boolValue(QLatin1String(key)));
}

void SilentTypingPlugin::perform(const QString &action, const ChatContext &chat)
{
    auto &s = context->settings();
    if (action == QLatin1String("global")) {
        s.setValue(QStringLiteral("enabledGlobally"), !config.enabledGlobally);
    } else if (action == QLatin1String("channel") || action == QLatin1String("guild")) {
        const Id target = action == QLatin1String("guild") ? chat.guildId : chat.channelId;
        if (!target)
            return; // "guild" in a DM does nothing, like upstream
        const QStringList list = SilentTyping::toggled(SilentTyping::effectiveList(config), QString::number(target));
        s.setValue(config.defaultHidden ? QStringLiteral("disabledLocations") : QStringLiteral("enabledLocations"),
                   list.join(QStringLiteral(", ")));
    } else if (action == QLatin1String("settings")) {
        if (auto *ui = context->ui())
            ui->openPluginSettings(context->pluginId());
    }
}

QByteArray qord_manifest_silenttyping();

void qord_register_silenttyping(Registry &registry)
{
    registry.add({ QStringLiteral("silenttyping/plugin.json"), [] { return std::make_unique<SilentTypingPlugin>(); },
                   qord_manifest_silenttyping() });
}

} // namespace Plugins
} // namespace Qord
