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
    if (hooks.testFlag(Hook::TransformIncomingContent) || hooks.testFlag(Hook::MessageAccessories))
        emit displayTransformsChanged();
    if (hooks.testFlag(Hook::ChatBarButtons))
        emit chatBarChanged();
    if (hooks.testFlag(Hook::DmPins))
        emit dmListChanged();
    if (hooks.testFlag(Hook::MessageFilter) || hooks.testFlag(Hook::MessageHistory))
        emit messageFiltersChanged();
    if (hooks.testFlag(Hook::AuthorNames) || hooks.testFlag(Hook::Typing) || hooks.testFlag(Hook::ServerListButtons) ||
        hooks.testFlag(Hook::MemberCounts) || hooks.testFlag(Hook::UserDecorations) ||
        hooks.testFlag(Hook::VoiceStates) || hooks.testFlag(Hook::ReactionUsers) ||
        hooks.testFlag(Hook::HiddenChannels) || hooks.testFlag(Hook::AttachmentButtons) ||
        hooks.testFlag(Hook::ChannelTyping))
        emit presentationChanged();
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
    entry.context->onDmListChanged = [this] { emit dmListChanged(); };
    entry.context->pluginEnabled = [this](const QString &pluginId) { return isEnabled(pluginId); };
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

QList<PluginHost::PluginMenuItem> PluginHost::dispatchMessageContextMenuItems(const MessageContext &message)
{
    QList<PluginMenuItem> out;
    const auto &table = tables[hookIndex(Hook::MessageContextMenu)];
    for (size_t i = 0; i < table.size(); ++i) {
        const QString &id = tableIds[hookIndex(Hook::MessageContextMenu)][i];
        guarded(id, "messageContextMenuItems", [&] {
            for (const auto &item : table[i]->messageContextMenuItems(message))
                out.append({ id, item });
        });
    }
    return out;
}

void PluginHost::dispatchMessageContextMenuAction(const QString &pluginId, const QString &itemId,
                                                  const MessageContext &message)
{
    if (Plugin *plugin = tablePlugin(Hook::MessageContextMenu, pluginId))
        guarded(pluginId, "onMessageContextMenuAction", [&] { plugin->onMessageContextMenuAction(itemId, message); });
}

bool PluginHost::dispatchMessageDeleted(const MessageContext &message)
{
    bool keep = false;
    const auto &table = tables[hookIndex(Hook::MessageHistory)];
    for (size_t i = 0; i < table.size(); ++i)
        guarded(tableIds[hookIndex(Hook::MessageHistory)][i], "keepDeletedMessage",
                [&] { keep = table[i]->keepDeletedMessage(message) || keep; });
    return keep;
}

void PluginHost::dispatchMessageEdited(const MessageContext &before, const MessageContext &after)
{
    const auto &table = tables[hookIndex(Hook::MessageHistory)];
    for (size_t i = 0; i < table.size(); ++i)
        guarded(tableIds[hookIndex(Hook::MessageHistory)][i], "onMessageEdited",
                [&] { table[i]->onMessageEdited(before, after); });
}

std::optional<MessageAnnotation> PluginHost::dispatchAnnotateMessage(const MessageContext &message)
{
    const auto &table = tables[hookIndex(Hook::MessageHistory)];
    for (size_t i = 0; i < table.size(); ++i) {
        std::optional<MessageAnnotation> annotation;
        guarded(tableIds[hookIndex(Hook::MessageHistory)][i], "annotateMessage",
                [&] { annotation = table[i]->annotateMessage(message); });
        if (annotation)
            return annotation;
    }
    return std::nullopt;
}

bool PluginHost::dispatchShouldHideMessage(const MessageContext &message)
{
    const auto &table = tables[hookIndex(Hook::MessageFilter)];
    for (size_t i = 0; i < table.size(); ++i) {
        bool hide = false;
        guarded(tableIds[hookIndex(Hook::MessageFilter)][i], "shouldHideMessage",
                [&] { hide = table[i]->shouldHideMessage(message); });
        if (hide)
            return true;
    }
    return false;
}

bool PluginHost::dispatchShouldSilenceMessage(const MessageContext &message)
{
    if (dispatchShouldHideMessage(message))
        return true;
    const auto &table = tables[hookIndex(Hook::MessageFilter)];
    for (size_t i = 0; i < table.size(); ++i) {
        bool silence = false;
        guarded(tableIds[hookIndex(Hook::MessageFilter)][i], "shouldSilenceMessage",
                [&] { silence = table[i]->shouldSilenceMessage(message); });
        if (silence)
            return true;
    }
    return false;
}

