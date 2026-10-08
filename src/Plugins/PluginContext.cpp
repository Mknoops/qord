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

ClipboardService *PluginContext::clipboard() const
{
    if (!granted.testFlag(Permission::WriteClipboard))
        return nullptr;
    return services.clipboard;
}

const RelationshipDirectory *PluginContext::relationships() const
{
    if (!granted.testFlag(Permission::ReadRelationships))
        return nullptr;
    return services.relationships;
}

namespace {
// Dialogs pass through; openUrl only with open_links, and only http(s)
class ScopedUi : public UiService
{
public:
    ScopedUi(UiService *inner, bool canOpen)
        : inner(inner), canOpen(canOpen), alive(std::make_shared<bool>(true))
    {
    }
    ~ScopedUi() override { *alive = false; }
    void insertIntoMessageBox(const QString &text) override { inner->insertIntoMessageBox(text); }
    void openPluginSettings(const QString &pluginId) override { inner->openPluginSettings(pluginId); }
    void promptText(const QString &title, const QString &label, const QString &initial, int maxLength,
                    std::function<void(const QString &)> done) override
    {
        std::weak_ptr<bool> weak = alive;
        inner->promptText(title, label, initial, maxLength, [weak, done = std::move(done)](const QString &text) {
            if (auto a = weak.lock(); a && *a)
                done(text);
        });
    }
    // The answer may come after the plugin stopped; drop it then
    void pickTimestamp(std::function<void(const QString &)> done) override
    {
        std::weak_ptr<bool> weak = alive;
        inner->pickTimestamp([weak, done = std::move(done)](const QString &markup) {
            if (auto a = weak.lock(); a && *a)
                done(markup);
        });
    }
    void showTextDialog(const QString &title, const QList<TextSection> &sections) override
    {
        inner->showTextDialog(title, sections);
    }
    void openUrl(const QUrl &url) override
    {
        if (canOpen && (url.scheme() == QLatin1String("https") || url.scheme() == QLatin1String("http")))
            inner->openUrl(url);
    }

private:
    UiService *inner;
    bool canOpen;
    std::shared_ptr<bool> alive;
};
} // namespace

UiService *PluginContext::ui()
{
    if (!granted.testFlag(Permission::AddUiElements) || !services.ui)
        return nullptr;
    if (!scopedUi)
        scopedUi = std::make_unique<ScopedUi>(services.ui, granted.testFlag(Permission::OpenLinks));
    return scopedUi.get();
}

DisplayOptionsService *PluginContext::displayOptions() const
{
    if (!granted.testFlag(Permission::ModifyDisplayedMessages))
        return nullptr;
    return services.displayOptions;
}

MessageActionService *PluginContext::messageActions() const
{
    if (!granted.testFlag(Permission::EditMessages))
        return nullptr;
    return services.messageActions;
}

MessageService *PluginContext::messages() const
{
    if (!granted.testFlag(Permission::LogMessages) && !granted.testFlag(Permission::HideMessages))
        return nullptr;
    return services.messages;
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
