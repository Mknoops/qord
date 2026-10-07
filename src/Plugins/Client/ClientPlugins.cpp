#include "ClientPlugins.hpp"

#include "Core/ClientInstance.hpp"
#include "Core/Logging.hpp"
#include "Core/NetworkRequest.hpp"
#include "Core/Session.hpp"
#include "Core/UserManager.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QStandardPaths>

namespace Qord {
namespace Plugins {

QtNetworkService::QtNetworkService(QObject *parent)
    : QObject(parent)
{
}

QtNetworkService::~QtNetworkService()
{
    for (const auto &reply : std::as_const(replies))
        if (reply) {
            reply->disconnect(this);
            reply->abort();
        }
}

NetworkService::Handle QtNetworkService::get(const QUrl &url, Callback callback)
{
    const Handle handle = ++next;
    // Plugins only fetch over HTTPS
    if (url.scheme() != QLatin1String("https")) {
        QMetaObject::invokeMethod(this, [callback = std::move(callback)] {
            Reply r;
            r.error = QStringLiteral("only https URLs are allowed");
            callback(r);
        }, Qt::QueuedConnection);
        return handle;
    }
    if (!nam)
        nam = new QNetworkAccessManager(this);

    QNetworkRequest request = Acheron::Core::networkRequest(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setMaximumRedirectsAllowed(5);
    request.setTransferTimeout(60'000);
    QNetworkReply *reply = nam->get(request);
    replies.insert(handle, reply);

    connect(reply, &QNetworkReply::downloadProgress, this, [reply](qint64 received, qint64) {
        if (received > MaxResponseBytes)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, handle, reply, callback = std::move(callback)] {
        reply->deleteLater();
        if (!replies.remove(handle))
            return; // cancelled
        Reply r;
        r.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        r.ok = reply->error() == QNetworkReply::NoError && r.status >= 200 && r.status < 300;
        if (r.ok)
            r.body = reply->readAll();
        else
            r.error = reply->errorString();
        callback(r);
    });
    return handle;
}

void QtNetworkService::cancel(Handle handle)
{
    QPointer<QNetworkReply> reply = replies.take(handle);
    if (reply)
        reply->abort();
}

std::optional<QList<Id>> SessionMemberDirectory::memberRoles(Id accountId, Id guildId, Id userId) const
{
    Acheron::Core::ClientInstance *instance = session->client(Acheron::Core::Snowflake(accountId));
    if (!instance || !instance->users())
        return std::nullopt;
    auto roles = instance->users()->getMemberRoles(Acheron::Core::Snowflake(guildId), Acheron::Core::Snowflake(userId));
    if (!roles)
        return std::nullopt;
    QList<Id> out;
    out.reserve(roles->size());
    for (const auto &role : *roles)
        out.append(quint64(role));
    return out;
}

static QString pluginDataDirectory()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("plugins"));
}

ClientPlugins::ClientPlugins(Acheron::Core::Session *session)
    : members(session)
{
    QElapsedTimer timer;
    timer.start();

    Registry registry;
    registerBuiltinPlugins(registry);
    storage = std::make_unique<FileStorage>(pluginDataDirectory());
    Services services;
    services.network = &network;
    services.members = &members;
    pluginHost = std::make_unique<PluginHost>(registry, storage.get(), services,
                                              QDir(pluginDataDirectory()).filePath(QStringLiteral("data")));
    pluginHost->load();
    PluginHost::setInstance(pluginHost.get());

    int running = 0;
    for (const QString &id : pluginHost->pluginIds())
        running += pluginHost->isRunning(id) ? 1 : 0;
    qCInfo(LogCore) << "Plugins:" << pluginHost->pluginIds().size() << "available," << running << "running, loaded in"
                    << timer.elapsed() << "ms";
}

ClientPlugins::~ClientPlugins()
{
    PluginHost::setInstance(nullptr);
    pluginHost->unload();
}

const SettingsWidgetRegistry &ClientPlugins::settingsWidgets()
{
    static const SettingsWidgetRegistry registry = [] {
        SettingsWidgetRegistry r;
        registerBuiltinPluginWidgets(r);
        return r;
    }();
    return registry;
}

} // namespace Plugins
} // namespace Qord
