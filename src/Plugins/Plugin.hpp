#pragma once

#include "Permission.hpp"
#include "Types.hpp"

#include <QJsonObject>

#include <optional>

namespace Qord {
namespace Plugins {

class PluginContext;

// Base class of every native plugin. All hooks default to no-ops; a plugin
// overrides the ones it needs and lists them in hooks() and in its manifest.
// The host refuses to start a plugin whose hooks() and manifest disagree, and
// only dispatches a hook if its permission is declared and granted.
//
// A plugin is constructed when it's enabled and destroyed when disabled, so
// disabled plugins cost nothing. Allocate in start(), free in stop().
class Plugin
{
public:
    virtual ~Plugin() = default;

    [[nodiscard]] virtual Hooks hooks() const = 0;

    virtual void start(PluginContext &context) { Q_UNUSED(context); }
    virtual void stop() {}
    virtual void onSettingChanged(const QString &key) { Q_UNUSED(key); }
    // An "action" setting's button was clicked in Settings → Plugins
    virtual void onSettingAction(const QString &key) { Q_UNUSED(key); }

    // Hook::BeforeSend, Hook::BeforeEdit
    virtual void onBeforeSend(OutgoingMessage &message) { Q_UNUSED(message); }
    virtual void onBeforeEdit(OutgoingMessage &message) { Q_UNUSED(message); }

    // Hook::ShouldMentionOnReply. nullopt = no opinion.
    virtual std::optional<bool> shouldMentionOnReply(const ReplyContext &reply)
    {
        Q_UNUSED(reply);
        return std::nullopt;
    }

    // Hook::TransformCopiedMessageLink
    virtual QString transformCopiedMessageLink(const QString &url) { return url; }

    // Hook::TransformIncomingContent
    virtual void transformIncomingContent(IncomingMessage &message) { Q_UNUSED(message); }

    // Hook::ChatBarButtons. Called whenever the input is (re)built, including
    // after switching channels; call PluginContext::refreshChatBar() when the
    // buttons change.
    virtual QList<ChatBarButton> chatBarButtons(const ChatContext &chat)
    {
        Q_UNUSED(chat);
        return {};
    }
    virtual void onChatBarButtonClicked(const QString &id, const ChatContext &chat)
    {
        Q_UNUSED(id);
        Q_UNUSED(chat);
    }

    // Hook::MessageContextMenu. `message.imageUrl` is set when the menu was
    // opened on an image.
    virtual QList<MenuItem> messageContextMenuItems(const MessageContext &message)
    {
        Q_UNUSED(message);
        return {};
    }
    virtual void onMessageContextMenuAction(const QString &id, const MessageContext &message)
    {
        Q_UNUSED(id);
        Q_UNUSED(message);
    }

    // Hook::MessageHistory. Return true to keep a deleted message on screen
    // (marked deleted) instead of removing it.
    virtual bool keepDeletedMessage(const MessageContext &message)
    {
        Q_UNUSED(message);
        return false;
    }
    // A message's content was edited; `before` is the cached version.
    virtual void onMessageEdited(const MessageContext &before, const MessageContext &after)
    {
        Q_UNUSED(before);
        Q_UNUSED(after);
    }
    virtual std::optional<MessageAnnotation> annotateMessage(const MessageContext &message)
    {
        Q_UNUSED(message);
        return std::nullopt;
    }

    // Hook::MessageFilter. Return true to hide the message from the chat
    // (it also stops counting as unread).
    virtual bool shouldHideMessage(const MessageContext &message)
    {
        Q_UNUSED(message);
        return false;
    }
    // Hook::MessageFilter: keep the message visible but do not count it as
    // unread or notify about it.
    virtual bool shouldSilenceMessage(const MessageContext &message)
    {
        Q_UNUSED(message);
        return false;
    }

    // Hook::Typing. nullopt = no opinion; any `false` wins.
    virtual std::optional<bool> shouldSendTyping(const TypingContext &typing)
    {
        Q_UNUSED(typing);
        return std::nullopt;
    }
    // Hook::Typing: whether to show who else is typing above the input.
    virtual std::optional<bool> shouldShowTypers(const ChatContext &chat)
    {
        Q_UNUSED(chat);
        return std::nullopt;
    }
    // Hook::Typing: whether the member list shows who is typing (the dots on
    // their avatars).
    virtual std::optional<bool> shouldShowMemberListTypers(const ChatContext &chat)
    {
        Q_UNUSED(chat);
        return std::nullopt;
    }

    // Hook::LinkWarnings: false skips the "Are you sure?" before opening a link.
    virtual std::optional<bool> shouldConfirmLink(const QString &url)
    {
        Q_UNUSED(url);
        return std::nullopt;
    }

