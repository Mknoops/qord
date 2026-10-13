#pragma once

#include "Types.hpp"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QUrl>

#include <functional>
#include <optional>

namespace Qord {
namespace Plugins {

// Client services a plugin can reach through PluginContext, each gated by a
// permission. The app provides real implementations; tests provide fakes.

class NetworkService
{
public:
    struct Reply
    {
        bool ok = false;
        int status = 0;
        QByteArray body;
        QString error;
    };
    using Callback = std::function<void(const Reply &)>;
    using Handle = quint64;

    virtual ~NetworkService() = default;
    // Asynchronous GET. The callback runs on the main thread unless the
    // request was cancelled.
    virtual Handle get(const QUrl &url, Callback callback) = 0;
    // Asynchronous HTTPS POST. Headers may carry an API key: they are sent
    // as given and never logged. Unlike get(), a failed reply keeps its body.
    using Headers = QList<QPair<QByteArray, QByteArray>>;
    virtual Handle post(const QUrl &url, const Headers &headers, const QByteArray &body, Callback callback) = 0;
    virtual void cancel(Handle handle) = 0;
};

class MemberDirectory
{
public:
    virtual ~MemberDirectory() = default;
    // Role ids of a guild member as known to the client's cache, or nullopt
    // if the member isn't cached.
    [[nodiscard]] virtual std::optional<QList<Id>> memberRoles(Id accountId, Id guildId, Id userId) const = 0;
};

class ClipboardService
{
public:
    virtual ~ClipboardService() = default;
    virtual void setText(const QString &text) = 0;
};

class RelationshipDirectory
{
public:
    virtual ~RelationshipDirectory() = default;
    [[nodiscard]] virtual bool isBlocked(Id accountId, Id userId) const = 0;
    [[nodiscard]] virtual bool isIgnored(Id accountId, Id userId) const = 0;
    enum class Kind { Friend, Blocked, Ignored };
    [[nodiscard]] virtual QList<Id> relationshipIds(Id accountId, Kind kind) const
    {
        Q_UNUSED(accountId);
        Q_UNUSED(kind);
        return {};
    }
    // Whether you have any relationship with a user (friend, request, block)
    [[nodiscard]] virtual bool hasRelationship(Id accountId, Id userId) const
    {
        Q_UNUSED(accountId);
        Q_UNUSED(userId);
        return false;
    }
    // GET /users/@me/affinities/v2/users; `done` may run after the plugin
    // stopped, so guard it
    using AffinitiesCallback = std::function<void(bool ok, const QList<UserAffinity> &affinities)>;
    virtual void fetchUserAffinities(Id accountId, AffinitiesCallback done) const
    {
        Q_UNUSED(accountId);
        done(false, {});
    }
};

class UiService
{
public:
    // Discord's toasts (Vencord's showToast / Toasts.Type)
    enum class ToastType { Message, Success, Failure };
    struct TextSection
    {
        QString title;
        QString text;
        bool monospace = false;
        QString copiedToast; // shown (as a success toast) when Copy is used
    };
    virtual ~UiService() = default;
    // A read-only dialog with copyable sections.
    virtual void showTextDialog(const QString &title, const QList<TextSection> &sections) = 0;
    // Needs the open_links permission (PluginContext enforces it).
    virtual void openUrl(const QUrl &url) = 0;
    // A short message at the bottom of the window that goes away by itself
    virtual void showToast(const QString &text, ToastType type = ToastType::Message)
    {
        Q_UNUSED(text);
        Q_UNUSED(type);
    }
    // Inserts text at the cursor in the message box.
    virtual void insertIntoMessageBox(const QString &text) { Q_UNUSED(text); }
    // Discord's timestamp picker: date, time and format, with a preview.
    // The callback gets the formatted <t:...> markup; it is not called if
    // the user cancels.
    virtual void pickTimestamp(std::function<void(const QString &markup)> done) { Q_UNUSED(done); }
    // Opens the Plugins settings page at this plugin.
    virtual void openPluginSettings(const QString &pluginId) { Q_UNUSED(pluginId); }
    // PermissionsViewer's views: a list of roles, members and the owner with
    // the permissions each grants or (for overwrites) allows and denies, and
    // a member's permissions with the role that grants each, shown as a
    // popout at the mouse
    virtual void showPermissions(const PermissionsView &view) { Q_UNUSED(view); }
    virtual void showMemberPermissions(const MemberPermissionsView &view) { Q_UNUSED(view); }
    // ServerInfo's window, and a user's profile
    virtual void showServerInfo(const ServerInfoView &view) { Q_UNUSED(view); }
    virtual void openUserProfile(Id accountId, Id guildId, Id userId)
    {
        Q_UNUSED(accountId);
        Q_UNUSED(guildId);
        Q_UNUSED(userId);
    }
    // A dialog of searchable choices and switches (Translate's languages).
    // Each change is reported right away through `changed` with the field's
    // key and its new value (choice value, or "true"/"false" for a switch).
    struct FormField
    {
        QString key;
        QString label;
        QString description;
        QList<QPair<QString, QString>> choices; // value, label; empty: a switch
        QString value; // the chosen value, or "true"/"false"
        // showFormDialog only: a line of text, or a color (value: 0xRRGGBB
        // as a decimal number) picked from swatches or freely
        enum class Kind { Auto, Text, Color };
        Kind kind = Kind::Auto;
        QList<quint32> colors; // Color: the swatches
    };
    virtual void showForm(const QString &title, const QList<FormField> &fields,
                          std::function<void(const QString &key, const QString &value)> changed)
    {
        Q_UNUSED(title);
        Q_UNUSED(fields);
        Q_UNUSED(changed);
    }
    // A dialog with a confirm button (PinDMs' New Category): `done` gets
    // every field's value when it is confirmed (disabled while a text field
    // is empty), and isn't called on cancel.
    virtual void showFormDialog(const QString &title, const QList<FormField> &fields, const QString &confirmText,
                                std::function<void(const QHash<QString, QString> &values)> done)
    {
        Q_UNUSED(title);
        Q_UNUSED(fields);
        Q_UNUSED(confirmText);
        Q_UNUSED(done);
    }
    // Re-renders the lock screen of a hidden channel, if it is showing
    virtual void refreshLockScreen() {}
    // Opens a channel or DM of the account you are viewing
    virtual void openChannel(Id accountId, Id channelId)
    {
        Q_UNUSED(accountId);
        Q_UNUSED(channelId);
    }
    // Joins a voice channel (or a DM's call) of the account you are viewing
    virtual void joinVoiceChannel(Id accountId, Id channelId)
    {
        Q_UNUSED(accountId);
        Q_UNUSED(channelId);
    }
    // Re-asks for a message's attachment buttons (their state changed)
    virtual void refreshAttachments(Id accountId, Id channelId, Id messageId)
    {
        Q_UNUSED(accountId);
        Q_UNUSED(channelId);
        Q_UNUSED(messageId);
    }
    // The message box's reply and the message being edited, ended; the
    // message box focused; a message scrolled into view if it is near the
    // edge of the chat or out of it (QuickReply)
    virtual void cancelReply() {}
    virtual void cancelEditing() {}
    virtual void focusMessageBox() {}
    virtual void revealMessage(Id messageId) { Q_UNUSED(messageId); }
    // Starts editing one of your messages in the chat
    virtual void startEditing(const MessageContext &message) { Q_UNUSED(message); }
    // Replies to a message. With no explicit `mention`, the client decides
    // like Discord (ping unless shift is held; reply-mention plugins apply).
    virtual void startReply(const MessageContext &message, bool shiftHeld, std::optional<bool> mention)
    {
        Q_UNUSED(message);
        Q_UNUSED(shiftHeld);
        Q_UNUSED(mention);
    }
    // Shows a Discord-hosted image (cdn.discordapp.com, media.discordapp.net)
    // in the client's image viewer; other URLs are ignored.
    virtual void openImage(const QUrl &url) { Q_UNUSED(url); }
    // A small dialog asking for one line of text. `done` gets the text when
    // the user saves; it is not called if they cancel.
    // A modal question. Returns whether the user confirmed; with an empty
    // cancelText it only informs. A checkbox is shown when checkboxText is
    // set, and its state is written back.
    struct Confirmation
    {
        QString title;
        QString text;
        QString confirmText;
        QString cancelText;
        QString checkboxText;
        bool checkboxChecked = false;
    };
    virtual bool confirm(Confirmation &confirmation)
    {
        Q_UNUSED(confirmation);
        return false;
    }
    // Adds a file to the message box's attachments; the user still sends it.
    virtual void attachFile(const QString &fileName, const QByteArray &data, const QString &mimeType)
    {
        Q_UNUSED(fileName);
        Q_UNUSED(data);
        Q_UNUSED(mimeType);
    }
    virtual void promptText(const QString &title, const QString &label, const QString &initial, int maxLength,
                            std::function<void(const QString &text)> done)
    {
        Q_UNUSED(title);
        Q_UNUSED(label);
        Q_UNUSED(initial);
        Q_UNUSED(maxLength);
        Q_UNUSED(done);
    }
};

// How the client renders things that are not message content.
class DisplayOptionsService
{
public:
    virtual ~DisplayOptionsService() = default;
    // Relative timestamps (<t:...:R>) round down instead of to nearest
    virtual void setRelativeTimeRoundsDown(bool floor) = 0;
    // Avatar status dots show the phone shape for users on mobile whatever
    // their status, instead of only when they are online
    virtual void setMobileIndicatorForAnyStatus(bool any) { Q_UNUSED(any); }
    // The member list shows the server owner's crown even in large servers
    virtual void setForceOwnerCrown(bool force) { Q_UNUSED(force); }
    // Role colors in mentions, member list headers, voice users and message
    // text (all off by default)
    virtual void setRoleColors(const RoleColors &colors) { Q_UNUSED(colors); }
    // BetterFolders (nullopt: off)
    virtual void setFolderSidebar(const std::optional<FolderSidebar> &folders) { Q_UNUSED(folders); }
    // The image viewer's magnifying lens (nullopt: off); `changed` gets the
    // zoom and size the wheel set while magnifying
    virtual void setImageLens(const std::optional<ImageLens> &lens,
                              std::function<void(double zoom, int size)> changed)
    {
        Q_UNUSED(lens);
        Q_UNUSED(changed);
    }
    // Role dots (Discord's "Show role colors next to names") copy their
    // color when clicked (nullopt: they don't)
    virtual void setRoleDot(const std::optional<RoleDot> &dot) { Q_UNUSED(dot); }
    // Profiles show connected accounts as icons (nullopt: as a list). With
    // `githubOrgs`, a GitHub account is followed by its organizations: the
    // profile asks for a user's and shows the logins `done` gets.
    using GithubOrgsLookup =
            std::function<void(const QString &githubName, std::function<void(const QStringList &logins)> done)>;
    virtual void setCompactConnections(const std::optional<CompactConnections> &connections,
                                       GithubOrgsLookup githubOrgs)
    {
        Q_UNUSED(connections);
        Q_UNUSED(githubOrgs);
    }
};

class ReadStateService
{
public:
    virtual ~ReadStateService() = default;
    // Marks every unread channel, voice channel and joined thread in every
    // server as read, like Discord's "Mark As Read" on all servers at once.
    virtual void markAllServersRead(Id accountId) = 0;
};

// Servers' roles, owners and channel permission overwrites, and members
// (PermissionsViewer); everything comes from the client's cache.
class GuildDirectory
{
public:
    virtual ~GuildDirectory() = default;
    [[nodiscard]] virtual std::optional<GuildInfo> guild(Id accountId, Id guildId) const = 0;
    [[nodiscard]] virtual std::optional<ChannelInfo> channel(Id accountId, Id channelId) const = 0;
    // nullopt when the user isn't known; isMember says whether they are in
    // the server
    [[nodiscard]] virtual std::optional<MemberInfo> member(Id accountId, Id guildId, Id userId) const = 0;
    // Asks Discord for members the cache lacks (op 8); they arrive later
    virtual void requestMembers(Id accountId, Id guildId, const QList<Id> &userIds) = 0;
    // Your servers, and a server's members the client has loaded
    [[nodiscard]] virtual QList<Id> guildIds(Id accountId) const
    {
        Q_UNUSED(accountId);
        return {};
    }
    [[nodiscard]] virtual QList<Id> memberIds(Id accountId, Id guildId) const
    {
        Q_UNUSED(accountId);
        Q_UNUSED(guildId);
        return {};
    }
};

// Users on Discord (ValidUser): whether the client knows one, fetching one
// (GET /users/{id}; the client keeps what it gets), and re-rendering a
// message once it does
class UserLookupService
{
public:
    struct Result
    {
        bool ok = false;
        int status = 0; // HTTP status, 429 when rate limited
        int retryAfterMs = 0;
    };
    using Callback = std::function<void(const Result &)>;
    virtual ~UserLookupService() = default;
    [[nodiscard]] virtual bool isKnown(Id accountId, Id userId) const = 0;
    virtual void fetchUser(Id accountId, Id userId, Callback done) = 0;
    virtual void refreshMessage(Id accountId, Id channelId, Id messageId) = 0;
};

// Emojis, stickers and what you may use where.
class ExpressionService
{
public:
    virtual ~ExpressionService() = default;
    // Discord's premium_type: 0 none, 1 Nitro Classic, 2 Nitro, 3 Nitro Basic
    [[nodiscard]] virtual int premiumType(Id accountId) const = 0;
    [[nodiscard]] virtual std::optional<CustomEmojiInfo> customEmoji(Id accountId, Id emojiId) const = 0;
    [[nodiscard]] virtual std::optional<StickerInfo> sticker(Id accountId, Id stickerId) const = 0;
    // Your roles in a server
    [[nodiscard]] virtual QList<Id> ownRoles(Id accountId, Id guildId) const = 0;
    // Whether you have a permission (Discord's bit) in a channel; always
    // true in DMs and group DMs
    [[nodiscard]] virtual bool hasChannelPermission(Id accountId, Id channelId, quint64 permission) const = 0;
    // Let the emoji picker and autocomplete (for messages, not reactions)
    // and the sticker picker offer emojis and stickers Discord would lock
    virtual void setChatBypass(bool emojis, bool stickers) = 0;
};

class ReactionService
{
public:
    using Callback = std::function<void(bool ok, const QList<Id> &userIds)>;
    virtual ~ReactionService() = default;
    // GET the first 100 users of a reaction (type 0 normal, 1 burst). The
    // client stores the users it gets, for names and avatars.
    virtual void fetchReactionUsers(Id accountId, Id channelId, Id messageId, const QString &emojiKey, int type,
                                    Callback done) = 0;
    // Re-render a message whose reaction users changed
    virtual void reactionUsersChanged(Id accountId, Id channelId, Id messageId) = 0;
};

class MessageActionService
{
public:
    virtual ~MessageActionService() = default;
    // PATCH a message's flags (e.g. SUPPRESS_EMBEDS). Discord checks permissions.
    virtual void setMessageFlags(Id accountId, Id channelId, Id messageId, quint64 flags) = 0;
    // Deletes the message on Discord; a message a plugin kept on screen after
    // it was deleted is just removed from the chat.
    virtual void deleteMessage(Id accountId, Id channelId, Id messageId)
    {
        Q_UNUSED(accountId);
        Q_UNUSED(channelId);
        Q_UNUSED(messageId);
    }
    virtual void setPinned(Id accountId, Id channelId, Id messageId, bool pinned)
    {
        Q_UNUSED(accountId);
        Q_UNUSED(channelId);
        Q_UNUSED(messageId);
        Q_UNUSED(pinned);
    }
    // emoji: "name:id" for custom emoji, else the emoji itself
    virtual void setReaction(Id accountId, Id channelId, Id messageId, const QString &emoji, bool add)
    {
        Q_UNUSED(accountId);
        Q_UNUSED(channelId);
        Q_UNUSED(messageId);
        Q_UNUSED(emoji);
        Q_UNUSED(add);
    }
};

class MessageService
{
public:
    virtual ~MessageService() = default;
    // Re-render a message (e.g. after its annotation changed).
    virtual void refreshMessage(Id accountId, Id channelId, Id messageId) = 0;
    // Drop a message that a plugin kept after deletion.
    virtual void removeMessage(Id accountId, Id channelId, Id messageId) = 0;
    // Re-evaluate message filters for everything on screen.
    virtual void refreshFilters() = 0;
};

// Messages and channels that links point at (MessageLinkEmbeds).
class MessageLookupService
{
public:
    virtual ~MessageLookupService() = default;
    // A message the client has (loaded, stored, or fetched before)
    [[nodiscard]] virtual std::optional<LinkedMessage> message(Id accountId, Id channelId, Id messageId) = 0;
    // GET /channels/{channel}/messages?limit=1&around={message}; `done` says
    // whether it came back
    virtual void fetchMessage(Id accountId, Id channelId, Id messageId, std::function<void(bool found)> done) = 0;
    [[nodiscard]] virtual std::optional<LinkedChannel> channel(Id accountId, Id channelId) = 0;
};

// How voice audio plays (VolumeBooster).
class AudioOptionsService
{
public:
    virtual ~AudioOptionsService() = default;
    // User volume sliders go up to 200% times this (1 to 5)
    virtual void setUserVolumeMultiplier(double multiplier) = 0;
};

// Your DMs and group DMs, newest first (Discord's sorted private channels).
class PrivateChannelDirectory
{
public:
    virtual ~PrivateChannelDirectory() = default;
    [[nodiscard]] virtual QList<PrivateChannelInfo> privateChannels(Id accountId) const = 0;
};

// Who is in voice where (UserVoiceShow), from the client's voice states.
class VoiceStateDirectory
{
public:
    virtual ~VoiceStateDirectory() = default;
    [[nodiscard]] virtual std::optional<UserVoiceState> voiceState(Id accountId, Id userId) const = 0;
    [[nodiscard]] virtual QList<Id> channelUsers(Id accountId, Id channelId) const = 0;
    [[nodiscard]] virtual std::optional<LinkedChannel> channel(Id accountId, Id channelId) const = 0;
    // A user's name as the server (or, with no server, Discord) shows it
    [[nodiscard]] virtual QString displayName(Id accountId, Id guildId, Id userId) const = 0;
};

struct Services
{
    NetworkService *network = nullptr;
    MemberDirectory *members = nullptr;
    ClipboardService *clipboard = nullptr;
    RelationshipDirectory *relationships = nullptr;
    UiService *ui = nullptr;
    MessageService *messages = nullptr;
    MessageActionService *messageActions = nullptr;
    DisplayOptionsService *displayOptions = nullptr;
    ReadStateService *readStates = nullptr;
    ReactionService *reactions = nullptr;
    ExpressionService *expressions = nullptr;
    GuildDirectory *guilds = nullptr;
    UserLookupService *users = nullptr;
    PrivateChannelDirectory *privateChannels = nullptr;
    AudioOptionsService *audio = nullptr;
    MessageLookupService *messageLookup = nullptr;
    VoiceStateDirectory *voiceStates = nullptr;
};

} // namespace Plugins
} // namespace Qord
