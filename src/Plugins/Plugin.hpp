#pragma once

#include "Permission.hpp"
#include "Types.hpp"

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