    // Hook::Notifications: false suppresses the desktop notification.
    virtual std::optional<bool> shouldNotify(const NotificationContext &notification)
    {
        Q_UNUSED(notification);
        return std::nullopt;
    }

    // Hook::AuthorNames: the name to show instead of the client's choice.
    virtual std::optional<QString> authorDisplayName(const AuthorContext &author)
    {
        Q_UNUSED(author);
        return std::nullopt;
    }

    // Hook::ChannelContextMenu: items for a server's or channel's menu.
    virtual QList<MenuItem> channelContextMenuItems(const ChannelContext &channel)
    {
        Q_UNUSED(channel);
        return {};
    }
    virtual void onChannelContextMenuAction(const QString &id, const ChannelContext &channel)
    {
        Q_UNUSED(id);
        Q_UNUSED(channel);
    }

    // Hook::ServerListButtons: buttons above the servers in the server list.
    virtual QList<ServerListButton> serverListButtons(const ServerListContext &list)
    {
        Q_UNUSED(list);
        return {};
    }
    virtual void onServerListButtonClicked(const QString &id, const ServerListContext &list)
    {
        Q_UNUSED(id);
        Q_UNUSED(list);
    }

    // Hook::MemberCounts: the numbers shown above the member list or in a
    // server's tooltip. An empty list shows nothing.
    virtual QList<CountBadge> memberCountBadges(const MemberCountContext &counts)
    {
        Q_UNUSED(counts);
        return {};
    }

    // Hook::UserDecorations: icons after a user's name.
    virtual QList<NameDecoration> nameDecorations(const DecorationContext &user)
    {
        Q_UNUSED(user);
        return {};
    }
    // A decoration with a clickId was clicked. A double click reports the
    // first click on its own, then one with doubleClick set.
    virtual void onNameDecorationClicked(const QString &clickId, const DecorationContext &user, bool doubleClick)
    {
        Q_UNUSED(clickId);
        Q_UNUSED(user);
        Q_UNUSED(doubleClick);
    }

    // Hook::ProfileImages: an avatar or banner in a profile was clicked.
    // Return true if the plugin handled it.
    virtual bool onProfileImageClicked(const ProfileImageContext &image)
    {
        Q_UNUSED(image);
        return false;
    }

    // Hook::ReactionUsers: the users to show on a reaction (nullopt shows none),
    // and live reaction changes
    virtual std::optional<ReactionUsers> reactionUsers(const ReactionContext &reaction)
    {
        Q_UNUSED(reaction);
        return std::nullopt;
    }
    virtual void onReactionChanged(const ReactionChange &change) { Q_UNUSED(change); }

    // Hook::MessageClicks: every mouse press in the chat, then the click on a
    // message (with its click count and the keys held)
    virtual void onChatMouseDown(qint64 timeMs) { Q_UNUSED(timeMs); }
    virtual void onMessageClicked(const MessageClickContext &click) { Q_UNUSED(click); }

    // Hook::VoiceStates: voice joins, moves and leaves, and text for the
    // voice participant list and the voice connection panel.
    virtual void onVoiceStateChanged(const VoiceStateChange &change) { Q_UNUSED(change); }
    virtual std::optional<VoiceLabel> voiceParticipantLabel(const VoiceParticipantContext &participant)
    {
        Q_UNUSED(participant);
        return std::nullopt;
    }
    virtual std::optional<QString> voiceConnectionText(const VoiceConnectionContext &connection)
    {
        Q_UNUSED(connection);
        return std::nullopt;
    }

    // Hook::KeyboardShortcuts: return true when the key was used
    virtual bool onShortcut(const ShortcutContext &shortcut)
    {
        Q_UNUSED(shortcut);
        return false;
    }

    // Hook::UnknownMentions: the mouse entered a user mention
    virtual void onUserMentionHovered(const MentionContext &mention) { Q_UNUSED(mention); }

    // Hook::ChannelTyping: an indicator next to a channel someone is typing in
    // (nullopt: none)
    virtual std::optional<TypingIndicator> channelTypingIndicator(const ChannelTypingContext &channel)
    {
        Q_UNUSED(channel);
        return std::nullopt;
    }

    // Hook::MessageAccessories: shown under a message (nullopt: nothing). A
    // plugin re-renders the message (MessageService) when this changes.
    virtual std::optional<MessageAccessory> messageAccessory(const MessageContext &message)
    {
        Q_UNUSED(message);
        return std::nullopt;
    }
    virtual void onMessageAccessoryAction(const QString &id, const MessageContext &message)
    {
        Q_UNUSED(id);
        Q_UNUSED(message);
    }

