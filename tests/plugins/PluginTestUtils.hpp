#pragma once

// Helpers shared by plugin tests: a host holding a single real plugin
// (loaded through its real manifest), with fake client services.

#include "Plugins/PluginHost.hpp"

#include <QHash>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

namespace Qord {
namespace Plugins {
namespace Testing {

class FakeNetwork : public NetworkService
{
public:
    Handle get(const QUrl &url, Callback callback) override
    {
        const Handle h = ++next;
        requests.insert(h, { url, std::move(callback) });
        urls.append(url);
        return h;
    }
    void cancel(Handle handle) override
    {
        if (requests.remove(handle))
            ++cancelled;
    }
    // Completes the oldest outstanding request.
    bool respond(bool ok, const QByteArray &body, int status = 200)
    {
        if (requests.isEmpty())
            return false;
        auto it = requests.begin();
        for (auto i = requests.begin(); i != requests.end(); ++i)
            if (i.key() < it.key())
                it = i;
        Callback cb = it->callback;
        requests.erase(it);
        Reply r;
        r.ok = ok;
        r.status = status;
        r.body = body;
        r.error = ok ? QString() : QStringLiteral("simulated failure");
        cb(r);
        return true;
    }

    struct Pending
    {
        QUrl url;
        Callback callback;
    };
    QHash<Handle, Pending> requests;
    QList<QUrl> urls;
    int cancelled = 0;
    Handle next = 0;
};

class FakeMembers : public MemberDirectory
{
public:
    std::optional<QList<Id>> memberRoles(Id accountId, Id guildId, Id userId) const override
    {
        Q_UNUSED(accountId);
        ++lookups;
        auto it = roles.constFind(qMakePair(guildId, userId));
        if (it == roles.constEnd())
            return std::nullopt;
        return *it;
    }
    QHash<QPair<Id, Id>, QList<Id>> roles;
    mutable int lookups = 0;
};

class FakeClipboard : public ClipboardService
{
public:
    void setText(const QString &t) override { text = t; ++writes; }
    QString text;
    int writes = 0;
};

class FakeRelationships : public RelationshipDirectory
{
public:
    bool isBlocked(Id accountId, Id userId) const override
    {
        Q_UNUSED(accountId);
        return blocked.contains(userId);
    }
    bool isIgnored(Id accountId, Id userId) const override
    {
        Q_UNUSED(accountId);
        return ignored.contains(userId);
    }
    QSet<Id> blocked;
    QSet<Id> ignored;
};

class FakeUi : public UiService
{
public:
    void showTextDialog(const QString &title, const QList<TextSection> &sections) override
    {
        dialogTitle = title;
        dialogSections = sections;
        ++dialogs;
    }
    void openUrl(const QUrl &url) override { opened.append(url); }
    void insertIntoMessageBox(const QString &text) override { inserted.append(text); }
    void pickTimestamp(std::function<void(const QString &)> done) override { pendingPick = std::move(done); }
    void openPluginSettings(const QString &pluginId) override { settingsOpened.append(pluginId); }
    void promptText(const QString &title, const QString &, const QString &initial, int,
                    std::function<void(const QString &)> done) override
    {
        promptTitle = title;
        promptInitial = initial;
        pendingPrompt = std::move(done);
    }
    QString promptTitle;
    QString promptInitial;
    std::function<void(const QString &)> pendingPrompt;
    QStringList settingsOpened;
    QStringList inserted;
    std::function<void(const QString &)> pendingPick;
    QString dialogTitle;
    QList<TextSection> dialogSections;
    int dialogs = 0;
    QList<QUrl> opened;
};

class FakeMessages : public MessageService
{
public:
    void refreshMessage(Id, Id, Id messageId) override { refreshed.append(messageId); }
    void removeMessage(Id, Id, Id messageId) override { removed.append(messageId); }
    void refreshFilters() override { ++filterRefreshes; }
    QList<Id> refreshed;
    QList<Id> removed;
    int filterRefreshes = 0;
};

class FakeMessageActions : public MessageActionService
{
public:
    void setMessageFlags(Id, Id channelId, Id messageId, quint64 flags) override
    {
        calls.append({ channelId, messageId, flags });
    }
    struct Call
    {
        Id channelId;
        Id messageId;
        quint64 flags;
    };
    QList<Call> calls;
};

class FakeDisplayOptions : public DisplayOptionsService
{
public:
    void setRelativeTimeRoundsDown(bool floor) override { roundsDown = floor; }
    bool roundsDown = false;
};

using RegisterFn = void (*)(Registry &);

// Owns a host with one registered plugin.
struct SinglePluginHost
{
    explicit SinglePluginHost(RegisterFn registerPlugin)
    {
        Registry registry;
        registerPlugin(registry);
        Services services;
        services.network = &network;
        services.members = &members;
        services.clipboard = &clipboard;
        services.relationships = &relationships;
        services.ui = &ui;
        services.messages = &messages;
        services.messageActions = &messageActions;
        services.displayOptions = &displayOptions;
        host = std::make_unique<PluginHost>(registry, &storage, services, dataDir.path());
        host->load();
        id = host->pluginIds().value(0);
    }

    // Grants everything the manifest declares and enables the plugin.
    bool enableWithAllPermissions()
    {
        host->setGranted(id, info()->manifest.permissions);
        QString error;
        const bool ok = host->setEnabled(id, true, &error);
        if (!ok)
            qWarning() << "enable failed:" << error;
        return ok;
    }

    const PluginHost::PluginInfo *info() const { return host->info(id); }

    QTemporaryDir dataDir;
    MemoryStorage storage;
    FakeNetwork network;
    FakeMembers members;
    FakeClipboard clipboard;
    FakeRelationships relationships;
    FakeUi ui;
    FakeMessages messages;
    FakeMessageActions messageActions;
    FakeDisplayOptions displayOptions;
    std::unique_ptr<PluginHost> host;
    QString id;
};

} // namespace Testing
} // namespace Plugins
} // namespace Qord
