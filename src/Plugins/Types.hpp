#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QFlags>
#include <QList>
#include <QString>
#include <QtGlobal>

namespace Qord {
namespace Plugins {

// Discord snowflake ids. The host library deliberately doesn't depend on
// Acheron's Core::Snowflake so it can be built and tested on its own.
using Id = quint64;

// A message about to be sent or edited. Hooks may rewrite `content`.
struct OutgoingMessage
{
    Id accountId = 0;
    Id channelId = 0;
    Id guildId = 0; // 0 in DMs
    Id messageId = 0; // the edited message; 0 for new messages
    Id replyToMessageId = 0;
    bool isEdit = false;
    QString content;
};

// A received message about to be rendered. Hooks may rewrite `content`; the
// stored message is unchanged, so this only affects what the user sees.
struct IncomingMessage
{
    Id accountId = 0;
    Id channelId = 0;
    Id guildId = 0;
    Id messageId = 0;
    Id authorId = 0;
    bool isOwn = false; // authored by the current account
    QString content;
};

// Context for deciding whether replying to a message pings its author.
struct ReplyContext
{
    Id accountId = 0;
    Id channelId = 0;
    Id guildId = 0;
    Id messageId = 0;
    Id authorId = 0;
    bool shiftHeld = false;
};

// The conversation the message input currently belongs to.
struct ChatContext
{
    Id accountId = 0;
    Id channelId = 0;
    Id guildId = 0;
    // For chat bar clicks: which mouse button (Discord plugins act differently
    // on left, middle and right clicks)
    enum class Click { Left, Middle, Right };
    Click click = Click::Left;
};

// About to tell Discord the user is typing.
struct TypingContext
{
    Id accountId = 0;
    Id channelId = 0;
    Id guildId = 0; // 0 in DMs
    Id connectedVoiceChannelId = 0; // the voice channel the user is in, if any
    QDateTime lastOwnMessageAt; // the user's last message in this channel, if loaded
};

// A message author's name about to be shown in the chat.
struct AuthorContext
{
    Id accountId = 0;
    Id guildId = 0;
    Id channelId = 0;
    Id userId = 0;
    QString username;
    QString globalName; // display name, may be empty
    QString nick; // server nickname, may be empty
    QString friendNickname; // your friend nickname for them, may be empty
    bool isBot = false;
    QString discriminator; // "0" for migrated users
    enum class Place { Message, Reply };
    Place place = Place::Message;
};

// A server or channel that a context menu was opened for.
struct ChannelContext
{
    Id accountId = 0;
    Id guildId = 0; // 0 for DMs
    Id channelId = 0; // 0 when the menu is for the server itself
    QString name;
    int type = -1; // Discord channel type, -1 for a server
    // CDN URLs as the client shows them; empty when there is none.
    // For a server: its icon and banner. For a group DM: its icon.
    QString iconUrl;
    QString bannerUrl;
};

// A user that a context menu was opened for.
struct UserContext
{
    Id accountId = 0;
    Id guildId = 0; // 0 outside servers
    Id channelId = 0; // 0 when not opened from a channel
    Id userId = 0;
    // CDN URLs as the client shows them. avatarUrl is the user's own avatar
    // (Discord's default avatar if they have none); memberAvatarUrl is their
    // server avatar, empty if they have none.
    QString avatarUrl;
    QString memberAvatarUrl;
};

// An image in a user profile that was clicked.
struct ProfileImageContext
{
    Id accountId = 0;
    Id guildId = 0;
    Id userId = 0;
    enum class Kind { Avatar, Banner };
    Kind kind = Kind::Avatar;
    QString url; // the CDN URL the profile shows
};

// The server list (left rail).
struct ServerListContext
{
    Id accountId = 0;
};

// A button above the servers in the server list.
struct ServerListButton
{
    QString id;
    QString label;
    QString tooltip;
};

// Member numbers for a server, for the member list header or the server's
// tooltip in the server list.
struct MemberCountContext
{
    Id accountId = 0;
    Id guildId = 0;
    Id channelId = 0; // the channel whose member list is shown; 0 for tooltips
    enum class Place { MemberList, ServerTooltip };
    Place place = Place::MemberList;
    // Unknown counts are -1. `online` is the online members in this channel's
    // member list (MemberList) or the last known online count for the server
    // (ServerTooltip).
    int online = -1;
    int total = -1;
    int inVoice = 0; // users in voice channels the user can see
};

// One number in the member count widget.
struct CountBadge
{
    enum class Icon { Online, Total, Voice };
    Icon icon = Icon::Online;
    QString text;
    QString tooltip;
};

// One of the current user's sessions (from SESSIONS_REPLACE).
struct SessionInfo
{
    QString client; // "desktop", "mobile", "web", "unknown", ...
    QString status; // "online", "idle", "dnd", ...
};

// A user whose name is about to be shown, for decorations next to it.
struct DecorationContext
{
    Id accountId = 0;
    Id guildId = 0;
    Id userId = 0;
    bool isBot = false;
    bool isSelf = false;
    enum class Place { MemberList, Message, Profile };
    Place place = Place::Message;
    // client_status from the user's presence: platform -> status, in the
    // order Discord sent them ("desktop", "mobile", "web", "embedded", "vr")
    QList<QPair<QString, QString>> clientStatus;
    QList<SessionInfo> ownSessions; // only for the current user
};

// Someone joined, moved between or left voice channels.
struct VoiceStateChange
{
    Id accountId = 0;
    Id guildId = 0; // 0 for DM calls
    Id userId = 0;
    Id channelId = 0; // the channel now; 0 when they left voice
    bool isSelf = false;
    qint64 atMs = 0; // when the client saw it (ms since the epoch)
};

// A user listed under a voice channel.
struct VoiceParticipantContext
{
    Id accountId = 0;
    Id guildId = 0;
    Id channelId = 0;
    Id userId = 0;
    bool isSelf = false;
    qint64 nowMs = 0;
};

// Extra text for a voice participant: shown after the name, or as a small
// icon with the text as its tooltip.
struct VoiceLabel
{
    QString text;
    bool inlineText = false;
};

// The current user's voice connection (the panel above the user panel).
struct VoiceConnectionContext
{
    Id accountId = 0;
    Id userId = 0; // the current user
    Id guildId = 0;
    Id channelId = 0;
    qint64 nowMs = 0;
};

// An icon drawn after a user's name. `svg` is an SVG document drawn in
// `color` where it says "currentColor"; `size` is in pixels.
struct NameDecoration
{
    QByteArray svg;
    QString color;
    QString tooltip;
    int width = 20;
    int height = 20;
};

// A button a plugin shows next to the message input. `svg` is an SVG
// document; "currentColor" is drawn in the theme's icon color.
struct ChatBarButton
{
    QString id;
    QString tooltip;
    QByteArray svg;
    bool active = false;
};

// A menu entry; one with children is shown as a submenu.
struct MenuItem
{
    QString id;
    QString label;
    QList<MenuItem> children;
};

// A message as plugins see it (from the client's cache).
struct MessageContext
{
    Id accountId = 0;
    Id channelId = 0;
    Id guildId = 0;
    Id parentChannelId = 0; // category or thread parent, 0 if unknown
    Id messageId = 0;
    Id authorId = 0;
    bool authorIsBot = false;
    bool isOwn = false;
    int type = 0; // Discord message type
    quint64 flags = 0; // Discord message flags
    Id referencedAuthorId = 0; // author of the replied-to message, if known
    QString content;
    QDateTime timestamp;
    QDateTime editedTimestamp;
    bool deleted = false; // kept on screen by a plugin after deletion
    QString imageUrl; // for context menus: the image under the cursor
    QString rawJson; // what the client knows about the message, as JSON
    bool hasEmbeds = false;
    bool canEmbedLinks = true; // the user may embed links in this channel
    bool isDm = false; // a 1:1 DM
    bool isGroupDm = false;
    bool mentionsMe = false; // the current user is in the message's user mentions
    bool mentionEveryone = false;
};

// A message the client is about to show a desktop notification for.
struct NotificationContext
{
    MessageContext message;
    bool isOldestUnread = false; // first unread message in its channel
};

// One part of a text diff (see TextDiff.hpp).
struct DiffPart
{
    enum class Kind { Unchanged, Added, Removed };
    Kind kind = Kind::Unchanged;
    QString text;
    bool operator==(const DiffPart &o) const { return kind == o.kind && text == o.text; }
};

// Extra state a plugin attaches to a message for rendering.
struct MessageAnnotation
{
    bool deleted = false;
    enum class DeletedStyle { Text, Overlay };
    DeletedStyle deletedStyle = DeletedStyle::Text;

    struct Edit
    {
        QDateTime timestamp; // when this version was replaced
        QString content;
        QList<DiffPart> diff; // optional: this version vs. the next one
    };
    QList<Edit> history; // oldest first; empty = nothing to show
    // With diffs: old versions show only their removals, and what the
    // latest edit added is highlighted in the current content
    bool separatedDiffs = false;
    QList<DiffPart> currentDiff; // used with separatedDiffs
};

} // namespace Plugins
} // namespace Qord