bool PluginHost::dispatchShouldSendTyping(const TypingContext &typing)
{
    const int h = hookIndex(Hook::Typing);
    for (size_t i = 0; i < tables[h].size(); ++i) {
        std::optional<bool> answer;
        guarded(tableIds[h][i], "shouldSendTyping", [&] { answer = tables[h][i]->shouldSendTyping(typing); });
        if (answer && !*answer)
            return false;
    }
    return true;
}

bool PluginHost::dispatchShouldShowTypers(const ChatContext &chat)
{
    const int h = hookIndex(Hook::Typing);
    for (size_t i = 0; i < tables[h].size(); ++i) {
        std::optional<bool> answer;
        guarded(tableIds[h][i], "shouldShowTypers", [&] { answer = tables[h][i]->shouldShowTypers(chat); });
        if (answer && !*answer)
            return false;
    }
    return true;
}

bool PluginHost::dispatchShouldConfirmLink(const QString &url)
{
    const int h = hookIndex(Hook::LinkWarnings);
    for (size_t i = 0; i < tables[h].size(); ++i) {
        std::optional<bool> answer;
        guarded(tableIds[h][i], "shouldConfirmLink", [&] { answer = tables[h][i]->shouldConfirmLink(url); });
        if (answer && !*answer)
            return false;
    }
    return true;
}

bool PluginHost::dispatchShouldNotify(const NotificationContext &notification)
{
    const int h = hookIndex(Hook::Notifications);
    for (size_t i = 0; i < tables[h].size(); ++i) {
        std::optional<bool> answer;
        guarded(tableIds[h][i], "shouldNotify", [&] { answer = tables[h][i]->shouldNotify(notification); });
        if (answer && !*answer)
            return false;
    }
    return true;
}

std::optional<QString> PluginHost::dispatchAuthorDisplayName(const AuthorContext &author)
{
    const int h = hookIndex(Hook::AuthorNames);
    for (size_t i = 0; i < tables[h].size(); ++i) {
        std::optional<QString> name;
        guarded(tableIds[h][i], "authorDisplayName", [&] { name = tables[h][i]->authorDisplayName(author); });
        if (name)
            return name;
    }
    return std::nullopt;
}

QList<PluginHost::PluginMenuItem> PluginHost::dispatchChannelContextMenuItems(const ChannelContext &channel)
{
    QList<PluginMenuItem> out;
    const int h = hookIndex(Hook::ChannelContextMenu);
    for (size_t i = 0; i < tables[h].size(); ++i) {
        const QString &id = tableIds[h][i];
        guarded(id, "channelContextMenuItems", [&] {
            for (const auto &item : tables[h][i]->channelContextMenuItems(channel))
                out.append({ id, item });
        });
    }
    return out;
}

void PluginHost::dispatchChannelContextMenuAction(const QString &pluginId, const QString &itemId,
                                                  const ChannelContext &channel)
{
    if (Plugin *plugin = tablePlugin(Hook::ChannelContextMenu, pluginId))
        guarded(pluginId, "onChannelContextMenuAction", [&] { plugin->onChannelContextMenuAction(itemId, channel); });
}

QList<PluginHost::PluginServerListButton> PluginHost::dispatchServerListButtons(const ServerListContext &list)
{
    QList<PluginServerListButton> out;
    const int h = hookIndex(Hook::ServerListButtons);
    for (size_t i = 0; i < tables[h].size(); ++i) {
        const QString &id = tableIds[h][i];
        guarded(id, "serverListButtons", [&] {
            for (const auto &button : tables[h][i]->serverListButtons(list))
                out.append({ id, button });
        });
    }
    return out;
}

void PluginHost::dispatchServerListButtonClicked(const QString &pluginId, const QString &buttonId,
                                                 const ServerListContext &list)
{
    if (Plugin *plugin = tablePlugin(Hook::ServerListButtons, pluginId))
        guarded(pluginId, "onServerListButtonClicked", [&] { plugin->onServerListButtonClicked(buttonId, list); });
}

QList<CountBadge> PluginHost::dispatchMemberCountBadges(const MemberCountContext &counts)
{
    const int h = hookIndex(Hook::MemberCounts);
    for (size_t i = 0; i < tables[h].size(); ++i) {
        QList<CountBadge> badges;
        guarded(tableIds[h][i], "memberCountBadges", [&] { badges = tables[h][i]->memberCountBadges(counts); });
        if (!badges.isEmpty())
            return badges;
    }
    return {};
}

