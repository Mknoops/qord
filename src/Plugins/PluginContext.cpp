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
        return track([&](Callback wrapped) { return inner->get(url, std::move(wrapped)); }, std::move(callback));
    }

    Handle post(const QUrl &url, const Headers &headers, const QByteArray &body, Callback callback) override
    {
        return track([&](Callback wrapped) { return inner->post(url, headers, body, std::move(wrapped)); },
                     std::move(callback));
    }

    void cancel(Handle handle) override
    {
        if (pending.remove(handle))
            inner->cancel(handle);
    }

private:
    // Drops the reply once the plugin stopped; cancels what is still pending then
    Handle track(const std::function<Handle(Callback)> &send, Callback callback)
    {
        struct Request
        {
            Handle handle = 0;
            bool done = false;
        };
        auto request = std::make_shared<Request>();
        std::weak_ptr<bool> weakAlive = alive;
        request->handle = send([this, weakAlive, request, callback = std::move(callback)](const Reply &reply) {
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
    void showToast(const QString &text, ToastType type) override { inner->showToast(text, type); }
    bool confirm(Confirmation &confirmation) override { return inner->confirm(confirmation); }
    void attachFile(const QString &fileName, const QByteArray &data, const QString &mimeType) override
    {
        inner->attachFile(fileName, data, mimeType);
    }
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
    void showPermissions(const PermissionsView &view) override { inner->showPermissions(view); }
    void refreshLockScreen() override { inner->refreshLockScreen(); }
    void openChannel(Id accountId, Id channelId) override { inner->openChannel(accountId, channelId); }
    void showFormDialog(const QString &title, const QList<FormField> &fields, const QString &confirmText,
                        std::function<void(const QHash<QString, QString> &)> done) override
    {
        std::weak_ptr<bool> weak = alive;
        inner->showFormDialog(title, fields, confirmText,
                              [weak, done = std::move(done)](const QHash<QString, QString> &values) {
                                  if (auto a = weak.lock(); a && *a)
                                      done(values);
                              });
    }
    void showForm(const QString &title, const QList<FormField> &fields,
                  std::function<void(const QString &, const QString &)> changed) override
    {
        std::weak_ptr<bool> weak = alive;
        inner->showForm(title, fields, [weak, changed = std::move(changed)](const QString &key, const QString &value) {
            if (auto a = weak.lock(); a && *a)
                changed(key, value);
        });
    }
    void cancelReply() override { inner->cancelReply(); }
    void cancelEditing() override { inner->cancelEditing(); }
    void focusMessageBox() override { inner->focusMessageBox(); }
    void revealMessage(Id messageId) override { inner->revealMessage(messageId); }
    void showServerInfo(const ServerInfoView &view) override { inner->showServerInfo(view); }
    void openUserProfile(Id accountId, Id guildId, Id userId) override
    {
        inner->openUserProfile(accountId, guildId, userId);
    }
    void refreshAttachments(Id accountId, Id channelId, Id messageId) override
    {
        inner->refreshAttachments(accountId, channelId, messageId);
    }
    // The popout's buttons may be clicked after the plugin stopped
    void showMemberPermissions(const MemberPermissionsView &view) override
    {
        MemberPermissionsView guardedView = view;
        std::weak_ptr<bool> weak = alive;
        auto guard = [weak](std::function<void()> f) -> std::function<void()> {
            if (!f)
                return {};
            return [weak, f = std::move(f)]() {
                if (auto a = weak.lock(); a && *a)
                    f();
            };
        };
        guardedView.toggleSort = guard(view.toggleSort);
        guardedView.showRoleDetails = guard(view.showRoleDetails);
        inner->showMemberPermissions(guardedView);
    }
    void startEditing(const MessageContext &message) override { inner->startEditing(message); }
    void startReply(const MessageContext &message, bool shiftHeld, std::optional<bool> mention) override
    {
        inner->startReply(message, shiftHeld, mention);
    }
    void openImage(const QUrl &url) override
    {
        static const QStringList hosts = { QStringLiteral("cdn.discordapp.com"), QStringLiteral("media.discordapp.net") };
        if (url.scheme() == QLatin1String("https") && hosts.contains(url.host()))
            inner->openImage(url);
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

namespace {
class ScopedReactions : public ReactionService
{
public:
    explicit ScopedReactions(ReactionService *inner) : inner(inner), alive(std::make_shared<bool>(true)) {}
    ~ScopedReactions() override { *alive = false; }
    void fetchReactionUsers(Id accountId, Id channelId, Id messageId, const QString &emojiKey, int type,
                            Callback done) override
    {
        std::weak_ptr<bool> weak = alive;
        inner->fetchReactionUsers(accountId, channelId, messageId, emojiKey, type,
                                  [weak, done = std::move(done)](bool ok, const QList<Id> &users) {
                                      if (auto a = weak.lock(); a && *a)
                                          done(ok, users);
                                  });
    }
    void reactionUsersChanged(Id accountId, Id channelId, Id messageId) override
    {
        inner->reactionUsersChanged(accountId, channelId, messageId);
    }

private:
    ReactionService *inner;
    std::shared_ptr<bool> alive;
};
} // namespace

ExpressionService *PluginContext::expressions() const
{
    if (!granted.testFlag(Permission::ReadExpressions))
        return nullptr;
    return services.expressions;
}

namespace {
class ScopedUsers : public UserLookupService
{
public:
    explicit ScopedUsers(UserLookupService *inner) : inner(inner), alive(std::make_shared<bool>(true)) {}
    ~ScopedUsers() override { *alive = false; }
    bool isKnown(Id accountId, Id userId) const override { return inner->isKnown(accountId, userId); }
    void fetchUser(Id accountId, Id userId, Callback done) override
    {
        std::weak_ptr<bool> weak = alive;
        inner->fetchUser(accountId, userId, [weak, done = std::move(done)](const Result &result) {
            if (auto a = weak.lock(); a && *a)
                done(result);
        });
    }
    void refreshMessage(Id accountId, Id channelId, Id messageId) override
    {
        inner->refreshMessage(accountId, channelId, messageId);
    }

private:
    UserLookupService *inner;
    std::shared_ptr<bool> alive;
};
} // namespace

namespace {
class ScopedMessageLookup : public MessageLookupService
{
public:
    explicit ScopedMessageLookup(MessageLookupService *inner) : inner(inner), alive(std::make_shared<bool>(true)) {}
    ~ScopedMessageLookup() override { *alive = false; }
    std::optional<LinkedMessage> message(Id accountId, Id channelId, Id messageId) override
    {
        return inner->message(accountId, channelId, messageId);
    }
    void fetchMessage(Id accountId, Id channelId, Id messageId, std::function<void(bool)> done) override
    {
        std::weak_ptr<bool> weak = alive;
        inner->fetchMessage(accountId, channelId, messageId, [weak, done = std::move(done)](bool found) {
            if (auto a = weak.lock(); a && *a)
                done(found);
        });
    }
    std::optional<LinkedChannel> channel(Id accountId, Id channelId) override
    {
        return inner->channel(accountId, channelId);
    }

private:
    MessageLookupService *inner;
    std::shared_ptr<bool> alive;
};
} // namespace

MessageLookupService *PluginContext::messageLookup()
{
    if (!granted.testFlag(Permission::FetchMessages) || !services.messageLookup)
        return nullptr;
    if (!scopedMessageLookup)
        scopedMessageLookup = std::make_unique<ScopedMessageLookup>(services.messageLookup);
    return scopedMessageLookup.get();
}

UserLookupService *PluginContext::users()
{
    if (!granted.testFlag(Permission::FetchUsers) || !services.users)
        return nullptr;
    if (!scopedUsers)
        scopedUsers = std::make_unique<ScopedUsers>(services.users);
    return scopedUsers.get();
}

AudioOptionsService *PluginContext::audio() const
{
    if (!granted.testFlag(Permission::ControlAudio))
        return nullptr;
    return services.audio;
}

const PrivateChannelDirectory *PluginContext::privateChannels() const
{
    if (!granted.testFlag(Permission::ReadDms))
        return nullptr;
    return services.privateChannels;
}

GuildDirectory *PluginContext::guilds() const
{
    if (!granted.testFlag(Permission::ReadServerPermissions))
        return nullptr;
    return services.guilds;
}

ReactionService *PluginContext::reactions()
{
    if (!granted.testFlag(Permission::ReadReactions) || !services.reactions)
        return nullptr;
    if (!scopedReactions)
        scopedReactions = std::make_unique<ScopedReactions>(services.reactions);
    return scopedReactions.get();
}

UiService *PluginContext::ui()
{
    if (!granted.testFlag(Permission::AddUiElements) || !services.ui)
        return nullptr;
    if (!scopedUi)
        scopedUi = std::make_unique<ScopedUi>(services.ui, granted.testFlag(Permission::OpenLinks));
    return scopedUi.get();
}

ReadStateService *PluginContext::readStates() const
{
    if (!granted.testFlag(Permission::MarkRead))
        return nullptr;
    return services.readStates;
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
    if (!granted.testFlag(Permission::LogMessages) && !granted.testFlag(Permission::HideMessages) &&
        !granted.testFlag(Permission::ModifyDisplayedMessages))
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