    // Hook::EmojiSuggestions: reorder (or add to) the emoji autocomplete's
    // suggestions, and items for an emoji right-clicked in the picker
    virtual void sortEmojiSuggestions(EmojiAutocompleteContext &context) { Q_UNUSED(context); }
    virtual QList<MenuItem> emojiContextMenuItems(const EmojiContext &emoji)
    {
        Q_UNUSED(emoji);
        return {};
    }
    virtual void onEmojiContextMenuAction(const QString &id, const EmojiContext &emoji)
    {
        Q_UNUSED(id);
        Q_UNUSED(emoji);
    }

    // Hook::DeviceSessions: how a device looks in Settings → Devices
    // (nullopt: as the client shows it), a click on one of its buttons, the
    // page being closed (the devices it showed), and Discord reporting your
    // sessions: first after the account connects, then whenever they change
    // (someone logged in or out somewhere)
    virtual std::optional<DeviceSessionView> deviceSessionView(Id accountId, const DeviceSession &session)
    {
        Q_UNUSED(accountId);
        Q_UNUSED(session);
        return std::nullopt;
    }
    virtual void onDeviceSessionButton(Id accountId, const QString &buttonId, const DeviceSession &session)
    {
        Q_UNUSED(accountId);
        Q_UNUSED(buttonId);
        Q_UNUSED(session);
    }
    virtual void onDevicesPageClosed(Id accountId, const QList<DeviceSession> &sessions)
    {
        Q_UNUSED(accountId);
        Q_UNUSED(sessions);
    }
    virtual void onDeviceSessionsChanged(Id accountId) { Q_UNUSED(accountId); }

    // Hook::Memberships: what an account belongs to, after it connects and
    // after each change, and each removal as it happens (servers that go
    // down for an outage aren't removals)
    virtual void onMembershipsChanged(const Memberships &memberships) { Q_UNUSED(memberships); }
    virtual void onRemoved(const Removal &removal) { Q_UNUSED(removal); }

    // Hook::Highlights: true treats a message from someone else as if it
    // mentioned you (the gold highlight, and a notification as for a mention).
    // Asked when the message is shown and when it could notify.
    virtual bool highlightMessage(const MessageContext &message)
    {
        Q_UNUSED(message);
        return false;
    }

    // Hook::ProfileTheme: the theme colours to show a profile with; nullopt
    // leaves it as it is
    virtual std::optional<ProfileTheme> profileTheme(const ProfileThemeContext &profile)
    {
        Q_UNUSED(profile);
        return std::nullopt;
    }

    // Hook::SlashCommands: one of the manifest's commands was run in the
    // message box. `options` holds each given option's value by name. A
    // returned text is sent as your message (Vencord's execute returning
    // { content }).
    virtual std::optional<QString> onSlashCommand(const QString &name, const QJsonObject &options,
                                                  const ChatContext &chat)
    {
        Q_UNUSED(name);
        Q_UNUSED(options);
        Q_UNUSED(chat);
        return std::nullopt;
    }

    // Hook::ChatInputContextMenu: items for the message box's right-click
    // menu (Vencord's "textarea-context")
    virtual QList<MenuItem> chatInputContextMenuItems(const ChatContext &chat)
    {
        Q_UNUSED(chat);
        return {};
    }
    virtual void onChatInputContextMenuAction(const QString &id, const ChatContext &chat)
    {
        Q_UNUSED(id);
        Q_UNUSED(chat);
    }

    // Hook::RoleContextMenu: items for a role's right-click menu
    virtual QList<MenuItem> roleContextMenuItems(const RoleContext &role)
    {
        Q_UNUSED(role);
        return {};
    }
    virtual void onRoleContextMenuAction(const QString &id, const RoleContext &role)
    {
        Q_UNUSED(id);
        Q_UNUSED(role);
    }

    // Hook::MessagePopover: buttons on a message's hover bar, before the
    // client's own (Vencord's addMessagePopoverButton), and a left or right
    // click on one. `svg` uses currentColor for the icon color.
    virtual QList<ChatBarButton> messagePopoverButtons(const MessageContext &message)
    {
        Q_UNUSED(message);
        return {};
    }
    virtual void onMessagePopoverClicked(const QString &id, const MessageContext &message, bool rightClick)
    {
        Q_UNUSED(id);
        Q_UNUSED(message);
        Q_UNUSED(rightClick);
    }

    // Hook::InboxTabs: tabs of the inbox after Mentions, a message removed
    // from one (its x) and "Clear All"
    virtual QList<InboxTab> inboxTabs(Id accountId)
    {
        Q_UNUSED(accountId);
        return {};
    }
    virtual void onInboxMessageRemoved(const QString &tabId, Id accountId, Id messageId)
    {
        Q_UNUSED(tabId);
        Q_UNUSED(accountId);
        Q_UNUSED(messageId);
    }
    virtual void onInboxCleared(const QString &tabId, Id accountId)
    {
        Q_UNUSED(tabId);
        Q_UNUSED(accountId);
    }