QList<NameDecoration> PluginHost::dispatchNameDecorations(const DecorationContext &user)
{
    QList<NameDecoration> out;
    const int h = hookIndex(Hook::UserDecorations);
    for (size_t i = 0; i < tables[h].size(); ++i)
        guarded(tableIds[h][i], "nameDecorations", [&] {
            QList<NameDecoration> decorations = tables[h][i]->nameDecorations(user);
            for (auto &d : decorations)
                d.pluginId = tableIds[h][i];
            out += decorations;
        });
    return out;
}

void PluginHost::dispatchNameDecorationClicked(const QString &pluginId, const QString &clickId,
                                               const DecorationContext &user, bool doubleClick)
{
    const int h = hookIndex(Hook::UserDecorations);
    for (size_t i = 0; i < tables[h].size(); ++i)
        if (tableIds[h][i] == pluginId)
            guarded(tableIds[h][i], "onNameDecorationClicked",
                    [&] { tables[h][i]->onNameDecorationClicked(clickId, user, doubleClick); });
}

bool PluginHost::dispatchProfileImageClicked(const ProfileImageContext &image)
{
    const int h = hookIndex(Hook::ProfileImages);
    for (size_t i = 0; i < tables[h].size(); ++i) {
        bool handled = false;
        guarded(tableIds[h][i], "onProfileImageClicked", [&] { handled = tables[h][i]->onProfileImageClicked(image); });
        if (handled)
            return true;
    }
    return false;
}

std::optional<ReactionUsers> PluginHost::dispatchReactionUsers(const ReactionContext &reaction)
{
    const int h = hookIndex(Hook::ReactionUsers);
    for (size_t i = 0; i < tables[h].size(); ++i) {
        std::optional<ReactionUsers> users;
        guarded(tableIds[h][i], "reactionUsers", [&] { users = tables[h][i]->reactionUsers(reaction); });
        if (users)
            return users;
    }
    return std::nullopt;
}

void PluginHost::dispatchReactionChanged(const ReactionChange &change)
{
    const int h = hookIndex(Hook::ReactionUsers);
    for (size_t i = 0; i < tables[h].size(); ++i)
        guarded(tableIds[h][i], "onReactionChanged", [&] { tables[h][i]->onReactionChanged(change); });
}

void PluginHost::dispatchChatMouseDown(qint64 timeMs)
{
    const int h = hookIndex(Hook::MessageClicks);
    for (size_t i = 0; i < tables[h].size(); ++i)
        guarded(tableIds[h][i], "onChatMouseDown", [&] { tables[h][i]->onChatMouseDown(timeMs); });
}

void PluginHost::dispatchMessageClicked(const MessageClickContext &click)
{
    const int h = hookIndex(Hook::MessageClicks);
    for (size_t i = 0; i < tables[h].size(); ++i)
        guarded(tableIds[h][i], "onMessageClicked", [&] { tables[h][i]->onMessageClicked(click); });
}

void PluginHost::dispatchVoiceStateChanged(const VoiceStateChange &change)
{
    const int h = hookIndex(Hook::VoiceStates);
    for (size_t i = 0; i < tables[h].size(); ++i)
        guarded(tableIds[h][i], "onVoiceStateChanged", [&] { tables[h][i]->onVoiceStateChanged(change); });
}

std::optional<VoiceLabel> PluginHost::dispatchVoiceParticipantLabel(const VoiceParticipantContext &participant)
{
    const int h = hookIndex(Hook::VoiceStates);
    for (size_t i = 0; i < tables[h].size(); ++i) {
        std::optional<VoiceLabel> label;
        guarded(tableIds[h][i], "voiceParticipantLabel", [&] { label = tables[h][i]->voiceParticipantLabel(participant); });
        if (label)
            return label;
    }
    return std::nullopt;
}

std::optional<QString> PluginHost::dispatchVoiceConnectionText(const VoiceConnectionContext &connection)
{
    const int h = hookIndex(Hook::VoiceStates);
    for (size_t i = 0; i < tables[h].size(); ++i) {
        std::optional<QString> text;
        guarded(tableIds[h][i], "voiceConnectionText", [&] { text = tables[h][i]->voiceConnectionText(connection); });
        if (text)
            return text;
    }
    return std::nullopt;
}

void PluginHost::dispatchUserContextMenuAction(const QString &pluginId, const QString &itemId, const UserContext &user)
{
    if (Plugin *plugin = tablePlugin(Hook::UserContextMenu, pluginId))
        guarded(pluginId, "onUserContextMenuAction", [&] { plugin->onUserContextMenuAction(itemId, user); });
}

bool PluginHost::dispatchShortcut(const ShortcutContext &shortcut)
{
    const auto &table = tables[hookIndex(Hook::KeyboardShortcuts)];
    for (size_t i = 0; i < table.size(); ++i) {
        bool used = false;
        guarded(tableIds[hookIndex(Hook::KeyboardShortcuts)][i], "onShortcut", [&] { used = table[i]->onShortcut(shortcut); });
        if (used)
            return true;
    }
    return false;
}

