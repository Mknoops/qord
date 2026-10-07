#include "PluginContext.hpp"

#include <QDir>
#include <QLoggingCategory>

#include <memory>

Q_LOGGING_CATEGORY(LogPlugin, "qord.plugin")

namespace Qord {
namespace Plugins {

// Forwards to the real service and remembers outstanding requests, so a
// stopping plugin can't receive callbacks after it is gone.
class ScopedNetwork : public NetworkService
{
public:
    explicit ScopedNetwork(NetworkService *inner)
        : inner(inner), alive(std::make_shared<bool>(true))
    {
    }

    ~ScopedNetwork() override
    {
        *alive = false;
        for (Handle h : pending)
            inner->cancel(h);
    }

    Handle get(const QUrl &url, Callback callback) override
    {
        struct Request
        {
            Handle handle = 0;
            bool done = false;
        };
        auto request = std::make_shared<Request>();
        std::weak_ptr<bool> weakAlive = alive;
        request->handle = inner->get(url, [this, weakAlive, request, callback = std::move(callback)](const Reply &reply) {
            auto isAlive = weakAlive.lock();
            if (!isAlive || !*isAlive)
                return;
            request->done = true;
            pending.remove(request->handle);
            callback(reply);
        });
        if (!request->done) // the service may answer synchronously
            pending.insert(request->handle);
        return request->handle;
    }

    void cancel(Handle handle) override
    {
        if (pending.remove(handle))
            inner->cancel(handle);
    }

private:
    NetworkService *inner;
    std::shared_ptr<bool> alive;
    QSet<Handle> pending;
};

PluginContext::PluginContext(QString pluginId, PluginSettings *settings, Permissions granted,
                             const Services &services, QString dataDirectory)
    : id(std::move(pluginId)), pluginSettings(settings), granted(granted), services(services),
      dataDir(std::move(dataDirectory))
{
}

PluginContext::~PluginContext() = default;

NetworkService *PluginContext::network()
{
    if (!granted.testFlag(Permission::Network) || !services.network)
        return nullptr;
    if (!scopedNetwork)
        scopedNetwork = std::make_unique<ScopedNetwork>(services.network);
    return scopedNetwork.get();
}

const MemberDirectory *PluginContext::members() const
{
    if (!granted.testFlag(Permission::ReadGuildMembers))
        return nullptr;
    return services.members;
}

QString PluginContext::dataDirectory() const
{
    QDir().mkpath(dataDir);
    return dataDir;
}

void PluginContext::logInfo(const QString &message) const
{
    qCInfo(LogPlugin).noquote() << QStringLiteral("[%1]").arg(id) << message;
}

void PluginContext::logWarning(const QString &message) const
{
    qCWarning(LogPlugin).noquote() << QStringLiteral("[%1]").arg(id) << message;
}

} // namespace Plugins
} // namespace Qord
