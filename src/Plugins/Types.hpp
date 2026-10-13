#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QFlags>
#include <QList>
#include <QString>
#include <QStringList>
#include <QtGlobal>

#include <functional>
#include <optional>

namespace Qord {
namespace Plugins {

// Discord snowflake ids. The host library deliberately doesn't depend on
// Acheron's Core::Snowflake so it can be built and tested on its own.
using Id = quint64;

// A message about to be sent or edited. Hooks may rewrite `content`.
// A custom emoji from one of your servers.
struct CustomEmojiInfo
{
    Id id = 0;
    QString name;
    bool animated = false;
    Id guildId = 0;
    bool available = true; // false when the server lost the boost level for it
    bool managed = false; // from an integration (Twitch subscriber emojis)
    QList<Id> roles; // roles allowed to use it; empty for everyone
};

// A sticker the client knows: your servers' stickers.
struct StickerInfo
{
    Id id = 0;
    QString name;
    int formatType = 1; // 1 PNG, 2 APNG, 3 Lottie, 4 GIF
    Id guildId = 0;
    bool available = true;
    bool standard = false; // one of Discord's own sticker packs
};

// A sticker to show on a message.
struct StickerRef
{
    Id id = 0;
    QString name;
    int formatType = 1;
};

struct OutgoingMessage
{
    Id accountId = 0;
    Id channelId = 0;
    Id guildId = 0; // 0 in DMs
    Id messageId = 0; // the edited message; 0 for new messages
    Id replyToMessageId = 0;
    bool isEdit = false;
    QString content;
    QList<Id> stickerIds; // a sticker being sent (new messages only)
    // Set by a hook to stop the send or edit; the text stays in the message box
    bool cancel = false;
    // New messages only: set by a hook that needs the network first
    // (Translate's auto-translate). The send waits, with the message box
    // locked, until `done` is called with the final content; done(nullopt)
    // gives up and leaves the text in the box. Only one hook may defer.
    using DeferredDone = std::function<void(std::optional<QString> content)>;
    std::function<void(const QString &content, DeferredDone done)> deferred;
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

