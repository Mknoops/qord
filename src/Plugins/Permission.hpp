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
    ReadServerPermissions = 1u << 21, // PluginContext::guilds()
    ShowHiddenChannels = 1u << 22, // hiddenChannels
    FetchUsers = 1u << 23, // unknownMentions, PluginContext::users()
    ReadDms = 1u << 24, // PluginContext::privateChannels()
    ControlAudio = 1u << 25, // PluginContext::audio()
    FetchMessages = 1u << 26, // PluginContext::messageLookup()
    ControlActivity = 1u << 27, // PluginContext::activity()
    ReadSessions = 1u << 28, // deviceSessions, PluginContext::sessions()
    AuthorizeApps = 1u << 29, // UiService::authorizeApp
    ReadMemberships = 1u << 30, // memberships
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
enum class Hook : quint64 {
    BeforeSend = Q_UINT64_C(1) << 0,
    BeforeEdit = Q_UINT64_C(1) << 1,
    ShouldMentionOnReply = Q_UINT64_C(1) << 2,
    TransformCopiedMessageLink = Q_UINT64_C(1) << 3,
    TransformIncomingContent = Q_UINT64_C(1) << 4,
    ChatBarButtons = Q_UINT64_C(1) << 5,
    UserContextMenu = Q_UINT64_C(1) << 6,
    MessageContextMenu = Q_UINT64_C(1) << 7,
    MessageHistory = Q_UINT64_C(1) << 8,
    MessageFilter = Q_UINT64_C(1) << 9,
    Typing = Q_UINT64_C(1) << 10,
    LinkWarnings = Q_UINT64_C(1) << 11,
    Notifications = Q_UINT64_C(1) << 12,
    AuthorNames = Q_UINT64_C(1) << 13,
    ChannelContextMenu = Q_UINT64_C(1) << 14,
    ServerListButtons = Q_UINT64_C(1) << 15,
    MemberCounts = Q_UINT64_C(1) << 16,
    UserDecorations = Q_UINT64_C(1) << 17,
    ProfileImages = Q_UINT64_C(1) << 18,
    VoiceStates = Q_UINT64_C(1) << 19,
    MessageClicks = Q_UINT64_C(1) << 20,
    ReactionUsers = Q_UINT64_C(1) << 21,
    ProfileRoleButtons = Q_UINT64_C(1) << 22,
    HiddenChannels = Q_UINT64_C(1) << 23,
    AttachmentButtons = Q_UINT64_C(1) << 24,
    ChannelTyping = Q_UINT64_C(1) << 25,
    UnknownMentions = Q_UINT64_C(1) << 26,
    KeyboardShortcuts = Q_UINT64_C(1) << 27,
    MessageAccessories = Q_UINT64_C(1) << 28,
    ProfileTabs = Q_UINT64_C(1) << 29,
    DmPins = Q_UINT64_C(1) << 30,
    EmojiSuggestions = Q_UINT64_C(1) << 31,
    FriendsSections = Q_UINT64_C(1) << 32,
    UserPanelButtons = Q_UINT64_C(1) << 33,
    DeviceSessions = Q_UINT64_C(1) << 34,
    Memberships = Q_UINT64_C(1) << 35,
};

// A set of hooks. QFlags holds at most 32 bits on Qt 5, so this is our own.
class Hooks
{
public:
    constexpr Hooks() = default;
    constexpr Hooks(Hook hook) : bits(quint64(hook)) {}

    [[nodiscard]] constexpr bool testFlag(Hook hook) const
    {
        return quint64(hook) != 0 && (bits & quint64(hook)) == quint64(hook);
    }
    Hooks &setFlag(Hook hook, bool on = true)
    {
        bits = on ? (bits | quint64(hook)) : (bits & ~quint64(hook));
        return *this;
    }
    [[nodiscard]] constexpr quint64 toUInt64() const { return bits; }
    constexpr explicit operator bool() const { return bits != 0; }
    constexpr bool operator!() const { return bits == 0; }

    constexpr Hooks operator|(Hooks other) const { return fromBits(bits | other.bits); }
    constexpr Hooks operator&(Hooks other) const { return fromBits(bits & other.bits); }
    Hooks &operator|=(Hooks other)
    {
        bits |= other.bits;
        return *this;
    }
    Hooks &operator&=(Hooks other)
    {
        bits &= other.bits;
        return *this;
    }
    constexpr bool operator==(Hooks other) const { return bits == other.bits; }
    constexpr bool operator!=(Hooks other) const { return bits != other.bits; }

private:
    static constexpr Hooks fromBits(quint64 value)
    {
        Hooks h;
        h.bits = value;
        return h;
    }
    quint64 bits = 0;
};

constexpr Hooks operator|(Hook a, Hook b)
{
    return Hooks(a) | Hooks(b);
}

constexpr int HookCount = 36;

std::optional<Hook> hookFromString(const QString &name);
QString hookToString(Hook hook);
Permission requiredPermission(Hook hook);
const QList<Hook> &allHooks();
int hookIndex(Hook hook);

} // namespace Plugins
} // namespace Qord
