#pragma once

#include <QFlags>
#include <QString>
#include <QStringList>

#include <optional>

namespace Qord {
namespace Plugins {

// What a plugin may do. Declared in plugin.json, granted by the user, and
// enforced by PluginHost: hooks and services are only reachable with the
// matching permission declared *and* granted.
// Keep in sync with PERMISSIONS in tools/qord_inventory.py.
enum class Permission : quint32 {
    ModifyOutgoingMessages = 1u << 0, // onBeforeSend / onBeforeEdit
    ModifyDisplayedMessages = 1u << 1, // transformIncomingContent
    ModifyMessageLinks = 1u << 2, // transformCopiedMessageLink
    ReplyMentions = 1u << 3, // shouldMentionOnReply
    ReadGuildMembers = 1u << 4, // PluginContext::members()
    Network = 1u << 5, // PluginContext::network()
    AddUiElements = 1u << 6, // chatBarButtons, userContextMenu
    WriteClipboard = 1u << 7, // PluginContext::clipboard()
    LogMessages = 1u << 8, // messageHistory
    HideMessages = 1u << 9, // messageFilter
    ReadRelationships = 1u << 10, // PluginContext::relationships()
    OpenLinks = 1u << 11, // PluginContext::ui()->openUrl()
    ControlTyping = 1u << 12, // typing
    TrustLinks = 1u << 13, // linkWarnings
    ControlNotifications = 1u << 14, // notifications
    EditMessages = 1u << 15, // PluginContext::messageActions()
    MarkRead = 1u << 16, // PluginContext::readStates()
    ReadPresences = 1u << 17, // userDecorations
    ReadVoiceStates = 1u << 18, // voiceStates
    ReadReactions = 1u << 19, // reactionUsers, PluginContext::reactions()
    ReadExpressions = 1u << 20, // PluginContext::expressions()
};
Q_DECLARE_FLAGS(Permissions, Permission)
Q_DECLARE_OPERATORS_FOR_FLAGS(Permissions)

std::optional<Permission> permissionFromString(const QString &name);
QString permissionToString(Permission permission);
QString permissionDescription(Permission permission);
QStringList permissionsToStrings(Permissions permissions);
const QList<Permission> &allPermissions();

// Hooks a plugin can implement. Each one requires a permission.
// Keep in sync with HOOK_DISPATCH in tools/qord_inventory.py.
enum class Hook : quint32 {
    BeforeSend = 1u << 0,
    BeforeEdit = 1u << 1,
    ShouldMentionOnReply = 1u << 2,
    TransformCopiedMessageLink = 1u << 3,
    TransformIncomingContent = 1u << 4,
    ChatBarButtons = 1u << 5,
    UserContextMenu = 1u << 6,
    MessageContextMenu = 1u << 7,
    MessageHistory = 1u << 8,
    MessageFilter = 1u << 9,
    Typing = 1u << 10,
    LinkWarnings = 1u << 11,
    Notifications = 1u << 12,
    AuthorNames = 1u << 13,
    ChannelContextMenu = 1u << 14,
    ServerListButtons = 1u << 15,
    MemberCounts = 1u << 16,
    UserDecorations = 1u << 17,
    ProfileImages = 1u << 18,
    VoiceStates = 1u << 19,
    MessageClicks = 1u << 20,
    ReactionUsers = 1u << 21,
};
Q_DECLARE_FLAGS(Hooks, Hook)
Q_DECLARE_OPERATORS_FOR_FLAGS(Hooks)

constexpr int HookCount = 22;

std::optional<Hook> hookFromString(const QString &name);
QString hookToString(Hook hook);
Permission requiredPermission(Hook hook);
const QList<Hook> &allHooks();
int hookIndex(Hook hook);

} // namespace Plugins
} // namespace Qord
