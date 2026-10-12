#pragma once

#include "Types.hpp"

#include <QByteArray>
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
};

} // namespace Plugins
} // namespace Qord
