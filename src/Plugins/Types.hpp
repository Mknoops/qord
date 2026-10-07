#pragma once

#include <QByteArray>
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
};

// A user that a context menu was opened for.
struct UserContext
{
    Id accountId = 0;
    Id guildId = 0; // 0 outside servers
    Id channelId = 0; // 0 when not opened from a channel
    Id userId = 0;
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

struct MenuItem
{
    QString id;
    QString label;
};

} // namespace Plugins
} // namespace Qord