void PluginHost::dispatchUserMentionHovered(const MentionContext &mention)
{
    const auto &table = tables[hookIndex(Hook::UnknownMentions)];
    for (size_t i = 0; i < table.size(); ++i)
        guarded(tableIds[hookIndex(Hook::UnknownMentions)][i], "onUserMentionHovered",
                [&] { table[i]->onUserMentionHovered(mention); });
}

std::optional<TypingIndicator> PluginHost::dispatchChannelTypingIndicator(const ChannelTypingContext &channel)
{
    const auto &table = tables[hookIndex(Hook::ChannelTyping)];
    for (size_t i = 0; i < table.size(); ++i) {
        std::optional<TypingIndicator> out;
        guarded(tableIds[hookIndex(Hook::ChannelTyping)][i], "channelTypingIndicator",
                [&] { out = table[i]->channelTypingIndicator(channel); });
        if (out)
            return out;
    }
    return std::nullopt;
}

QList<PluginHost::PluginAccessory> PluginHost::dispatchMessageAccessories(const MessageContext &message)
{
    QList<PluginAccessory> out;
    const auto &table = tables[hookIndex(Hook::MessageAccessories)];
    for (size_t i = 0; i < table.size(); ++i) {
        const QString &id = tableIds[hookIndex(Hook::MessageAccessories)][i];
        guarded(id, "messageAccessory", [&] {
            if (auto accessory = table[i]->messageAccessory(message))
                out.append({ id, *accessory });
        });
    }
    return out;
}

void PluginHost::dispatchMessageAccessoryAction(const QString &pluginId, const QString &actionId,
                                                const MessageContext &message)
{
    if (Plugin *plugin = tablePlugin(Hook::MessageAccessories, pluginId))
        guarded(pluginId, "onMessageAccessoryAction", [&] { plugin->onMessageAccessoryAction(actionId, message); });
}

QList<PluginHost::PluginButton> PluginHost::dispatchAttachmentButtons(const AttachmentContext &attachment)
{
    QList<PluginButton> out;
    const auto &table = tables[hookIndex(Hook::AttachmentButtons)];
    for (size_t i = 0; i < table.size(); ++i) {
        const QString &id = tableIds[hookIndex(Hook::AttachmentButtons)][i];
        guarded(id, "attachmentButtons", [&] {
            for (const auto &button : table[i]->attachmentButtons(attachment))
                out.append({ id, button });
        });
    }
    return out;
}

void PluginHost::dispatchAttachmentButtonClicked(const QString &pluginId, const QString &buttonId,
                                                 const AttachmentContext &attachment)
{
    if (Plugin *plugin = tablePlugin(Hook::AttachmentButtons, pluginId))
        guarded(pluginId, "onAttachmentButtonClicked", [&] { plugin->onAttachmentButtonClicked(buttonId, attachment); });
}

std::optional<HiddenChannelDisplay> PluginHost::dispatchHiddenChannelDisplay()
{
    const auto &table = tables[hookIndex(Hook::HiddenChannels)];
    for (size_t i = 0; i < table.size(); ++i) {
        std::optional<HiddenChannelDisplay> out;
        guarded(tableIds[hookIndex(Hook::HiddenChannels)][i], "hiddenChannelDisplay",
                [&] { out = table[i]->hiddenChannelDisplay(); });
        if (out)
            return out;
    }
    return std::nullopt;
}

std::optional<LockScreen> PluginHost::dispatchHiddenChannelLockScreen(const HiddenChannelContext &channel)
{
    const auto &table = tables[hookIndex(Hook::HiddenChannels)];
    for (size_t i = 0; i < table.size(); ++i) {
        std::optional<LockScreen> out;
        guarded(tableIds[hookIndex(Hook::HiddenChannels)][i], "hiddenChannelLockScreen",
                [&] { out = table[i]->hiddenChannelLockScreen(channel); });
        if (out)
            return out;
    }
    return std::nullopt;
}

QList<PluginHost::PluginButton> PluginHost::dispatchProfileRoleButtons(const UserContext &user)
{
    QList<PluginButton> out;
    const auto &table = tables[hookIndex(Hook::ProfileRoleButtons)];
    for (size_t i = 0; i < table.size(); ++i) {
        const QString &id = tableIds[hookIndex(Hook::ProfileRoleButtons)][i];
        guarded(id, "profileRoleButtons", [&] {
            for (const auto &button : table[i]->profileRoleButtons(user))
                out.append({ id, button });
        });
    }
    return out;
}

