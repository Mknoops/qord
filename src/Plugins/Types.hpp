#pragma once

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

} // namespace Plugins
} // namespace Qord
