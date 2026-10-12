#include "Permission.hpp"

#include <QCoreApplication>

namespace Qord {
namespace Plugins {

namespace {
struct PermissionInfo
{
    Permission permission;
    const char *name;
    const char *description;
};

const PermissionInfo permissionTable[] = {
    { Permission::ModifyOutgoingMessages, "modify_outgoing_messages",
      QT_TRANSLATE_NOOP("Qord::Plugins", "Change messages you send or edit") },
    { Permission::ModifyDisplayedMessages, "modify_displayed_messages",
      QT_TRANSLATE_NOOP("Qord::Plugins", "Change how received messages are shown to you") },
    { Permission::ModifyMessageLinks, "modify_message_links",
      QT_TRANSLATE_NOOP("Qord::Plugins", "Change message links you copy") },
    { Permission::ReplyMentions, "reply_mentions",
      QT_TRANSLATE_NOOP("Qord::Plugins", "Decide whether replies ping the author") },
    { Permission::ReadGuildMembers, "read_guild_members",
      QT_TRANSLATE_NOOP("Qord::Plugins", "Read server members' roles") },
    { Permission::Network, "network",
      QT_TRANSLATE_NOOP("Qord::Plugins", "Download data from the internet") },
    { Permission::AddUiElements, "add_ui_elements",
      QT_TRANSLATE_NOOP("Qord::Plugins", "Add buttons and menu items") },
    { Permission::WriteClipboard, "write_clipboard",
      QT_TRANSLATE_NOOP("Qord::Plugins", "Copy text to your clipboard") },
    { Permission::LogMessages, "log_messages",
      QT_TRANSLATE_NOOP("Qord::Plugins", "Keep deleted and edited messages visible") },
    { Permission::HideMessages, "hide_messages",
      QT_TRANSLATE_NOOP("Qord::Plugins", "Hide messages from the chat") },
    { Permission::ReadRelationships, "read_relationships",
      QT_TRANSLATE_NOOP("Qord::Plugins", "See your friends and who you have blocked or ignored") },
    { Permission::OpenLinks, "open_links",
      QT_TRANSLATE_NOOP("Qord::Plugins", "Open links in your browser") },
    { Permission::ControlTyping, "control_typing",
      QT_TRANSLATE_NOOP("Qord::Plugins", "Decide when others see you typing, and whether you see them typing") },
    { Permission::TrustLinks, "trust_links",
      QT_TRANSLATE_NOOP("Qord::Plugins", "Skip the warning before opening links") },
    { Permission::ControlNotifications, "control_notifications",
      QT_TRANSLATE_NOOP("Qord::Plugins", "Decide which messages notify you") },
    { Permission::EditMessages, "edit_messages",
      QT_TRANSLATE_NOOP("Qord::Plugins", "Change message settings on Discord (such as hiding embeds)") },
    { Permission::MarkRead, "mark_read",
      QT_TRANSLATE_NOOP("Qord::Plugins", "Mark channels as read") },
    { Permission::ReadPresences, "read_presences",
      QT_TRANSLATE_NOOP("Qord::Plugins", "See which devices people are online on") },
    { Permission::ReadVoiceStates, "read_voice_states",
      QT_TRANSLATE_NOOP("Qord::Plugins", "See who joins and leaves voice channels") },
    { Permission::ReadReactions, "read_reactions",
      QT_TRANSLATE_NOOP("Qord::Plugins", "See who reacted to messages") },
    { Permission::ReadExpressions, "read_expressions",
      QT_TRANSLATE_NOOP("Qord::Plugins", "See your servers' emojis and stickers, your Nitro status and channel permissions") },
    { Permission::ReadServerPermissions, "read_server_permissions",
      QT_TRANSLATE_NOOP("Qord::Plugins", "See servers' roles, owners and channel permission settings") },
    { Permission::ShowHiddenChannels, "show_hidden_channels",
      QT_TRANSLATE_NOOP("Qord::Plugins", "Show channels you can't see in the channel list") },
    { Permission::FetchUsers, "fetch_users",
      QT_TRANSLATE_NOOP("Qord::Plugins", "Look up users on Discord") },
    { Permission::ReadDms, "read_dms",
      QT_TRANSLATE_NOOP("Qord::Plugins", "See your DMs and group DMs") },
    { Permission::ControlAudio, "control_audio",
      QT_TRANSLATE_NOOP("Qord::Plugins", "Change how loud voice chat can play") },
};

struct HookInfo
{
    Hook hook;
    const char *name;
    Permission permission;
};

const HookInfo hookTable[] = {
    { Hook::BeforeSend, "onBeforeSend", Permission::ModifyOutgoingMessages },
    { Hook::BeforeEdit, "onBeforeEdit", Permission::ModifyOutgoingMessages },
    { Hook::ShouldMentionOnReply, "shouldMentionOnReply", Permission::ReplyMentions },
    { Hook::TransformCopiedMessageLink, "transformCopiedMessageLink", Permission::ModifyMessageLinks },
    { Hook::TransformIncomingContent, "transformIncomingContent", Permission::ModifyDisplayedMessages },
    { Hook::ChatBarButtons, "chatBarButtons", Permission::AddUiElements },
    { Hook::UserContextMenu, "userContextMenu", Permission::AddUiElements },
    { Hook::MessageContextMenu, "messageContextMenu", Permission::AddUiElements },
    { Hook::MessageHistory, "messageHistory", Permission::LogMessages },
    { Hook::MessageFilter, "messageFilter", Permission::HideMessages },
    { Hook::Typing, "typing", Permission::ControlTyping },
    { Hook::LinkWarnings, "linkWarnings", Permission::TrustLinks },
    { Hook::Notifications, "notifications", Permission::ControlNotifications },
    { Hook::AuthorNames, "authorNames", Permission::ModifyDisplayedMessages },
    { Hook::ChannelContextMenu, "channelContextMenu", Permission::AddUiElements },
    { Hook::ServerListButtons, "serverListButtons", Permission::AddUiElements },
    { Hook::MemberCounts, "memberCounts", Permission::AddUiElements },
    { Hook::UserDecorations, "userDecorations", Permission::ReadPresences },
    { Hook::ProfileImages, "profileImages", Permission::AddUiElements },
    { Hook::VoiceStates, "voiceStates", Permission::ReadVoiceStates },
    { Hook::MessageClicks, "messageClicks", Permission::AddUiElements },
    { Hook::ReactionUsers, "reactionUsers", Permission::ReadReactions },
    { Hook::ProfileRoleButtons, "profileRoleButtons", Permission::AddUiElements },
    { Hook::HiddenChannels, "hiddenChannels", Permission::ShowHiddenChannels },
    { Hook::AttachmentButtons, "attachmentButtons", Permission::AddUiElements },
    { Hook::ChannelTyping, "channelTyping", Permission::AddUiElements },
    { Hook::UnknownMentions, "unknownMentions", Permission::FetchUsers },
    { Hook::KeyboardShortcuts, "keyboardShortcuts", Permission::AddUiElements },
    { Hook::MessageAccessories, "messageAccessories", Permission::ModifyDisplayedMessages },
    { Hook::ProfileTabs, "profileTabs", Permission::AddUiElements },
    { Hook::DmPins, "dmPins", Permission::AddUiElements },
};
static_assert(std::size(hookTable) == HookCount);
} // namespace

std::optional<Permission> permissionFromString(const QString &name)
{
    for (const auto &info : permissionTable)
        if (name == QLatin1String(info.name))
            return info.permission;
    return std::nullopt;
}

QString permissionToString(Permission permission)
{
    for (const auto &info : permissionTable)
        if (info.permission == permission)
            return QString::fromLatin1(info.name);
    return {};
}

QString permissionDescription(Permission permission)
{
    for (const auto &info : permissionTable)
        if (info.permission == permission)
            return QCoreApplication::translate("Qord::Plugins", info.description);
    return {};
}

QStringList permissionsToStrings(Permissions permissions)
{
    QStringList out;
    for (const auto &info : permissionTable)
        if (permissions.testFlag(info.permission))
            out.append(QString::fromLatin1(info.name));
    return out;
}

const QList<Permission> &allPermissions()
{
    static const QList<Permission> list = [] {
        QList<Permission> l;
        for (const auto &info : permissionTable)
            l.append(info.permission);
        return l;
    }();
    return list;
}

std::optional<Hook> hookFromString(const QString &name)
{
    for (const auto &info : hookTable)
        if (name == QLatin1String(info.name))
            return info.hook;
    return std::nullopt;
}

QString hookToString(Hook hook)
{
    for (const auto &info : hookTable)
        if (info.hook == hook)
            return QString::fromLatin1(info.name);
    return {};
}

Permission requiredPermission(Hook hook)
{
    for (const auto &info : hookTable)
        if (info.hook == hook)
            return info.permission;
    Q_UNREACHABLE();
}

const QList<Hook> &allHooks()
{
    static const QList<Hook> list = [] {
        QList<Hook> l;
        for (const auto &info : hookTable)
            l.append(info.hook);
        return l;
    }();
    return list;
}

int hookIndex(Hook hook)
{
    for (int i = 0; i < HookCount; ++i)
        if (hookTable[i].hook == hook)
            return i;
    Q_UNREACHABLE();
}

} // namespace Plugins
} // namespace Qord
