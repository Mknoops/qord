/*
 * Qord, a native Discord client
 * Port of Equicord's UserVoiceShow plugin (src/plugins/userVoiceShow)
 * Original: Copyright (c) Vendicated and contributors, authors Nuckyz, LordElias, omaw
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "UserVoiceShow.hpp"

#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

#include <QTimer>

namespace Qord {
namespace Plugins {

namespace {
// Discord's channel types for DMs and group DMs
constexpr int DmChannel = 1;
constexpr int GroupDmChannel = 3;

// Paths copied from upstream's components.tsx
const char speakerPaths[] =
        "<path d=\"M12 3a1 1 0 0 0-1-1h-.06a1 1 0 0 0-.74.32L5.92 7H3a1 1 0 0 0-1 1v8a1 1 0 0 0 1 1h2.92l4.28 4.68a1 1 0 0 0 "
        ".74.32H11a1 1 0 0 0 1-1V3ZM15.1 20.75c-.58.14-1.1-.33-1.1-.92v-.03c0-.5.37-.92.85-1.05a7 7 0 0 0 0-13.5A1.11 1.11 0 0 "
        "1 14 4.2v-.03c0-.6.52-1.06 1.1-.92a9 9 0 0 1 0 17.5Z\"/>"
        "<path d=\"M15.16 16.51c-.57.28-1.16-.2-1.16-.83v-.14c0-.43.28-.8.63-1.02a3 3 0 0 0 0-5.04c-.35-.23-.63-.6-.63-1.02v-.14c0-"
        ".63.59-1.1 1.16-.83a5 5 0 0 1 0 9.02Z\"/>";
const char lockedSpeakerPaths[] =
        "<path fill-rule=\"evenodd\" clip-rule=\"evenodd\" d=\"M16 4h.5v-.5a2.5 2.5 0 0 1 5 0V4h.5a1 1 0 0 1 1 1v4a1 1 0 0 1-1 "
        "1h-6a1 1 0 0 1-1-1V5a1 1 0 0 1 1-1Zm4-.5V4h-2v-.5a1 1 0 1 1 2 0Z\"/>"
        "<path d=\"M11 2a1 1 0 0 1 1 1v18a1 1 0 0 1-1 1h-.06a1 1 0 0 1-.74-.32L5.92 17H3a1 1 0 0 1-1-1V8a1 1 0 0 1 1-1h2.92l4.28-"
        "4.68a1 1 0 0 1 .74-.32H11ZM20.5 12c-.28 0-.5.22-.52.5a7 7 0 0 1-5.13 6.25c-.48.13-.85.55-.85 1.05v.03c0 .6.52 1.06 1.1.92a9 "
        "9 0 0 0 6.89-8.25.48.48 0 0 0-.49-.5h-1ZM16.5 12c-.28 0-.5.23-.54.5a3 3 0 0 1-1.33 2.02c-.35.23-.63.6-.63 1.02v.14c0 .63.59 "
        "1.1 1.16.83a5 5 0 0 0 2.82-4.01c.02-.28-.2-.5-.48-.5h-1Z\"/>";
const char mutedPaths[] =
        "<path d=\"m2.7 22.7 20-20a1 1 0 0 0-1.4-1.4l-20 20a1 1 0 1 0 1.4 1.4ZM10.8 17.32c-.21.21-.1.58.2.62V20H9a1 1 0 1 0 0 2h6a1 "
        "1 0 1 0 0-2h-2v-2.06A8 8 0 0 0 20 10a1 1 0 0 0-2 0c0 1.45-.52 2.79-1.38 3.83l-.02.02A5.99 5.99 0 0 1 12.32 16a.52.52 0 0 "
        "0-.34.15l-1.18 1.18ZM15.36 4.52c.15-.15.19-.38.08-.56A4 4 0 0 0 8 6v4c0 .3.03.58.1.86.07.34.49.43.74.18l6.52-6.52ZM5.06 "
        "13.98c.16.28.53.31.75.09l.75-.75c.16-.16.19-.4.08-.61A5.97 5.97 0 0 1 6 10a1 1 0 0 0-2 0c0 1.45.39 2.81 1.06 3.98Z\"/>";
const char deafPaths[] =
        "<path d=\"M22.7 2.7a1 1 0 0 0-1.4-1.4l-20 20a1 1 0 1 0 1.4 1.4l20-20ZM17.06 2.94a.48.48 0 0 0-.11-.77A11 11 0 0 0 2.18 "
        "16.94c.14.3.53.35.76.12l3.2-3.2c.25-.25.15-.68-.2-.76a5 5 0 0 0-1.02-.1H3.05a9 9 0 0 1 12.66-9.2c.2.09.44.05.59-.1l.76-.76ZM"
        "20.2 8.28a.52.52 0 0 1 .1-.58l.76-.76a.48.48 0 0 1 .77.11 11 11 0 0 1-4.5 14.57c-1.27.71-2.73.23-3.55-.74a3.1 3.1 0 0 1-.17-"
        "3.78l1.38-1.97a5 5 0 0 1 4.1-2.13h1.86a9.1 9.1 0 0 0-.75-4.72ZM10.1 17.9c.25-.25.65-.18.74.14a3.1 3.1 0 0 1-.62 2.84 2.85 "
        "2.85 0 0 1-3.55.74.16.16 0 0 1-.04-.25l3.48-3.48Z\"/>";
} // namespace

QByteArray UserVoiceShowPlugin::iconSvg(Icon icon, int size)
{
    const char *paths = speakerPaths;
    switch (icon) {
    case Icon::Speaker:
        break;
    case Icon::LockedSpeaker:
        paths = lockedSpeakerPaths;
        break;
    case Icon::Muted:
        paths = mutedPaths;
        break;
    case Icon::Deaf:
        paths = deafPaths;
        break;
    }
    return QStringLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%1\" height=\"%1\" viewBox=\"0 0 24 24\" "
                          "fill=\"currentColor\">%2</svg>")
            .arg(size)
            .arg(QString::fromLatin1(paths))
            .toUtf8();
}

UserVoiceShowPlugin::UserVoiceShowPlugin() = default;
UserVoiceShowPlugin::~UserVoiceShowPlugin() = default;

void UserVoiceShowPlugin::start(PluginContext &ctx)
{
    context = &ctx;
    clickTimer = std::make_unique<QTimer>();
    clickTimer->setSingleShot(true);
    clickTimer->setInterval(ClickDelayMs);
    QObject::connect(clickTimer.get(), &QTimer::timeout, [this]() {
        // ChannelRouter.transitionToChannel(channelId)
        if (context && context->ui())
            context->ui()->openChannel(pendingAccount, pendingChannel);
    });
}

void UserVoiceShowPlugin::stop()
{
    clickTimer.reset();
    context = nullptr;
}

std::optional<UserVoiceShowPlugin::Shown> UserVoiceShowPlugin::shownFor(Id accountId, Id userId) const
{
    const VoiceStateDirectory *voice = context ? context->voiceStates() : nullptr;
    if (!voice)
        return std::nullopt;
    const auto state = voice->voiceState(accountId, userId);
    if (!state || !state->channelId)
        return std::nullopt;
    const auto channel = voice->channel(accountId, state->channelId);
    if (!channel)
        return std::nullopt;
    Shown shown;
    shown.state = *state;
    shown.channel = *channel;
    shown.isPrivate = channel->type == DmChannel || channel->type == GroupDmChannel;
    if (!shown.isPrivate && !channel->canView && !context->isPluginEnabled(QStringLiteral("showhiddenchannels")))
        return std::nullopt;
    shown.locked = !shown.isPrivate && (!channel->canView || !channel->canConnect);
    return shown;
}

QString UserVoiceShowPlugin::tooltip(Id accountId, const Shown &shown) const
{
    // VoiceChannelTooltip: "In Voice Chat", <#channel>, then who is there
    const VoiceStateDirectory *voice = context->voiceStates();
    QStringList names;
    const QList<Id> users = voice->channelUsers(accountId, shown.channel.channelId);
    for (int i = 0; i < users.size() && i < MaxTooltipUsers; ++i) {
        const QString name = voice->displayName(accountId, shown.channel.guildId, users[i]);
        if (!name.isEmpty())
            names.append(name.toHtmlEscaped());
    }
    if (users.size() > MaxTooltipUsers)
        names.append(QStringLiteral("+%1").arg(users.size() - MaxTooltipUsers));
    QString channel = shown.channel.name.toHtmlEscaped();
    if (!shown.isPrivate)
        channel.prepend(QStringLiteral("\U0001F50A "));
    else if (shown.channel.type == DmChannel)
        channel.prepend(QLatin1Char('@'));
    QString html = QStringLiteral("<b>In Voice Chat</b><br><b>%1</b>").arg(channel);
    if (!names.isEmpty())
        html += QStringLiteral("<br>") + names.join(QStringLiteral(", "));
    return html;
}

QList<NameDecoration> UserVoiceShowPlugin::nameDecorations(const DecorationContext &user)
{
    if (!context || !user.userId)
        return {};
    auto &s = context->settings();
    switch (user.place) {
    case DecorationContext::Place::Profile:
        if (!s.boolValue(QStringLiteral("showInUserProfileModal")))
            return {};
        break;
    case DecorationContext::Place::MemberList:
        if (!s.boolValue(QStringLiteral("showInMemberList")))
            return {};
        break;
    case DecorationContext::Place::Message:
        if (!s.boolValue(QStringLiteral("showInMessages")))
            return {};
        break;
    }
    const auto shown = shownFor(user.accountId, user.userId);
    if (!shown)
        return {};

    const Icon icon = shown->locked           ? Icon::LockedSpeaker
                      : shown->state.deafened ? Icon::Deaf
                      : shown->state.muted    ? Icon::Muted
                                              : Icon::Speaker;
    constexpr int Size = 16;
    NameDecoration d;
    d.svg = iconSvg(icon, Size);
    d.color = QStringLiteral("icon"); // --interactive-icon-default
    d.tooltip = tooltip(user.accountId, *shown);
    d.width = Size;
    d.height = Size;
    d.clickId = QString::number(shown->channel.channelId);
    return { d };
}

void UserVoiceShowPlugin::onNameDecorationClicked(const QString &clickId, const DecorationContext &user,
                                                  bool doubleClick)
{
    Q_UNUSED(clickId);
    UiService *ui = context ? context->ui() : nullptr;
    if (!ui || !clickTimer)
        return;
    const auto shown = shownFor(user.accountId, user.userId);
    clickTimer->stop();
    if (!shown)
        return;
    if (doubleClick) {
        if (!shown->isPrivate && !shown->channel.canConnect) {
            ui->showToast(QStringLiteral("You cannot join the user's Voice Channel"), UiService::ToastType::Failure);
            return;
        }
        // selectVoiceChannel(channelId)
        ui->joinVoiceChannel(user.accountId, shown->channel.channelId);
        return;
    }
    pendingAccount = user.accountId;
    pendingChannel = shown->channel.channelId;
    clickTimer->start();
}

QByteArray qord_manifest_uservoiceshow();

void qord_register_uservoiceshow(Registry &registry)
{
    registry.add({ QStringLiteral("uservoiceshow/plugin.json"), [] { return std::make_unique<UserVoiceShowPlugin>(); },
                   qord_manifest_uservoiceshow() });
}

} // namespace Plugins
} // namespace Qord
