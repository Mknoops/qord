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