    // Hook::UserPanelButtons: buttons next to mute and deafen, a click on
    // one, and its right-click menu
    virtual QList<UserPanelButton> userPanelButtons(const UserPanelContext &panel)
    {
        Q_UNUSED(panel);
        return {};
    }
    virtual void onUserPanelButtonClicked(const QString &id, const UserPanelContext &panel)
    {
        Q_UNUSED(id);
        Q_UNUSED(panel);
    }
    virtual QList<MenuItem> userPanelButtonMenu(const QString &id, const UserPanelContext &panel)
    {
        Q_UNUSED(id);
        Q_UNUSED(panel);
        return {};
    }
    virtual void onUserPanelButtonMenuAction(const QString &id, const QString &itemId, const UserPanelContext &panel)
    {
        Q_UNUSED(id);
        Q_UNUSED(itemId);
        Q_UNUSED(panel);
    }

    // Hook::FriendsSections: tabs added to the Friends page, and the page
    // being opened (where Discord fetches relationships)
    virtual QList<FriendsSection> friendsSections(const FriendsContext &friends)
    {
        Q_UNUSED(friends);
        return {};
    }
    virtual void onFriendsPageOpened(const FriendsContext &friends) { Q_UNUSED(friends); }

    // Hook::DmPins: how an account's DM list is grouped (nullopt: as Discord
    // shows it), and its headers' clicks and menus. Category ids, or
    // DmSectionHeaderId for "Direct Messages".
    virtual std::optional<DmPinLayout> dmPinLayout(Id accountId)
    {
        Q_UNUSED(accountId);
        return std::nullopt;
    }
    virtual void onDmPinHeaderClicked(Id accountId, const QString &headerId)
    {
        Q_UNUSED(accountId);
        Q_UNUSED(headerId);
    }
    virtual QList<MenuItem> dmPinHeaderMenu(Id accountId, const QString &headerId)
    {
        Q_UNUSED(accountId);
        Q_UNUSED(headerId);
        return {};
    }
    virtual void onDmPinHeaderMenuAction(Id accountId, const QString &headerId, const QString &itemId)
    {
        Q_UNUSED(accountId);
        Q_UNUSED(headerId);
        Q_UNUSED(itemId);
    }

    // Hook::ProfileTabs: tabs added to a user's profile, and a click on one
    // of their rows
    virtual QList<ProfileTab> profileTabs(const UserContext &user)
    {
        Q_UNUSED(user);
        return {};
    }
    virtual void onProfileTabRowClicked(const QString &tabId, const QString &rowId, const UserContext &user)
    {
        Q_UNUSED(tabId);
        Q_UNUSED(rowId);
        Q_UNUSED(user);
    }

    // Hook::AttachmentButtons: buttons in a text file's preview (and its
    // full view), once its start is loaded
    virtual QList<ChatBarButton> attachmentButtons(const AttachmentContext &attachment)
    {
        Q_UNUSED(attachment);
        return {};
    }
    virtual void onAttachmentButtonClicked(const QString &id, const AttachmentContext &attachment)
    {
        Q_UNUSED(id);
        Q_UNUSED(attachment);
    }

    // Hook::HiddenChannels: show channels you can't see (nullopt: don't), and
    // what selecting one shows instead of its messages
    virtual std::optional<HiddenChannelDisplay> hiddenChannelDisplay() { return std::nullopt; }
    virtual std::optional<LockScreen> hiddenChannelLockScreen(const HiddenChannelContext &channel)
    {
        Q_UNUSED(channel);
        return std::nullopt;
    }

    // Hook::ProfileRoleButtons: small buttons next to "Roles" in a server
    // member's profile (`user.guildId` is set)
    virtual QList<ChatBarButton> profileRoleButtons(const UserContext &user)
    {
        Q_UNUSED(user);
        return {};
    }
    virtual void onProfileRoleButtonClicked(const QString &id, const UserContext &user)
    {
        Q_UNUSED(id);
        Q_UNUSED(user);
    }

    // Hook::UserContextMenu
    virtual QList<MenuItem> userContextMenuItems(const UserContext &user)
    {
        Q_UNUSED(user);
        return {};
    }
    virtual void onUserContextMenuAction(const QString &id, const UserContext &user)
    {
        Q_UNUSED(id);
        Q_UNUSED(user);
    }
};

} // namespace Plugins
} // namespace Qord
