/*
 * Qord, a native Discord client
 * Port of Equicord's ViewRaw plugin (src/plugins/viewRaw)
 * Original: Copyright (c) Vendicated and contributors, authors KingFish, Ven, rad, ImLvna
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ViewRaw.hpp"

#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

namespace Qord {
namespace Plugins {

namespace {
const QString MessageItem = QStringLiteral("vc-view-message-raw");
const QString ViewItem = QStringLiteral("vc-view-raw");

// Upstream's CopyRawIcon
const QByteArray CopyRawIcon = QByteArrayLiteral(
        R"(<svg viewBox="0 0 20 20" fill="currentColor" xmlns="http://www.w3.org/2000/svg">)"
        R"(<path d="M12.9297 3.25007C12.7343 3.05261 12.4154 3.05226 12.2196 3.24928L11.5746 3.89824C11.3811 )"
        R"(4.09297 11.3808 4.40733 11.5739 4.60245L16.5685 9.64824C16.7614 9.84309 16.7614 10.1569 16.5685 )"
        R"(10.3517L11.5739 15.3975C11.3808 15.5927 11.3811 15.907 11.5746 16.1017L12.2196 16.7507C12.4154 )"
        R"(16.9477 12.7343 16.9474 12.9297 16.7499L19.2604 10.3517C19.4532 10.1568 19.4532 9.84314 19.2604 )"
        R"(9.64832L12.9297 3.25007Z"/>)"
        R"(<path d="M8.42616 4.60245C8.6193 4.40733 8.61898 4.09297 8.42545 3.89824L7.78047 3.24928C7.58466 )"
        R"(3.05226 7.26578 3.05261 7.07041 3.25007L0.739669 9.64832C0.5469 9.84314 0.546901 10.1568 0.739669 )"
        R"(10.3517L7.07041 16.7499C7.26578 16.9474 7.58465 16.9477 7.78047 16.7507L8.42545 16.1017C8.61898 )"
        R"(15.907 8.6193 15.5927 8.42616 15.3975L3.43155 10.3517C3.23869 10.1569 3.23869 9.84309 3.43155 )"
        R"(9.64824L8.42616 4.60245Z"/></svg>)");
} // namespace

bool ViewRawPlugin::rightViews() const
{
    return context && context->settings().stringValue(QStringLiteral("clickMethod")) == QLatin1String("Right");
}

// The "Message" context menu entry is off unless the setting is on
QList<MenuItem> ViewRawPlugin::messageContextMenuItems(const MessageContext &message)
{
    Q_UNUSED(message);
    if (!context || !context->settings().boolValue(QStringLiteral("messageContextMenu")))
        return {};
    return { { MessageItem, QStringLiteral("View Raw"), {} } };
}

void ViewRawPlugin::onMessageContextMenuAction(const QString &id, const MessageContext &message)
{
    if (id == MessageItem)
        viewMessage(message);
}

QList<ChatBarButton> ViewRawPlugin::messagePopoverButtons(const MessageContext &message)
{
    Q_UNUSED(message);
    if (!context)
        return {};
    ChatBarButton button;
    button.id = ViewItem;
    button.tooltip = rightViews() ? QStringLiteral("Copy Raw (Left Click) / View Raw (Right Click)")
                                  : QStringLiteral("View Raw (Left Click) / Copy Raw (Right Click)");
    button.svg = CopyRawIcon;
    return { button };
}

void ViewRawPlugin::onMessagePopoverClicked(const QString &id, const MessageContext &message, bool rightClick)
{
    Q_UNUSED(id);
    if (rightClick == rightViews())
        viewMessage(message);
    else
        copyContent(message);
}

void ViewRawPlugin::copyContent(const MessageContext &message)
{
    if (!context)
        return;
    if (ClipboardService *clipboard = context->clipboard()) {
        clipboard->setText(message.content);
        // upstream: copyWithToast(msg.content)
        if (UiService *ui = context->ui())
            ui->showToast(QStringLiteral("Copied to clipboard!"), UiService::ToastType::Success);
    }
}

// openViewRawModalMessage(): the content, then the message's data
void ViewRawPlugin::viewMessage(const MessageContext &message)
{
    UiService *ui = context ? context->ui() : nullptr;
    if (!ui)
        return;
    QList<UiService::TextSection> sections;
    if (!message.content.isEmpty())
        sections.append({ QStringLiteral("Message Content"), message.content, true,
                          QStringLiteral("Content copied to clipboard!") });
    sections.append({ QStringLiteral("Message Data"), message.rawJson, true,
                      QStringLiteral("Message data copied to clipboard!") });
    ui->showTextDialog(QStringLiteral("Raw Message Data"), sections);
}

void ViewRawPlugin::view(GuildDirectory::RawKind kind, const QString &type, Id accountId, Id id, Id guildId)
{
    UiService *ui = context ? context->ui() : nullptr;
    if (!ui)
        return;
    GuildDirectory *guilds = context->guilds();
    if (!guilds) {
        ui->showToast(QStringLiteral("ViewRaw needs the \"See servers' roles, owners and channel permission settings\" "
                                     "permission for this. Allow it in its plugin settings."),
                      UiService::ToastType::Failure);
        return;
    }
    const std::optional<QString> json = guilds->rawJson(accountId, kind, id, guildId);
    if (!json) {
        ui->showToast(QStringLiteral("No %1 data is loaded for this.").arg(type.toLower()), UiService::ToastType::Failure);
        return;
    }
    ui->showTextDialog(QStringLiteral("Raw %1 Data").arg(type),
                       { { QStringLiteral("%1 Data").arg(type), *json, true,
                           QStringLiteral("%1 data copied to clipboard!").arg(type) } });
}

// "guild-context", "channel-context", "thread-context", "gdm-context"
QList<MenuItem> ViewRawPlugin::channelContextMenuItems(const ChannelContext &channel)
{
    Q_UNUSED(channel);
    return { { ViewItem, QStringLiteral("View Raw"), {} } };
}

void ViewRawPlugin::onChannelContextMenuAction(const QString &id, const ChannelContext &channel)
{
    if (id != ViewItem)
        return;
    if (channel.channelId)
        view(GuildDirectory::RawKind::Channel, QStringLiteral("Channel"), channel.accountId, channel.channelId, 0);
    else
        view(GuildDirectory::RawKind::Guild, QStringLiteral("Guild"), channel.accountId, channel.guildId, 0);
}

// "user-context"
QList<MenuItem> ViewRawPlugin::userContextMenuItems(const UserContext &user)
{
    Q_UNUSED(user);
    return { { ViewItem, QStringLiteral("View Raw"), {} } };
}

void ViewRawPlugin::onUserContextMenuAction(const QString &id, const UserContext &user)
{
    if (id != ViewItem)
        return;
    // "user-profile-overflow-menu": the profile
    if (!user.profileJson.isEmpty()) {
        if (UiService *ui = context ? context->ui() : nullptr)
            ui->showTextDialog(QStringLiteral("Raw Profile Data"),
                               { { QStringLiteral("Profile Data"), user.profileJson, true,
                                   QStringLiteral("Profile data copied to clipboard!") } });
        return;
    }
    view(GuildDirectory::RawKind::User, QStringLiteral("User"), user.accountId, user.userId, 0);
}

// "guild-settings-role-context", and "dev-context" on a role
QList<MenuItem> ViewRawPlugin::roleContextMenuItems(const RoleContext &role)
{
    Q_UNUSED(role);
    return { { ViewItem, QStringLiteral("View Raw"), {} } };
}

void ViewRawPlugin::onRoleContextMenuAction(const QString &id, const RoleContext &role)
{
    if (id == ViewItem)
        view(GuildDirectory::RawKind::Role, QStringLiteral("Role"), role.accountId, role.roleId, role.guildId);
}

QByteArray qord_manifest_viewraw();

void qord_register_viewraw(Registry &registry)
{
    registry.add({ QStringLiteral("viewraw/plugin.json"), [] { return std::make_unique<ViewRawPlugin>(); },
                   qord_manifest_viewraw() });
}

} // namespace Plugins
} // namespace Qord
