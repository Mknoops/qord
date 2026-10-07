#include "PluginHost.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

#include <exception>

Q_LOGGING_CATEGORY(LogPluginHost, "qord.plugins.host")

namespace Qord {
namespace Plugins {

namespace {
PluginHost *globalHost = nullptr;
const QString StateKey = QStringLiteral("host-state");

template<typename Fn>
void guarded(const QString &id, const char *what, Fn &&fn)
{
    try {
        fn();
    } catch (const std::exception &e) {
        qCWarning(LogPluginHost) << "Plugin" << id << "threw in" << what << ":" << e.what();
    } catch (...) {
        qCWarning(LogPluginHost) << "Plugin" << id << "threw in" << what;
    }
}
} // namespace

PluginHost::PluginHost(Registry registry, Storage *stateStorage, Services services, QString dataDirectory,
                       QObject *parent)
    : QObject(parent), storage(stateStorage), services(services), dataDir(std::move(dataDirectory))
{
    for (const auto &descriptor : registry.all()) {
        Entry entry;
        entry.descriptor = descriptor;

        QByteArray manifest = descriptor.manifest;
        QString error;
        if (manifest.isEmpty()) {
            QFile file(descriptor.manifestPath);
            if (file.open(QIODevice::ReadOnly))
                manifest = file.readAll();
        }
        if (manifest.isEmpty()) {
            error = QStringLiteral("cannot read manifest %1").arg(descriptor.manifestPath);
        } else if (!PluginManifest::parse(manifest, entry.info.manifest, error)) {
            error = QStringLiteral("invalid manifest %1: %2").arg(descriptor.manifestPath, error);
        }

        QString id = entry.info.manifest.id;
        if (!error.isEmpty()) {
            id = descriptor.manifestPath;
            entry.info.manifest.id = id;
            entry.info.manifest.name = id;
        } else if (entries.count(id)) {
            error = QStringLiteral("duplicate plugin id %1").arg(id);
            id = descriptor.manifestPath;
        }
        if (!error.isEmpty()) {
            qCWarning(LogPluginHost).noquote() << error;
            entry.manifestError = error;
            entry.info.loadError = error;
        }
        entries.emplace(id, std::move(entry));
    }
}

PluginHost::~PluginHost()
{
    unload();
    if (globalHost == this)
        globalHost = nullptr;
}

PluginHost *PluginHost::instance()
{
    return globalHost;
}

void PluginHost::setInstance(PluginHost *host)
{
    globalHost = host;
}

void PluginHost::load()
{
    loadState();
    for (auto &[id, entry] : entries) {
        if (!entry.info.enabled || entry.info.running)
            continue;
        QString error;
        if (!start(entry, &error)) {
            qCWarning(LogPluginHost).noquote() << "Could not start plugin" << id << ":" << error;
            entry.info.loadError = error;
        }
    }
    rebuildTables();
}

void PluginHost::unload()
{
    for (auto &[id, entry] : entries)
        stop(entry);
    rebuildTables();
}

QStringList PluginHost::pluginIds() const
{
    QStringList ids;
    for (const auto &[id, entry] : entries)
        ids.append(id);
    return ids;
}

const PluginHost::PluginInfo *PluginHost::info(const QString &id) const
{
    const Entry *e = find(id);
    return e ? &e->info : nullptr;
}

PluginHost::Entry *PluginHost::find(const QString &id)
{
    auto it = entries.find(id);
    return it == entries.end() ? nullptr : &it->second;
}

const PluginHost::Entry *PluginHost::find(const QString &id) const
{
    auto it = entries.find(id);
    return it == entries.end() ? nullptr : &it->second;
}

bool PluginHost::isEnabled(const QString &id) const
{
    const Entry *e = find(id);
    return e && e->info.enabled;
}

bool PluginHost::isRunning(const QString &id) const
{
    const Entry *e = find(id);
    return e && e->info.running;
}

PluginSettings *PluginHost::settings(const QString &id)
{
    Entry *e = find(id);
    if (!e || !e->manifestError.isEmpty())
        return nullptr;
    if (!e->settings) {
        e->settings = std::make_unique<PluginSettings>(id, e->info.manifest.settings, storage);
        e->settings->load();
        e->settings->setChangeCallback([this, e](const QString &key) { onSettingChanged(*e, key); });
    }
    return e->settings.get();
}

void PluginHost::onSettingChanged(Entry &entry, const QString &key)
{
    if (entry.plugin)
        guarded(entry.info.manifest.id, "onSettingChanged", [&] { entry.plugin->onSettingChanged(key); });
    if (entry.info.running)
        notifyHooksChanged(entry.info.manifest.hooks);
}

void PluginHost::notifyHooksChanged(Hooks hooks)
{
    if (hooks.testFlag(Hook::TransformIncomingContent))
        emit displayTransformsChanged();
    if (hooks.testFlag(Hook::ChatBarButtons))
        emit chatBarChanged();
}

bool PluginHost::start(Entry &entry, QString *error)
{
    if (entry.info.running)
        return true;
    const PluginManifest &m = entry.info.manifest;
    if (!entry.manifestError.isEmpty()) {
        if (error)
            *error = entry.manifestError;
        return false;
    }

    std::unique_ptr<Plugin> plugin;
    guarded(m.id, "constructor", [&] { plugin = entry.descriptor.create ? entry.descriptor.create() : nullptr; });
    if (!plugin) {
        if (error)
            *error = QStringLiteral("plugin %1 could not be constructed").arg(m.id);
        return false;
    }
    if (plugin->hooks() != m.hooks) {
        if (error)
            *error = QStringLiteral("plugin %1 implements hooks that differ from its manifest").arg(m.id);
        return false;
    }

    PluginSettings *s = settings(m.id);
    entry.context = std::make_unique<PluginContext>(m.id, s, entry.info.granted & m.permissions, services,
                                                    QDir(dataDir).filePath(m.id));
    entry.context->onChatBarChanged = [this] { emit chatBarChanged(); };
    bool ok = true;
    try {
        plugin->start(*entry.context);
    } catch (const std::exception &e) {
        ok = false;
        if (error)
            *error = QStringLiteral("plugin %1 failed to start: %2").arg(m.id, QString::fromUtf8(e.what()));
    } catch (...) {
        ok = false;
        if (error)
            *error = QStringLiteral("plugin %1 failed to start").arg(m.id);
    }
    if (!ok) {
        entry.context.reset();
        return false;
    }
    entry.plugin = std::move(plugin);
    entry.info.running = true;
    entry.info.loadError.clear();
    return true;
}

void PluginHost::stop(Entry &entry)
{
    if (!entry.plugin)
        return;
    guarded(entry.info.manifest.id, "stop", [&] { entry.plugin->stop(); });
    entry.plugin.reset();
    entry.context.reset(); // cancels outstanding requests
    entry.info.running = false;
}

bool PluginHost::setEnabled(const QString &id, bool enable, QString *error)
{
    Entry *e = find(id);
    if (!e) {
        if (error)
            *error = QStringLiteral("unknown plugin %1").arg(id);
        return false;
    }
    if (e->info.enabled == enable && e->info.running == enable)
        return true;

    Hooks changedHooks;
    if (enable) {
        // Dependencies first, with cycle detection
        QStringList order, visiting;
        std::function<bool(const QString &)> visit = [&](const QString &pid) -> bool {
            if (order.contains(pid))
                return true;
            if (visiting.contains(pid)) {
                if (error)
                    *error = QStringLiteral("dependency cycle involving %1").arg(pid);
                return false;
            }
            Entry *pe = find(pid);
            if (!pe) {
                if (error)
                    *error = QStringLiteral("%1 depends on unknown plugin %2").arg(id, pid);
                return false;
            }
            visiting.append(pid);
            for (const auto &dep : pe->info.manifest.dependencies)
                if (!visit(dep))
                    return false;
            visiting.removeAll(pid);
            order.append(pid);
            return true;
        };
        if (!visit(id))
            return false;

        QStringList started;
        for (const auto &pid : order) {
            Entry *pe = find(pid);
            const bool wasRunning = pe->info.running;
            QString startError;
            if (!start(*pe, &startError)) {
                for (const auto &undo : started)
                    stop(*find(undo));
                pe->info.loadError = startError;
                if (error)
                    *error = startError;
                rebuildTables();
                emit pluginStateChanged(id);
                return false;
            }
            if (!wasRunning)
                started.append(pid);
        }
        for (const auto &pid : order) {
            Entry *pe = find(pid);
            pe->info.enabled = true;
            changedHooks |= pe->info.manifest.hooks;
        }
    } else {
        // Dependents first
        QStringList toDisable { id };
        for (int i = 0; i < toDisable.size(); ++i)
            for (auto &[pid, pe] : entries)
                if (pe.info.enabled && pe.info.manifest.dependencies.contains(toDisable[i]) && !toDisable.contains(pid))
                    toDisable.append(pid);
        for (auto it = toDisable.crbegin(); it != toDisable.crend(); ++it) {
            Entry *pe = find(*it);
            if (pe->info.running)
                changedHooks |= pe->info.manifest.hooks;
            stop(*pe);
            pe->info.enabled = false;
        }
    }

    rebuildTables();
    saveState();
    emit pluginStateChanged(id);
    notifyHooksChanged(changedHooks);
    return true;
}

void PluginHost::setGranted(const QString &id, Permissions permissions)
{
    Entry *e = find(id);
    if (!e)
        return;
    const Permissions newGrants = permissions & e->info.manifest.permissions;
    if (newGrants == e->info.granted)
        return;
    e->info.granted = newGrants;
    if (e->info.running) {
        stop(*e);
        QString error;
        if (!start(*e, &error))
            e->info.loadError = error;
    }
    rebuildTables();
    saveState();
    emit pluginStateChanged(id);
    notifyHooksChanged(e->info.manifest.hooks);
}

Permissions PluginHost::granted(const QString &id) const
{
    const Entry *e = find(id);
    return e ? e->info.granted : Permissions();
}

void PluginHost::rebuildTables()
{
    for (auto &t : tables)
        t.clear();
    for (auto &t : tableIds)
        t.clear();
    for (auto &[id, entry] : entries) {
        if (!entry.info.running || !entry.plugin)
            continue;
        for (Hook hook : allHooks()) {
            if (!entry.info.manifest.hooks.testFlag(hook))
                continue;
            if (!entry.info.granted.testFlag(requiredPermission(hook)))
                continue;
            tables[hookIndex(hook)].push_back(entry.plugin.get());
            tableIds[hookIndex(hook)].push_back(id);
        }
    }
}

QStringList PluginHost::pluginsForHook(Hook hook) const
{
    QStringList out;
    for (const auto &id : tableIds[hookIndex(hook)])
        out.append(id);
    return out;
}

void PluginHost::loadState()
{
    QJsonObject state;
    if (storage) {
        const QByteArray data = storage->read(StateKey);
        if (!data.isEmpty())
            state = QJsonDocument::fromJson(data).object().value(QLatin1String("plugins")).toObject();
    }
    for (auto &[id, entry] : entries) {
        const PluginManifest &m = entry.info.manifest;
        if (!state.contains(id)) {
            // First run: plugins that ship enabled get what they declare
            entry.info.enabled = m.enabledByDefault && entry.manifestError.isEmpty();
            entry.info.granted = m.enabledByDefault ? m.permissions : Permissions();
            continue;
        }
        const QJsonObject s = state.value(id).toObject();
        entry.info.enabled = s.value(QLatin1String("enabled")).toBool() && entry.manifestError.isEmpty();
        Permissions grants;
        for (const auto &p : s.value(QLatin1String("granted")).toArray())
            if (auto perm = permissionFromString(p.toString()))
                grants |= *perm;
        entry.info.granted = grants & m.permissions;
    }
}

void PluginHost::saveState() const
{
    if (!storage)
        return;
    QJsonObject plugins;
    for (const auto &[id, entry] : entries) {
        if (!entry.manifestError.isEmpty())
            continue;
        QJsonObject s;
        s.insert(QLatin1String("enabled"), entry.info.enabled);
        s.insert(QLatin1String("granted"), QJsonArray::fromStringList(permissionsToStrings(entry.info.granted)));
        plugins.insert(id, s);
    }
    QJsonObject root;
    root.insert(QLatin1String("version"), 1);
    root.insert(QLatin1String("plugins"), plugins);
    storage->write(StateKey, QJsonDocument(root).toJson(QJsonDocument::Indented));
}

void PluginHost::dispatchBeforeSend(OutgoingMessage &message)
{
    const auto &table = tables[hookIndex(Hook::BeforeSend)];
    for (size_t i = 0; i < table.size(); ++i)
        guarded(tableIds[hookIndex(Hook::BeforeSend)][i], "onBeforeSend", [&] { table[i]->onBeforeSend(message); });
}

void PluginHost::dispatchBeforeEdit(OutgoingMessage &message)
{
    const auto &table = tables[hookIndex(Hook::BeforeEdit)];
    for (size_t i = 0; i < table.size(); ++i)
        guarded(tableIds[hookIndex(Hook::BeforeEdit)][i], "onBeforeEdit", [&] { table[i]->onBeforeEdit(message); });
}

bool PluginHost::dispatchShouldMentionOnReply(const ReplyContext &reply)
{
    const auto &table = tables[hookIndex(Hook::ShouldMentionOnReply)];
    for (size_t i = 0; i < table.size(); ++i) {
        std::optional<bool> answer;
        guarded(tableIds[hookIndex(Hook::ShouldMentionOnReply)][i], "shouldMentionOnReply",
                [&] { answer = table[i]->shouldMentionOnReply(reply); });
        if (answer)
            return *answer;
    }
    return !reply.shiftHeld;
}

QString PluginHost::dispatchTransformCopiedMessageLink(const QString &url)
{
    QString out = url;
    const auto &table = tables[hookIndex(Hook::TransformCopiedMessageLink)];
    for (size_t i = 0; i < table.size(); ++i)
        guarded(tableIds[hookIndex(Hook::TransformCopiedMessageLink)][i], "transformCopiedMessageLink",
                [&] { out = table[i]->transformCopiedMessageLink(out); });
    return out;
}

void PluginHost::dispatchTransformIncomingContent(IncomingMessage &message)
{
    const auto &table = tables[hookIndex(Hook::TransformIncomingContent)];
    for (size_t i = 0; i < table.size(); ++i)
        guarded(tableIds[hookIndex(Hook::TransformIncomingContent)][i], "transformIncomingContent",
                [&] { table[i]->transformIncomingContent(message); });
}

Plugin *PluginHost::tablePlugin(Hook hook, const QString &id) const
{
    const auto &ids = tableIds[hookIndex(hook)];
    for (size_t i = 0; i < ids.size(); ++i)
        if (ids[i] == id)
            return tables[hookIndex(hook)][i];
    return nullptr;
}

QList<PluginHost::PluginButton> PluginHost::dispatchChatBarButtons(const ChatContext &chat)
{
    QList<PluginButton> out;
    const auto &table = tables[hookIndex(Hook::ChatBarButtons)];
    for (size_t i = 0; i < table.size(); ++i) {
        const QString &id = tableIds[hookIndex(Hook::ChatBarButtons)][i];
        guarded(id, "chatBarButtons", [&] {
            for (const auto &button : table[i]->chatBarButtons(chat))
                out.append({ id, button });
        });
    }
    return out;
}

void PluginHost::dispatchChatBarButtonClicked(const QString &pluginId, const QString &buttonId, const ChatContext &chat)
{
    // Only plugins currently allowed to show buttons receive clicks
    if (Plugin *plugin = tablePlugin(Hook::ChatBarButtons, pluginId))
        guarded(pluginId, "onChatBarButtonClicked", [&] { plugin->onChatBarButtonClicked(buttonId, chat); });
}

QList<PluginHost::PluginMenuItem> PluginHost::dispatchUserContextMenuItems(const UserContext &user)
{
    QList<PluginMenuItem> out;
    const auto &table = tables[hookIndex(Hook::UserContextMenu)];
    for (size_t i = 0; i < table.size(); ++i) {
        const QString &id = tableIds[hookIndex(Hook::UserContextMenu)][i];
        guarded(id, "userContextMenuItems", [&] {
            for (const auto &item : table[i]->userContextMenuItems(user))
                out.append({ id, item });
        });
    }
    return out;
}

void PluginHost::dispatchUserContextMenuAction(const QString &pluginId, const QString &itemId, const UserContext &user)
{
    if (Plugin *plugin = tablePlugin(Hook::UserContextMenu, pluginId))
        guarded(pluginId, "onUserContextMenuAction", [&] { plugin->onUserContextMenuAction(itemId, user); });
}

} // namespace Plugins
} // namespace Qord