void PluginHost::dispatchProfileRoleButtonClicked(const QString &pluginId, const QString &buttonId,
                                                  const UserContext &user)
{
    if (Plugin *plugin = tablePlugin(Hook::ProfileRoleButtons, pluginId))
        guarded(pluginId, "onProfileRoleButtonClicked", [&] { plugin->onProfileRoleButtonClicked(buttonId, user); });
}

void PluginHost::dispatchSortEmojiSuggestions(EmojiAutocompleteContext &context)
{
    const auto &table = tables[hookIndex(Hook::EmojiSuggestions)];
    for (size_t i = 0; i < table.size(); ++i)
        guarded(tableIds[hookIndex(Hook::EmojiSuggestions)][i], "sortEmojiSuggestions",
                [&] { table[i]->sortEmojiSuggestions(context); });
}

QList<PluginHost::PluginMenuItem> PluginHost::dispatchEmojiContextMenuItems(const EmojiContext &emoji)
{
    QList<PluginMenuItem> out;
    const auto &table = tables[hookIndex(Hook::EmojiSuggestions)];
    for (size_t i = 0; i < table.size(); ++i) {
        const QString &id = tableIds[hookIndex(Hook::EmojiSuggestions)][i];
        guarded(id, "emojiContextMenuItems", [&] {
            for (const auto &item : table[i]->emojiContextMenuItems(emoji))
                out.append({ id, item });
        });
    }
    return out;
}

void PluginHost::dispatchEmojiContextMenuAction(const QString &pluginId, const QString &itemId, const EmojiContext &emoji)
{
    if (Plugin *plugin = tablePlugin(Hook::EmojiSuggestions, pluginId))
        guarded(pluginId, "onEmojiContextMenuAction", [&] { plugin->onEmojiContextMenuAction(itemId, emoji); });
}

std::optional<QPair<QString, DmPinLayout>> PluginHost::dispatchDmPinLayout(Id accountId)
{
    const auto &table = tables[hookIndex(Hook::DmPins)];
    for (size_t i = 0; i < table.size(); ++i) {
        const QString &id = tableIds[hookIndex(Hook::DmPins)][i];
        std::optional<DmPinLayout> layout;
        guarded(id, "dmPinLayout", [&] { layout = table[i]->dmPinLayout(accountId); });
        if (layout)
            return qMakePair(id, *layout);
    }
    return std::nullopt;
}

void PluginHost::dispatchDmPinHeaderClicked(const QString &pluginId, Id accountId, const QString &headerId)
{
    if (Plugin *plugin = tablePlugin(Hook::DmPins, pluginId))
        guarded(pluginId, "onDmPinHeaderClicked", [&] { plugin->onDmPinHeaderClicked(accountId, headerId); });
}

QList<MenuItem> PluginHost::dispatchDmPinHeaderMenu(const QString &pluginId, Id accountId, const QString &headerId)
{
    QList<MenuItem> out;
    if (Plugin *plugin = tablePlugin(Hook::DmPins, pluginId))
        guarded(pluginId, "dmPinHeaderMenu", [&] { out = plugin->dmPinHeaderMenu(accountId, headerId); });
    return out;
}

void PluginHost::dispatchDmPinHeaderMenuAction(const QString &pluginId, Id accountId, const QString &headerId,
                                               const QString &itemId)
{
    if (Plugin *plugin = tablePlugin(Hook::DmPins, pluginId))
        guarded(pluginId, "onDmPinHeaderMenuAction",
                [&] { plugin->onDmPinHeaderMenuAction(accountId, headerId, itemId); });
}

QList<PluginHost::PluginProfileTab> PluginHost::dispatchProfileTabs(const UserContext &user)
{
    QList<PluginProfileTab> out;
    const auto &table = tables[hookIndex(Hook::ProfileTabs)];
    for (size_t i = 0; i < table.size(); ++i) {
        const QString &id = tableIds[hookIndex(Hook::ProfileTabs)][i];
        guarded(id, "profileTabs", [&] {
            for (const auto &tab : table[i]->profileTabs(user))
                out.append({ id, tab });
        });
    }
    return out;
}

void PluginHost::dispatchProfileTabRowClicked(const QString &pluginId, const QString &tabId, const QString &rowId,
                                              const UserContext &user)
{
    if (Plugin *plugin = tablePlugin(Hook::ProfileTabs, pluginId))
        guarded(pluginId, "onProfileTabRowClicked", [&] { plugin->onProfileTabRowClicked(tabId, rowId, user); });
}

} // namespace Plugins
} // namespace Qord
