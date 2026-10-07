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

struct Services
{
    NetworkService *network = nullptr;
    MemberDirectory *members = nullptr;
};

} // namespace Plugins
} // namespace Qord