    struct Attachment
    {
        QString url;
        QString contentType;
    };
    struct Embed
    {
        QString type; // "image", "rich", ...
        QString url; // the embed's url, else its image's
    };
    QList<Attachment> attachments;
    QList<Embed> embeds;
    // Set by hooks: attachments and embeds (by index) not to show, and
    // stickers to show with the message
    QList<int> hiddenAttachments;
    QList<int> hiddenEmbeds;
    QList<StickerRef> addedStickers;
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
    bool shift = false; // Shift was held (Translate toggles auto-translate)
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
    bool isBot = false;
    QString username; // the user's username (not display name), when known
};

// A tab a plugin adds to user profiles (MutualGroupDMs): a list of rows,
// each clickable
struct ProfileTab
{
    QString id;
    QString title;
    struct Row
    {
        QString id;
        QString title;
        QString subtitle;
        QString iconUrl; // CDN image; empty: iconText on a plain circle
        QString iconText;
    };
    QList<Row> rows;
    QString emptyText; // shown when there are no rows
};

// PinDMs: categories of DMs shown above the others in the DM list. The rest
// follow under a "Direct Messages" header.
struct DmPinLayout
{
    struct Category
    {
        QString id;
        QString name;
        quint32 color = 0; // 0xRRGGBB
        bool collapsed = false; // only the open DM stays visible
        QList<Id> channelIds; // display order
    };
    QList<Category> categories;
    bool sortByLastMessage = true; // in each category; false: channelIds' order
    bool dmSectionCollapsible = false; // clicking "Direct Messages" collapses the rest
    bool dmSectionCollapsed = false;
};
// The "Direct Messages" header's id in the DM pin hooks
inline const char *const DmSectionHeaderId = "dm-section";

// A message from another channel, as MessageLinkEmbeds shows it.
struct LinkedMessage
{
    Id channelId = 0;
    Id messageId = 0;
    Id authorId = 0;
    QString authorName;
    QString authorAvatarUrl;
    QString content;
    QDateTime timestamp;
    struct Image
    {
        QString url;
        int width = 0;
        int height = 0;
    };
    QList<Image> images; // image attachments, image embeds and GIFs
    int attachmentCount = 0;
    int embedCount = 0;
};

// A channel as a link to it shows it.
struct LinkedChannel
{
    Id channelId = 0;
    Id guildId = 0; // 0 for DMs
    int type = 0; // Discord's channel type
    QString name; // the channel's, or for a DM the other user's
    Id recipientId = 0; // a DM's other user
    QString iconUrl; // the server's icon, the group's, or the DM user's avatar
    bool canView = false; // you can see it (View Channel; always in DMs)
    bool canConnect = false; // you may join its voice (Connect; always in DMs)
};

// Where a user is in voice (Discord's voice state).
struct UserVoiceState
{
    Id guildId = 0; // 0 for DM calls
    Id channelId = 0;
    bool muted = false; // by a moderator or themselves
    bool deafened = false;
};

// A DM or group DM, as the DM list shows it.
struct PrivateChannelInfo
{
    Id channelId = 0;
    bool isGroup = false;
    QString name; // the group's name, or its members' names
    QString iconUrl; // a group's icon, empty if it has none
    QList<Id> recipientIds; // without you
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
// `color` where it says "currentColor": "#rrggbb", "status:<status>" (the
// theme's status colour) or "icon" (the theme's icon colour); `size` is in
// pixels.
struct NameDecoration
{
    QByteArray svg;
    QString color;
    QString tooltip; // plain text, or rich text (Qt's subset of HTML)
    int width = 20;
    int height = 20;
    // Set to make it clickable: onNameDecorationClicked gets it back
    QString clickId;
    QString pluginId; // filled in by the host
};

// A button a plugin shows next to the message input. `svg` is an SVG
// document; "currentColor" is drawn in the theme's icon color.
struct ChatBarButton
{
    QString id;
    QString tooltip;
    QByteArray svg;
    bool active = false;
    // Chat bar only: shown as a tooltip right away, without hovering, while
    // set (Translate's "Auto Translate Enabled")
    QString popupTooltip;
};

// A menu entry; one with children is shown as a submenu. One with neither id
// nor label is a separator (channel and DM list header menus).
struct MenuItem
{
    QString id;
    QString label;
    QList<MenuItem> children;
    std::optional<bool> checked = std::nullopt; // a checkbox item (message menus)
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
    QString snapshotContent; // a forwarded message's content
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

// A click on a message in the chat (DOM-style: detail is 1, 2 or 3 for
// single, double and triple clicks).
struct MessageClickContext
{
    MessageContext message;
    int detail = 1;
    bool primaryButton = true;
    // held while clicking
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
    bool backspace = false;
    bool deleteKey = false;
    qint64 timeMs = 0;
    // what the user may do here
    bool canSend = true;
    bool canManageMessages = false;
    bool canReact = true;
    bool pinned = false;
    bool isSystemDm = false; // a DM with Discord's system account
    QStringList myReactions; // reactions you added: "name:id" for custom emoji, else the emoji
    QString selectedText;
};

// A reaction on a message, for showing who reacted.
struct ReactionContext
{
    Id accountId = 0;
    Id guildId = 0;
    Id channelId = 0;
    Id messageId = 0;
    QString emojiName;
    Id emojiId = 0; // 0 for Unicode emoji
    int type = 0; // 0 normal, 1 burst (super reaction)
    int reactionsOnMessage = 0; // how many different reactions the message has
    // Users the client hides (NoBlockedMessages' blocked/ignored users), if any
    QList<Id> hiddenUsers;
};

// The users a plugin shows on a reaction (WhoReacted)
struct ReactionUsers
{
    QList<Id> users;
    bool clickable = true; // clicking an avatar opens the profile (else it reacts)
};

// Someone added or removed a reaction (MESSAGE_REACTION_ADD/REMOVE).
struct ReactionChange
{
    Id accountId = 0;
    Id channelId = 0;
    Id messageId = 0;
    QString emojiName;
    Id emojiId = 0;
    int type = 0;
    Id userId = 0;
    bool added = true;
};

// A server role as plugins see it (PermissionsViewer)
struct RoleInfo
{
    Id id = 0; // the guild's id for @everyone
    QString name;
    quint32 color = 0; // 0 when the role has no colour
    int position = 0;
    quint64 permissions = 0;
    QString iconUrl; // a custom role icon, if any
    QString unicodeEmoji; // a Unicode role icon, if any
};

// A channel's permission overwrite for a role or a member
struct PermissionOverwriteInfo
{
    enum class Type { Role = 0, Member = 1 };
    Type type = Type::Role;
    Id id = 0;
    quint64 allow = 0;
    quint64 deny = 0;
};

struct GuildInfo
{
    Id id = 0;
    QString name;
    Id ownerId = 0;
    QList<RoleInfo> roles; // highest position first, as Discord sorts them
    // ServerInfo
    QString description;
    QString iconUrl; // empty without an icon
    QString bannerUrl;
    QString vanityUrlCode;
    QString preferredLocale;
    int verificationLevel = -1; // -1: unknown
    int premiumSubscriptionCount = 0;
    int premiumTier = 0;
    QDateTime joinedAt; // when you joined
    int channelCount = -1; // channels and categories you can see; -1: unknown
};

struct ChannelInfo
{
    Id id = 0;
    Id guildId = 0;
    QString name;
    QList<PermissionOverwriteInfo> overwrites; // in Discord's order
    // Settings, as Discord sent them (ShowHiddenChannels' lock screen)
    int type = 0; // Discord's channel type
    QString topic;
    bool nsfw = false;
    Id lastMessageId = 0;
    QDateTime lastPinTimestamp;
    int rateLimitPerUser = 0; // seconds
    int defaultThreadRateLimitPerUser = 0; // seconds
    std::optional<int> bitrate;
    bool hasRtcRegion = false; // voice channels have the key (null: automatic)
    QString rtcRegion;
    std::optional<int> videoQualityMode;
    int defaultAutoArchiveDuration = 0; // minutes
    std::optional<int> defaultForumLayout;
    std::optional<int> defaultSortOrder;
    QString defaultReactionEmojiName; // a Unicode emoji
    Id defaultReactionEmojiId = 0; // or a custom one
    quint64 flags = 0;
    QStringList availableTags;
};

// A user, and their membership of a server if they are a member
struct MemberInfo
{
    Id userId = 0;
    QString username;
    QString globalName;
    QString avatarUrl;
    bool isMember = false;
    QString nick;
    QList<Id> roles;
};

// One row of a permissions view: a role, a member (a channel overwrite) or
// the server owner. A role or the owner has `permissions`; an overwrite has
// `allow` and `deny`.
struct PermissionsEntry
{
    enum class Kind { Role, Member, Owner };
    Kind kind = Kind::Role;
    Id id = 0;
    QString name;
    quint32 color = 0;
    QString imageUrl; // role icon or user avatar
    QString emoji; // Unicode role icon
    std::optional<quint64> permissions;
    quint64 allow = 0;
    quint64 deny = 0;
};

struct PermissionsView
{
    QString title; // e.g. "general Permissions"
    Id accountId = 0;
    Id guildId = 0;
    QList<PermissionsEntry> entries;
};

// A member's permissions and the role granting each (the profile's
// "View Permissions" popout)
struct GrantedPermission
{
    quint64 bit = 0; // 0: Server Owner
    QString roleName;
    quint32 roleColor = 0;
};

struct MemberPermissionsView
{
    QList<GrantedPermission> permissions;
    bool lowestFirst = false; // the sort order, for the toggle's arrow
    std::function<void()> toggleSort;
    std::function<void()> showRoleDetails;
};

// How hidden channels are shown in the channel list (ShowHiddenChannels)
struct HiddenChannelDisplay
{
    enum class Icon { Lock, LockRight, EyeRight }; // Lock replaces the channel's icon
    enum class Style { Classic, Muted, Unread, MutedUnread };
    Icon icon = Icon::Lock;
    Style style = Style::Classic;
};

// A channel you can't see (or, for voice, can't join), selected in the list
struct HiddenChannelContext
{
    Id accountId = 0;
    Id guildId = 0;
    Id channelId = 0;
    bool canView = false; // false: hidden; true: a locked voice channel
};

// What the lock screen shows instead of the channel
struct LockScreen
{
    QString heading; // "This is a hidden text channel"
    bool nsfw = false;
    QString subheading;
    QString topic; // a forum's guidelines (markdown)
    struct Detail
    {
        QString text;
        QDateTime time; // shown after the text when valid
    };
    QList<Detail> details;
    QStringList tags; // a forum's tags
    QList<PermissionsEntry> allowed; // roles and members who can see it
    bool allowedExpanded = true;
    std::function<void()> toggleAllowed;
    std::function<void()> showPermissionDetails;
};

// A text file attachment with the start of it Qord loaded (CopyFileContents)
struct AttachmentContext
{
    Id accountId = 0;
    Id channelId = 0;
    Id messageId = 0;
    Id attachmentId = 0;
    QString filename;
    QString contentType;
    qint64 size = 0;
    QString text; // the start of the file
    qint64 bytesLeft = 0; // the part that wasn't loaded (0: `text` is the whole file)
};

// ServerInfo's window: the server's details and four lists of users
struct ServerInfoUser
{
    Id userId = 0;
    QString name; // display name, or username
    QString username;
    QString avatarUrl;
    int mutualServers = 0; // Mutual Users only
    struct Server
    {
        QString name;
        QString iconUrl;
        QString acronym; // shown without an icon
    };
    QList<Server> mutualServerIcons; // the first three
};

struct ServerInfoView
{
    Id accountId = 0;
    Id guildId = 0;
    QString name;
    QString description;
    QString iconUrl;
    QString bannerUrl;
    QString acronym; // shown without an icon
    struct Field
    {
        QString label;
        QString text;
        QDateTime time; // shown instead of `text` when valid
        Id userId = 0; // a user (the owner) shown with `avatarUrl`
        QString avatarUrl;
    };
    QList<Field> fields;
    struct Tab
    {
        QString title;
        QList<ServerInfoUser> users;
    };
    QList<Tab> tabs;
};

// A channel in the channel list that someone is typing in (TypingIndicator)
struct ChannelTypingContext
{
    Id accountId = 0;
    Id guildId = 0; // 0 for DMs
    Id channelId = 0;
    bool selected = false; // the channel you have open
    bool muted = false;
    QList<Id> typers; // oldest first; never you
};

// What the channel list shows next to such a channel
struct TypingIndicator
{
    QList<Id> avatars; // up to three are drawn, then "+N"
    bool dots = true; // Discord's animated dots
    QString tooltip;
};

// The mouse entered a user mention in a message (ValidUser)
struct MentionContext
{
    Id accountId = 0;
    Id channelId = 0;
    Id messageId = 0;
    Id userId = 0;
};

// A loaded message in the open channel, for keyboard navigation
struct ChatMessageInfo
{
    Id id = 0;
    Id authorId = 0;
    int type = 0; // Discord's message type
    quint64 flags = 0;
    bool deleted = false; // kept on screen by a plugin (MessageLogger)
};

// Ctrl+Up/Down (Cmd on macOS), with or without Shift, in the main window
// (QuickReply)
struct ShortcutContext
{
    Id accountId = 0;
    Id guildId = 0;
    Id channelId = 0;
    enum class Key { Up, Down };
    Key key = Key::Up;
    bool shift = false;
    bool canSend = true; // in the open channel
    Id replyingTo = 0; // the message box's reply, if any
    Id editing = 0; // the message being edited, if any
    QList<ChatMessageInfo> messages; // oldest first, as shown
};

// One part of a text diff (see TextDiff.hpp).
struct DiffPart
{
    enum class Kind { Unchanged, Added, Removed };
    Kind kind = Kind::Unchanged;
    QString text;
    bool operator==(const DiffPart &o) const { return kind == o.kind && text == o.text; }
};

// An emoji in the autocomplete or the picker. key: a custom emoji's id, or a
// Unicode emoji's name (Discord's frecency and favorites keys).
struct EmojiInfo
{
    QString key;
    QString name; // without colons (a custom emoji's original name)
    Id customId = 0;
    bool animated = false;
    QString surrogates; // Unicode only
    bool favorite = false; // in your Discord favorites
};

// The emoji autocomplete's suggestions for `:query`, best first. A plugin may
// reorder them and add others by key (shown if you can use them here).
struct EmojiAutocompleteContext
{
    Id accountId = 0;
    Id channelId = 0;
    QString query;
    bool forReaction = false;
    QList<EmojiInfo> results;
};

// An emoji right-clicked in the emoji picker.
struct EmojiContext
{
    Id accountId = 0;
    Id channelId = 0;
    EmojiInfo emoji;
};

// BetterFolders: open server folders on a second rail, and when they open
// and close.
struct FolderSidebar
{
    bool sidebar = true;
    bool closeAllFolders = false;
    bool closeAllHomeButton = false;
    bool closeOthers = false;
    bool closeServerFolder = false;
    bool forceOpen = false;
    bool keepIcons = false;
    enum class FolderIcon { Never, Always, MoreThanOneFolderExpanded };
    FolderIcon folderIcon = FolderIcon::Always;
};

// ImageZoom's lens in the image viewer.
struct ImageLens
{
    double zoom = 2.0;
    int size = 100;
    double zoomSpeed = 0.5;
    bool square = false;
    bool nearestNeighbour = false;
    bool invertScroll = true;
};

// The panel at the bottom left with your name and the mute and deafen
// buttons, for an account.
struct UserPanelContext
{
    Id accountId = 0;
};

// A button a plugin adds next to mute and deafen (GameActivityToggle). `svg`
// is drawn with "currentColor" in the theme's icon color; `danger` gives it a
// red glow, as Discord shows a button that is switched off.
struct UserPanelButton
{
    QString id;
    QString tooltip;
    QByteArray svg;
    bool danger = false;
};

// One of your connected accounts, and whether it shares its activity.
struct ActivityConnection
{
    QString type; // "spotify", ...
    QString id;
    QString name;
    bool showActivity = false;
    bool revoked = false;
};

// A device logged in to your account (Discord's Devices settings, GET
// /auth/sessions).
struct DeviceSession
{
    QString idHash;
    QString os; // "Windows", "Android", ...
    QString platform; // "Discord Client", "Chrome", ...
    QString location;
    qint64 lastUsedMs = 0; // approx_last_used_time, ms since the epoch
    bool current = false; // the session Qord is using
};

// How a plugin shows a device in the Devices settings (BetterSessions).
// Empty fields keep the client's.
struct DeviceSessionView
{
    QString title; // instead of "OS · Platform"
    bool isNew = false; // a red "NEW" badge after the title
    QString iconColor; // the device icon's background ("#rrggbb")
    QByteArray badgeSvg; // a small round badge on the icon, "currentColor" drawn dark
    QString timeTooltip; // tooltip on the "last used" text
    QList<MenuItem> buttons; // small buttons after the title ("Rename")
};

// A Discord app asking to be authorized on your account (OAuth2), as
// Discord's in-client authorization does (ReviewDB's login).
struct OAuthRequest
{
    QString clientId;
    QStringList scopes; // "identify", ...
    QString redirectUri;
    QString responseType = QStringLiteral("code");
    quint64 permissions = 0;
};

// A window listing things people wrote, with an input to add one and pages
// (ReviewDB's reviews). Built by the plugin; updated by it as data arrives.
struct ListDialogItem
{
    QString id;
    QString avatarUrl; // CDN image; empty: none
    QString title; // bold (the author)
    QString badge; // small text after the title ("Admin")
    QString meta; // muted text after that (a time)
    QString body; // wrapped plain text
    QList<MenuItem> menu; // a "…" button's menu; empty: none
};

struct ListDialogView
{
    QString title;
    QString notice; // above the list (a warning to be respectful)
    QList<ListDialogItem> items;
    QString emptyText;
    QString inputPlaceholder; // empty: no input
    bool inputEnabled = true; // false: clicking it reports inputBlocked
    QString pageText; // "Page 2 of 5"; empty: no pager
    bool canPrevious = false;
    bool canNext = false;
};

struct ListDialogEvents
{
    std::function<void(const QString &itemId, const QString &menuId)> menuAction;
    std::function<void(const QString &text)> submitted;
    std::function<void(int delta)> page; // -1 or +1
    std::function<void()> inputBlocked;
    std::function<void()> closed;
};

// A notification a plugin shows (Vencord's showNotification): a card in the
// corner of the window, and a desktop notification while Qord isn't focused.
struct PluginNotification
{
    QString title;
    QString body;
    bool permanent = false; // stays until closed or clicked (else 5 seconds)
    QString iconUrl; // a round image before the text (an avatar, a server's icon), optional
};

// A friend, a friend request, a server or a group DM, as you knew it.
struct MembershipEntry
{
    Id id = 0; // the user, server or channel
    QString name; // a user's username, a server's or group's name
    QString iconUrl; // an avatar or icon, empty if there is none
};

// Everyone and everything an account belongs to, after it connects and
// whenever that changes.
struct Memberships
{
    Id accountId = 0;
    QList<MembershipEntry> friends;
    QList<MembershipEntry> incomingRequests;
    QList<MembershipEntry> servers;
    QList<MembershipEntry> groups;
};

// A friend, friend request, server or group DM gone while Qord is running.
struct Removal
{
    enum class Kind { Friend, FriendRequest, Server, Group };
    Kind kind = Kind::Friend;
    Id accountId = 0;
    MembershipEntry entry;
    bool byYou = false; // you removed the friend, declined the request or left, from Qord
};

// Someone Discord ranks you as talking to (GET /users/@me/affinities).
struct UserAffinity
{
    Id userId = 0;
    double communicationRank = 0; // lower is closer
};

// The Friends page of an account.
struct FriendsContext
{
    Id accountId = 0;
};

// A tab a plugin adds to the Friends page, after Blocked (ImplicitRelationships).
struct FriendsSection
{
    QString id;
    QString label; // the tab
    QString countTitle; // above the list, %1 is the count ("Implicit — %1")
    QString emptyText; // when it has nobody
    QList<Id> userIds; // in order; the page asks for unknown users and shows them when they arrive
    bool sortByName = false; // sort by name, as friends are, instead
};

// BetterRoleDot: role dots copy their color when clicked.
struct RoleDot
{
    bool bothStyles = false; // names keep their role color next to the dot
    bool inProfile = false; // profile roles' dots can be clicked too
};

// ShowConnections: a profile's connected accounts as a row of icons.
struct CompactConnections
{
    int iconSize = 32; // px
    int spacing = 6; // px between icons
};

// Where RoleColorEverywhere shows role colors.
struct RoleColors
{
    bool mentions = false;
    bool memberListHeaders = false;
    bool voiceUsers = false;
    bool messages = false;
    int messageSaturation = 30; // percent
};

// Shown under a message's text (Translate's translation).
struct MessageAccessory
{
    QString content; // markdown, rendered like message text
    QString footer; // small text under it, e.g. "translated from Spanish"
    // Links after the footer ("Dismiss"); a click calls onMessageAccessoryAction
    QList<MenuItem> links;
    // Embeds shown after the message's own, drawn like Discord's
    struct Embed
    {
        QString authorName;
        QString authorIconUrl;
        QString description; // markdown
        QString footerText;
        QString footerIconUrl;
        QDateTime timestamp;
        quint32 color = 0; // 0xRRGGBB; 0 for the default
        struct Image
        {
            QString url;
            int width = 0;
            int height = 0;
        };
        QList<Image> images; // up to four
    };
    QList<Embed> embeds;
};
// How the chat links to an accessory's links: <href><plugin id>/<link id>
inline constexpr const char *MessageAccessoryHref = "acheron://accessory/";

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
