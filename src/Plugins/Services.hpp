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
    struct TextSection
    {
        QString title;
        QString text;
        bool monospace = false;
    };
    virtual ~UiService() = default;
    // A read-only dialog with copyable sections.
    virtual void showTextDialog(const QString &title, const QList<TextSection> &sections) = 0;
    // Needs the open_links permission (PluginContext enforces it).
    virtual void openUrl(const QUrl &url) = 0;
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
};

} // namespace Plugins
} // namespace Qord
