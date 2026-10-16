#include "Core/OpenExternalUrl.hpp"
#include "ClientPlugins.hpp"
#include <QPointer>
#include <memory>

#include "Core/ClientInstance.hpp"
#include "Core/Emoji/EmojiManager.hpp"
#include "Core/PermissionManager.hpp"
#include "Plugins/Client/MessageContexts.hpp"
#include "Core/Logging.hpp"
#include "Core/MessageManager.hpp"
#include "Core/NetworkRequest.hpp"
#include "Core/NetworkAccessManager.hpp"
#include "Core/Presence/PresenceBadge.hpp"
#include "Core/Presence/PresenceManager.hpp"
#include "Core/RelationshipManager.hpp"
#include "Core/Audio/VolumeLimit.hpp"
#ifndef ACHERON_NO_VOICE
#include "Core/Audio/VoiceManager.hpp"
#endif
#include "Core/CompactConnections.hpp"
#include "Core/FolderSidebar.hpp"
#include "Core/ImageLens.hpp"
#include "Core/OwnerCrown.hpp"
#include "Core/RoleColors.hpp"
#include "Core/RoleStyle.hpp"
#include "Core/Session.hpp"
#include "Core/TokenStore.hpp"
#include "Qord/DevMock.hpp"
#include "Plugins/Client/NetworkPolicy.hpp"
#include "Core/UserManager.hpp"
#include "Core/Markdown/Parser.hpp"
#include "Discord/CdnUrls.hpp"
#include "Discord/HttpClient.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QMessageBox>
#include <QClipboard>
#include <QColorDialog>
#include <QComboBox>
#include <QCompleter>
#include <QDateTimeEdit>
#include <QLineEdit>
#include <QLocale>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QLabel>
#include <QJsonObject>
#include <QJsonDocument>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QDir>
#include <QGuiApplication>
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
    return send(url, {}, nullptr, std::move(callback));
}

NetworkService::Handle QtNetworkService::post(const QUrl &url, const Headers &headers, const QByteArray &body,
                                              Callback callback)
{
    return send(url, headers, &body, std::move(callback));
}

NetworkService::Handle QtNetworkService::request(const QByteArray &method, const QUrl &url, const Headers &headers,
                                                 const QByteArray &body, Callback callback)
{
    if (method == "GET" && body.isEmpty())
        return send(url, headers, nullptr, std::move(callback), true);
    return send(url, headers, &body, std::move(callback), true, method);
}

// body null: GET. Headers can hold API keys, so nothing here logs them.
NetworkService::Handle QtNetworkService::send(const QUrl &url, const Headers &headers, const QByteArray *body,
                                              Callback callback, bool keepErrorBodies, const QByteArray &method)
{
    const Handle handle = ++next;
    // Plugins only fetch over HTTPS
    if (!isPluginNetworkUrl(url)) {
        QMetaObject::invokeMethod(this, [callback = std::move(callback)] {
            Reply r;
            r.error = QStringLiteral("only valid https URLs without user credentials are allowed");
            callback(r);
        }, Qt::QueuedConnection);
        return handle;
    }
    if (!nam)
        nam = new Acheron::Core::NetworkAccessManager(this);

    QNetworkRequest request = pluginNetworkRequest(url);
    for (const auto &header : headers)
        request.setRawHeader(header.first, header.second);
    QNetworkReply *reply = !method.isEmpty() ? nam->sendCustomRequest(request, method, body ? *body : QByteArray())
                           : body           ? nam->post(request, *body)
                                            : nam->get(request);
    replies.insert(handle, reply);

    const auto buffer = std::make_shared<PluginResponseBuffer>();
    reply->setReadBufferSize(MaxResponseBytes + 1);
    connect(reply, &QIODevice::readyRead, this, [reply, buffer] {
        if (!buffer->read(*reply, MaxResponseBytes))
            reply->abort();
    });
    const bool keepErrorBody = body != nullptr || keepErrorBodies;
    connect(reply, &QNetworkReply::finished, this, [this, handle, reply, buffer, keepErrorBody, callback = std::move(callback)] {
        reply->deleteLater();
        if (!replies.remove(handle))
            return; // cancelled
        Reply r;
        r.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (!buffer->read(*reply, MaxResponseBytes)) {
            r.error = QStringLiteral("response exceeds size limit");
            callback(r);
            return;
        }
        r.ok = reply->error() == QNetworkReply::NoError && r.status >= 200 && r.status < 300;
        if (r.ok || (keepErrorBody && r.status))
            r.body = std::move(buffer->data);
        if (!r.ok)
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

void QtClipboardService::setText(const QString &text)
{
    QGuiApplication::clipboard()->setText(text);
}

static std::optional<Acheron::Discord::Relationship> relationshipOf(Acheron::Core::Session *session, Id accountId,
                                                                    Id userId)
{
    Acheron::Core::ClientInstance *instance = session->client(Acheron::Core::Snowflake(accountId));
    if (!instance || !instance->relationships())
        return std::nullopt;
    return instance->relationships()->getRelationship(Acheron::Core::Snowflake(userId));
}

bool SessionRelationshipDirectory::isBlocked(Id accountId, Id userId) const
{
    const auto rel = relationshipOf(session, accountId, userId);
    return rel && rel->type.hasValue() && rel->type.get() == Acheron::Discord::RelationshipType::BLOCKED;
}

bool SessionRelationshipDirectory::isIgnored(Id accountId, Id userId) const
{
    const auto rel = relationshipOf(session, accountId, userId);
    return rel && rel->userIgnored.hasValue() && rel->userIgnored.get();
}

QList<Id> SessionRelationshipDirectory::relationshipIds(Id accountId, Kind kind) const
{
    Acheron::Core::ClientInstance *instance = session->client(Acheron::Core::Snowflake(accountId));
    if (!instance || !instance->relationships())
        return {};
    QList<Id> out;
    for (const auto &rel : instance->relationships()->all()) {
        const bool match =
                kind == Kind::Ignored ? rel.userIgnored.hasValue() && rel.userIgnored.get()
                : kind == Kind::Blocked
                        ? rel.type.hasValue() && rel.type.get() == Acheron::Discord::RelationshipType::BLOCKED
                        : rel.type.hasValue() && rel.type.get() == Acheron::Discord::RelationshipType::FRIEND;
        if (match && rel.id.hasValue())
            out.append(quint64(rel.id.get()));
    }
    return out;
}

bool SessionRelationshipDirectory::hasRelationship(Id accountId, Id userId) const
{
    Acheron::Core::ClientInstance *instance = session->client(Acheron::Core::Snowflake(accountId));
    if (!instance || !instance->relationships())
        return false;
    for (const auto &rel : instance->relationships()->all())
        if (rel.id.hasValue() && quint64(rel.id.get()) == userId)
            return rel.type.hasValue() && int(rel.type.get()) != 0;
    return false;
}

void SessionRelationshipDirectory::fetchUserAffinities(Id accountId, AffinitiesCallback done) const
{
    Acheron::Core::ClientInstance *instance = session->client(Acheron::Core::Snowflake(accountId));
    if (!instance) {
        done(false, {});
        return;
    }
    instance->discord()->fetchUserAffinities(
            [done](const Acheron::Discord::HttpResponse &response,
                   const QList<QPair<Acheron::Core::Snowflake, double>> &affinities) {
                QList<UserAffinity> out;
                for (const auto &[userId, rank] : affinities)
                    out.append({ quint64(userId), rank });
                done(response.success, out);
            });
}

void QtUiService::showTextDialog(const QString &title, const QList<TextSection> &sections)
{
    auto *dialog = new QDialog(QApplication::activeWindow());
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(title);
    dialog->resize(640, 520);
    auto *layout = new QVBoxLayout(dialog);
    for (const auto &section : sections) {
        auto *header = new QHBoxLayout;
        auto *label = new QLabel(QStringLiteral("<b>%1</b>").arg(section.title.toHtmlEscaped()), dialog);
        auto *copy = new QPushButton(QObject::tr("Copy"), dialog);
        header->addWidget(label, 1);
        header->addWidget(copy);
        layout->addLayout(header);
        auto *text = new QPlainTextEdit(section.text, dialog);
        text->setReadOnly(true);
        if (section.monospace)
            text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        layout->addWidget(text, 1);
        const QString value = section.text;
        const QString toast = section.copiedToast;
        QObject::connect(copy, &QPushButton::clicked, dialog, [value, toast] {
            QGuiApplication::clipboard()->setText(value);
            if (!toast.isEmpty() && toastShower())
                toastShower()(toast, ToastType::Success);
        });
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    layout->addWidget(buttons);
    dialog->show();
}

std::function<void(const PermissionsView &)> &QtUiService::permissionsShower()
{
    static std::function<void(const PermissionsView &)> shower;
    return shower;
}

std::function<void(const MemberPermissionsView &)> &QtUiService::memberPermissionsShower()
{
    static std::function<void(const MemberPermissionsView &)> shower;
    return shower;
}

std::function<void()> &QtUiService::lockScreenRefresher()
{
    static std::function<void()> refresher;
    return refresher;
}

std::function<void(Id, Id)> &QtUiService::channelOpener()
{
    static std::function<void(Id, Id)> opener;
    return opener;
}

void QtUiService::openChannel(Id accountId, Id channelId)
{
    if (channelOpener())
        channelOpener()(accountId, channelId);
}

std::function<void(Id, Id)> &QtUiService::voiceJoiner()
{
    static std::function<void(Id, Id)> joiner;
    return joiner;
}

void QtUiService::joinVoiceChannel(Id accountId, Id channelId)
{
    if (voiceJoiner())
        voiceJoiner()(accountId, channelId);
}

std::function<void(Id)> &QtUiService::voiceLeaver()
{
    static std::function<void(Id)> leaver;
    return leaver;
}

void QtUiService::leaveVoice(Id accountId)
{
    if (voiceLeaver())
        voiceLeaver()(accountId);
}

std::function<void(Id, Id, const QString &)> &QtUiService::localMessageShower()
{
    static std::function<void(Id, Id, const QString &)> shower;
    return shower;
}

void QtUiService::showLocalMessage(Id accountId, Id channelId, const QString &content)
{
    if (localMessageShower())
        localMessageShower()(accountId, channelId, content);
}

void QtAudioOptions::setUserVolumeMultiplier(double multiplier)
{
    multiplier = qBound(1.0, multiplier, 5.0);
    if (qFuzzyCompare(Acheron::Core::Audio::userVolumeMultiplier(), multiplier))
        return;
    Acheron::Core::Audio::userVolumeMultiplier() = multiplier;
    if (host)
        emit host->presentationChanged();
}

std::optional<LinkedMessage> SessionMessageLookup::message(Id accountId, Id channelId, Id messageId)
{
    using namespace Acheron;
    Core::ClientInstance *instance = session->client(Core::Snowflake(accountId));
    if (!instance)
        return std::nullopt;
    const auto found = instance->messages()->findMessage(Core::Snowflake(messageId));
    if (!found || quint64(found->channelId.get()) != channelId)
        return std::nullopt;
    const Discord::Message &msg = *found;
    LinkedMessage out;
    out.channelId = channelId;
    out.messageId = messageId;
    out.content = msg.content.hasValue() ? msg.content.get() : QString();
    if (msg.timestamp.hasValue())
        out.timestamp = msg.timestamp.get();
    const Core::Snowflake guildId = instance->discord()->getGuildIdForChannel(Core::Snowflake(channelId));
    if (msg.author.hasValue()) {
        out.authorId = quint64(msg.author->id.get());
        out.authorName = instance->users()->getDisplayName(msg.author->id.get(), guildId);
        out.authorAvatarUrl = instance->users()->getAvatarUrl(msg.author.get(), guildId, 64).toString();
    }
    if (msg.attachments.hasValue()) {
        out.attachmentCount = int(msg.attachments->size());
        for (const auto &att : msg.attachments.get())
            if (att.contentType.hasValue() && att.contentType->startsWith(QLatin1String("image/")))
                out.images.append({ att.proxyUrl.get(), att.width.hasValue() ? att.width.get() : 0,
                                    att.height.hasValue() ? att.height.get() : 0 });
    }
    if (msg.embeds.hasValue()) {
        out.embedCount = int(msg.embeds->size());
        // Upstream's getImages: image embeds, and GIFs that aren't from Tenor
        for (const auto &embed : msg.embeds.get()) {
            const QString type = embed.type.hasValue() ? embed.type.get() : QString();
            const auto &media = embed.image.hasValue() ? embed.image : embed.thumbnail;
            const QString url = embed.url.hasValue() ? embed.url.get() : QString();
            if (!media.hasValue())
                continue;
            const QString mediaUrl = media->proxyUrl.hasValue() ? media->proxyUrl.get() : media->url.get();
            if (type == QLatin1String("image") ||
                (type == QLatin1String("gifv") && !url.contains(QLatin1String("tenor.com/"))))
                out.images.append({ mediaUrl, media->width.hasValue() ? media->width.get() : 0,
                                    media->height.hasValue() ? media->height.get() : 0 });
        }
    }
    return out;
}

void SessionMessageLookup::fetchMessage(Id accountId, Id channelId, Id messageId, std::function<void(bool)> done)
{
    Acheron::Core::ClientInstance *instance = session->client(Acheron::Core::Snowflake(accountId));
    if (!instance) {
        done(false);
        return;
    }
    instance->messages()->fetchLinkedMessage(Acheron::Core::Snowflake(channelId), Acheron::Core::Snowflake(messageId),
                                             std::move(done));
}

std::optional<LinkedChannel> SessionMessageLookup::channel(Id accountId, Id channelId)
{
    using namespace Acheron;
    Core::ClientInstance *instance = session->client(Core::Snowflake(accountId));
    if (!instance)
        return std::nullopt;
    const auto channel = instance->getChannel(Core::Snowflake(channelId));
    if (!channel)
        return std::nullopt;
    LinkedChannel out;
    out.channelId = channelId;
    out.type = channel->type.hasValue() ? int(channel->type.get()) : 0;
    out.name = channel->name.hasValue() ? channel->name.get() : QString();
    const bool isPrivate = out.type == int(Discord::ChannelType::DM) || out.type == int(Discord::ChannelType::GROUP_DM);
    if (isPrivate) {
        out.canView = true;
        out.canConnect = true;
        if (channel->type.get() == Discord::ChannelType::GROUP_DM) {
            if (channel->icon.hasValue())
                out.iconUrl = Discord::Cdn::channelIcon(Core::Snowflake(channelId), channel->icon.get(), 64).toString();
        } else {
            Core::Snowflake recipient;
            if (channel->recipients.hasValue() && !channel->recipients->isEmpty())
                recipient = channel->recipients->first().id.get();
            else if (channel->recipientIds.hasValue() && !channel->recipientIds->isEmpty())
                recipient = channel->recipientIds->first();
            out.recipientId = quint64(recipient);
            out.name = instance->users()->getDisplayName(recipient);
            if (const auto user = instance->users()->getUser(recipient))
                out.iconUrl = instance->users()->getAvatarUrl(*user, Core::Snowflake::Invalid, 64).toString();
        }
        return out;
    }
    const Core::Snowflake guildId = channel->guildId.hasValue() ? channel->guildId.get() : Core::Snowflake::Invalid;
    out.guildId = Qord::Plugins::pluginId(guildId);
    if (const auto guild = instance->getGuild(guildId))
        if (guild->icon.hasValue())
            out.iconUrl = Discord::Cdn::guildIcon(guildId, guild->icon.get(), 64).toString();
    out.canView = instance->permissions()->hasChannelPermission(instance->accountId(), Core::Snowflake(channelId),
                                                                 Discord::Permission::VIEW_CHANNEL);
    out.canConnect = out.canView && instance->permissions()->hasChannelPermission(instance->accountId(), Core::Snowflake(channelId),
                                                                                  Discord::Permission::CONNECT);
    return out;
}

std::optional<UserVoiceState> SessionVoiceStates::voiceState(Id accountId, Id userId) const
{
#ifndef ACHERON_NO_VOICE
    using namespace Acheron;
    Core::ClientInstance *instance = session->client(Core::Snowflake(accountId));
    if (!instance || !instance->voice())
        return std::nullopt;
    const auto state = instance->voice()->voiceStateForUser(Core::Snowflake(userId));
    if (!state || state->channelId.isNull() || !state->channelId.get().isValid())
        return std::nullopt;
    UserVoiceState out;
    out.channelId = quint64(state->channelId.get());
    if (state->guildId.hasValue())
        out.guildId = Qord::Plugins::pluginId(state->guildId.get());
    out.muted = state->mute.valueOr(false) || state->selfMute.valueOr(false);
    out.deafened = state->deaf.valueOr(false) || state->selfDeaf.valueOr(false);
    return out;
#else
    Q_UNUSED(accountId);
    Q_UNUSED(userId);
    return std::nullopt;
#endif
}

QList<Id> SessionVoiceStates::channelUsers(Id accountId, Id channelId) const
{
    QList<Id> out;
#ifndef ACHERON_NO_VOICE
    using namespace Acheron;
    Core::ClientInstance *instance = session->client(Core::Snowflake(accountId));
    if (!instance || !instance->voice())
        return out;
    for (const Core::Snowflake id : instance->voice()->channelVoiceUsers(Core::Snowflake(channelId)))
        out.append(quint64(id));
#else
    Q_UNUSED(accountId);
    Q_UNUSED(channelId);
#endif
    return out;
}

std::optional<LinkedChannel> SessionVoiceStates::channel(Id accountId, Id channelId) const
{
    return lookup.channel(accountId, channelId);
}

QString SessionVoiceStates::displayName(Id accountId, Id guildId, Id userId) const
{
    using namespace Acheron;
    Core::ClientInstance *instance = session->client(Core::Snowflake(accountId));
    if (!instance)
        return {};
    return instance->users()->getDisplayName(Core::Snowflake(userId),
                                             guildId ? Core::Snowflake(guildId) : Core::Snowflake::Invalid);
}

bool SessionActivity::showCurrentGame(Id accountId) const
{
    Acheron::Core::ClientInstance *instance = session->client(Acheron::Core::Snowflake(accountId));
    return !instance || instance->discord()->showCurrentGame();
}

void SessionActivity::setShowCurrentGame(Id accountId, bool show)
{
    if (Acheron::Core::ClientInstance *instance = session->client(Acheron::Core::Snowflake(accountId)))
        instance->discord()->setShowCurrentGame(show);
}

void SessionActivity::fetchConnections(Id accountId,
                                       std::function<void(bool, const QList<ActivityConnection> &)> done)
{
    Acheron::Core::ClientInstance *instance = session->client(Acheron::Core::Snowflake(accountId));
    if (!instance) {
        done(false, {});
        return;
    }
    instance->discord()->fetchOwnConnections(
            [done](bool ok, const QList<Acheron::Discord::Client::OwnConnection> &connections) {
                QList<ActivityConnection> out;
                for (const auto &c : connections)
                    out.append({ c.type, c.id, c.name, c.showActivity, c.revoked });
                done(ok, out);
            });
}

void SessionActivity::setConnectionShowActivity(Id accountId, const QString &type, const QString &connectionId,
                                                bool show, std::function<void(bool)> done)
{
    Acheron::Core::ClientInstance *instance = session->client(Acheron::Core::Snowflake(accountId));
    if (!instance) {
        if (done)
            done(false);
        return;
    }
    instance->discord()->setConnectionShowActivity(type, connectionId, show, std::move(done));
}

std::function<QList<PrivateChannelInfo>(Id)> &QtPrivateChannels::provider()
{
    static std::function<QList<PrivateChannelInfo>(Id)> list;
    return list;
}

QList<PrivateChannelInfo> QtPrivateChannels::privateChannels(Id accountId) const
{
    return provider() ? provider()(accountId) : QList<PrivateChannelInfo>();
}

std::function<void(const ServerInfoView &)> &QtUiService::serverInfoShower()
{
    static std::function<void(const ServerInfoView &)> shower;
    return shower;
}

std::function<void(Id, Id, Id)> &QtUiService::profileOpener()
{
    static std::function<void(Id, Id, Id)> opener;
    return opener;
}

QtUiService::ChatControls &QtUiService::chatControls()
{
    static ChatControls controls;
    return controls;
}

void QtUiService::cancelReply()
{
    if (chatControls().cancelReply)
        chatControls().cancelReply();
}

void QtUiService::cancelEditing()
{
    if (chatControls().cancelEditing)
        chatControls().cancelEditing();
}

void QtUiService::focusMessageBox()
{
    if (chatControls().focusMessageBox)
        chatControls().focusMessageBox();
}

void QtUiService::revealMessage(Id messageId)
{
    if (chatControls().revealMessage)
        chatControls().revealMessage(messageId);
}

void QtUiService::showServerInfo(const ServerInfoView &view)
{
    if (serverInfoShower())
        serverInfoShower()(view);
}

void QtUiService::openUserProfile(Id accountId, Id guildId, Id userId)
{
    if (profileOpener())
        profileOpener()(accountId, guildId, userId);
}

std::function<void(Id, Id, Id)> &QtUiService::attachmentRefresher()
{
    static std::function<void(Id, Id, Id)> refresher;
    return refresher;
}

void QtUiService::refreshAttachments(Id accountId, Id channelId, Id messageId)
{
    if (attachmentRefresher())
        attachmentRefresher()(accountId, channelId, messageId);
}

void QtUiService::refreshLockScreen()
{
    if (lockScreenRefresher())
        lockScreenRefresher()();
}

void QtUiService::showPermissions(const PermissionsView &view)
{
    if (permissionsShower())
        permissionsShower()(view);
}

void QtUiService::showMemberPermissions(const MemberPermissionsView &view)
{
    if (memberPermissionsShower())
        memberPermissionsShower()(view);
}

void QtUiService::openUrl(const QUrl &url)
{
    Acheron::Core::openExternalUrl(url);
}

std::function<void(const PluginNotification &, std::function<void()>)> &QtUiService::notificationShower()
{
    static std::function<void(const PluginNotification &, std::function<void()>)> shower;
    return shower;
}

void QtUiService::showNotification(const PluginNotification &notification, std::function<void()> clicked)
{
    if (notificationShower())
        notificationShower()(notification, std::move(clicked));
}

std::function<void(const OAuthRequest &, std::function<void(std::optional<QString>)>)> &QtUiService::appAuthorizer()
{
    static std::function<void(const OAuthRequest &, std::function<void(std::optional<QString>)>)> authorizer;
    return authorizer;
}

void QtUiService::authorizeApp(const OAuthRequest &request, std::function<void(std::optional<QString>)> done)
{
    if (!appAuthorizer()) {
        done(std::nullopt);
        return;
    }
    appAuthorizer()(request, std::move(done));
}

QtUiService::ListDialogs &QtUiService::listDialogs()
{
    static ListDialogs dialogs;
    return dialogs;
}

quint64 QtUiService::openListDialog(const ListDialogView &view, ListDialogEvents events)
{
    return listDialogs().open ? listDialogs().open(view, std::move(events)) : 0;
}

void QtUiService::updateListDialog(quint64 handle, const ListDialogView &view)
{
    if (listDialogs().update)
        listDialogs().update(handle, view);
}

void QtUiService::closeListDialog(quint64 handle)
{
    if (listDialogs().close)
        listDialogs().close(handle);
}

std::function<void()> &QtUiService::devicesOpener()
{
    static std::function<void()> opener;
    return opener;
}

void QtUiService::openDevicesSettings()
{
    if (devicesOpener())
        devicesOpener()();
}

std::function<void()> &SessionDevices::devicesRefresher()
{
    static std::function<void()> refresher;
    return refresher;
}

QString SessionPresences::status(Id accountId, Id userId) const
{
    Acheron::Core::ClientInstance *instance = session->client(Acheron::Core::Snowflake(accountId));
    if (!instance || !instance->presences())
        return QStringLiteral("offline");
    switch (instance->presences()->badge(Acheron::Core::Snowflake(userId)).status) {
    case Acheron::Discord::StatusType::ONLINE:
        return QStringLiteral("online");
    case Acheron::Discord::StatusType::IDLE:
        return QStringLiteral("idle");
    case Acheron::Discord::StatusType::DND:
        return QStringLiteral("dnd");
    default:
        return QStringLiteral("offline");
    }
}

QList<Id> SessionDevices::accountIds() const
{
    QList<Id> out;
    for (const auto &instance : session->getClients())
        out.append(quint64(instance->accountId()));
    return out;
}

void SessionDevices::refreshDevicesPage()
{
    if (devicesRefresher())
        devicesRefresher()();
}

void SessionDevices::fetchSessions(Id accountId, std::function<void(bool, const QList<DeviceSession> &)> done)
{
    Acheron::Core::ClientInstance *instance = session->client(Acheron::Core::Snowflake(accountId));
    if (!instance) {
        done(false, {});
        return;
    }
    const QString current = instance->discord()->currentAuthSessionHash();
    instance->discord()->fetchAuthSessions(
            [done, current](bool ok, const QList<Acheron::Discord::Client::AuthSession> &list) {
                QList<DeviceSession> out;
                for (const auto &a : list)
                    out.append({ a.idHash, a.os, a.platform, a.location,
                                 a.lastUsed.isValid() ? a.lastUsed.toMSecsSinceEpoch() : 0,
                                 !current.isEmpty() && a.idHash == current });
                done(ok, out);
            });
}

std::function<void(const QString &, UiService::ToastType)> &QtUiService::toastShower()
{
    static std::function<void(const QString &, ToastType)> shower;
    return shower;
}

void QtUiService::showToast(const QString &text, ToastType type)
{
    if (toastShower())
        toastShower()(text, type);
}

std::function<void(const QString &)> &QtUiService::messageBoxInserter()
{
    static std::function<void(const QString &)> inserter;
    return inserter;
}

std::function<void(const QString &)> &QtUiService::pluginSettingsOpener()
{
    static std::function<void(const QString &)> opener;
    return opener;
}

std::function<void(const QUrl &)> &QtUiService::imageOpener()
{
    static std::function<void(const QUrl &)> opener;
    return opener;
}

std::function<void(const MessageContext &)> &QtUiService::editStarter()
{
    static std::function<void(const MessageContext &)> starter;
    return starter;
}

std::function<void(const MessageContext &, bool, std::optional<bool>)> &QtUiService::replyStarter()
{
    static std::function<void(const MessageContext &, bool, std::optional<bool>)> starter;
    return starter;
}

void QtUiService::startEditing(const MessageContext &message)
{
    if (editStarter())
        editStarter()(message);
}

void QtUiService::startReply(const MessageContext &message, bool shiftHeld, std::optional<bool> mention)
{
    if (replyStarter())
        replyStarter()(message, shiftHeld, mention);
}

void QtUiService::openImage(const QUrl &url)
{
    if (imageOpener())
        imageOpener()(url);
}

std::function<void(Id)> &QtReadStates::allServersMarker()
{
    static std::function<void(Id)> marker;
    return marker;
}

void QtReadStates::markAllServersRead(Id accountId)
{
    if (allServersMarker())
        allServersMarker()(accountId);
}

void QtDisplayOptions::setMobileIndicatorForAnyStatus(bool any)
{
    if (Acheron::Core::mobileIndicatorForAnyStatus() == any)
        return;
    Acheron::Core::mobileIndicatorForAnyStatus() = any;
    if (host)
        emit host->presentationChanged();
}

void QtDisplayOptions::setForceOwnerCrown(bool force)
{
    if (Acheron::Core::forceOwnerCrown() == force)
        return;
    Acheron::Core::forceOwnerCrown() = force;
    if (host)
        emit host->presentationChanged();
}

void QtDisplayOptions::setImageLens(const std::optional<ImageLens> &lens, std::function<void(double, int)> changed)
{
    auto &options = Acheron::Core::imageLensOptions();
    options = {};
    if (!lens)
        return;
    options.enabled = true;
    options.zoom = lens->zoom;
    options.size = lens->size;
    options.zoomSpeed = lens->zoomSpeed;
    options.square = lens->square;
    options.nearestNeighbour = lens->nearestNeighbour;
    options.invertScroll = lens->invertScroll;
    options.changed = std::move(changed);
}

void QtDisplayOptions::setRoleDot(const std::optional<RoleDot> &dot)
{
    auto &options = Acheron::Core::roleDotOptions();
    options = {};
    if (dot) {
        options.enabled = true;
        options.bothStyles = dot->bothStyles;
        options.inProfile = dot->inProfile;
    }
    if (host)
        emit host->presentationChanged();
}

void QtDisplayOptions::setCompactConnections(const std::optional<CompactConnections> &connections,
                                             GithubOrgsLookup githubOrgs)
{
    auto &options = Acheron::Core::compactConnectionsOptions();
    options = {};
    if (!connections)
        return;
    options.enabled = true;
    options.iconSize = qBound(8, connections->iconSize, 128);
    options.spacing = qBound(0, connections->spacing, 32);
    options.githubOrgs = std::move(githubOrgs);
}

void QtDisplayOptions::setFolderSidebar(const std::optional<FolderSidebar> &folders)
{
    auto &options = Acheron::Core::folderSidebarOptions();
    options = {};
    if (folders) {
        options.enabled = true;
        options.sidebar = folders->sidebar;
        options.closeAllFolders = folders->closeAllFolders;
        options.closeAllHomeButton = folders->closeAllHomeButton;
        options.closeOthers = folders->closeOthers;
        options.closeServerFolder = folders->closeServerFolder;
        options.forceOpen = folders->forceOpen;
        options.keepIcons = folders->keepIcons;
        options.folderIcon = Acheron::Core::FolderSidebarOptions::FolderIcon(int(folders->folderIcon));
    }
    if (host)
        emit host->presentationChanged();
}

void QtDisplayOptions::setRoleColors(const RoleColors &colors)
{
    auto &options = Acheron::Core::roleColorOptions();
    const bool mentionsChanged = options.mentions != colors.mentions;
    options = { colors.mentions, colors.memberListHeaders, colors.voiceUsers, colors.messages,
                colors.messageSaturation };
    if (!host)
        return;
    if (mentionsChanged)
        emit host->displayTransformsChanged(); // mentions are in the rendered text
    emit host->presentationChanged();
}

void QtUiService::openPluginSettings(const QString &pluginId)
{
    if (pluginSettingsOpener())
        pluginSettingsOpener()(pluginId);
}

void QtUiService::promptText(const QString &title, const QString &label, const QString &initial, int maxLength,
                             std::function<void(const QString &)> done)
{
    auto *dialog = new QDialog(QApplication::activeWindow());
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(title);
    dialog->setMinimumWidth(380);
    auto *layout = new QVBoxLayout(dialog);
    auto *caption = new QLabel(label, dialog);
    caption->setWordWrap(true);
    layout->addWidget(caption);
    auto *field = new QLineEdit(initial, dialog);
    if (maxLength > 0)
        field->setMaxLength(maxLength);
    layout->addWidget(field);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, dialog);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, [dialog, field, done]() {
        done(field->text());
        dialog->accept();
    });
    dialog->open();
    field->setFocus();
}

void QtUiService::showForm(const QString &title, const QList<FormField> &fields,
                           std::function<void(const QString &, const QString &)> changed)
{
    auto *dialog = new QDialog(QApplication::activeWindow());
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(title);
    dialog->setMinimumWidth(420);
    auto *layout = new QVBoxLayout(dialog);
    for (const auto &field : fields) {
        if (field.choices.isEmpty()) {
            auto *box = new QCheckBox(field.label, dialog);
            box->setChecked(field.value == QLatin1String("true"));
            QObject::connect(box, &QCheckBox::toggled, dialog, [changed, key = field.key](bool on) {
                changed(key, on ? QStringLiteral("true") : QStringLiteral("false"));
            });
            layout->addWidget(box);
        } else {
            auto *caption = new QLabel(field.label, dialog);
            caption->setStyleSheet(QStringLiteral("font-weight: bold;"));
            layout->addWidget(caption);
            // Searchable: type to filter, like Discord's SearchableSelect
            auto *combo = new QComboBox(dialog);
            combo->setEditable(true);
            combo->setInsertPolicy(QComboBox::NoInsert);
            for (const auto &choice : field.choices)
                combo->addItem(choice.second, choice.first);
            combo->setCurrentIndex(combo->findData(field.value));
            combo->completer()->setCompletionMode(QCompleter::PopupCompletion);
            combo->completer()->setFilterMode(Qt::MatchContains);
            combo->completer()->setCaseSensitivity(Qt::CaseInsensitive);
            QObject::connect(combo, qOverload<int>(&QComboBox::activated), dialog,
                             [combo, changed, key = field.key](int index) {
                                 if (index >= 0)
                                     changed(key, combo->itemData(index).toString());
                             });
            layout->addWidget(combo);
        }
        if (!field.description.isEmpty()) {
            auto *help = new QLabel(field.description, dialog);
            help->setWordWrap(true);
            help->setStyleSheet(QStringLiteral("color: #949ba4;"));
            layout->addWidget(help);
        }
        layout->addSpacing(8);
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog->open();
}

void QtUiService::showFormDialog(const QString &title, const QList<FormField> &fields, const QString &confirmText,
                                 std::function<void(const QHash<QString, QString> &)> done)
{
    auto *dialog = new QDialog(QApplication::activeWindow());
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(title);
    dialog->setMinimumWidth(380);
    auto *layout = new QVBoxLayout(dialog);
    auto values = std::make_shared<QHash<QString, QString>>();
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, dialog);
    QPushButton *confirm = buttons->addButton(confirmText.isEmpty() ? QObject::tr("Save") : confirmText,
                                              QDialogButtonBox::AcceptRole);
    auto required = std::make_shared<QStringList>();
    auto refresh = [values, confirm, required]() {
        bool ok = true;
        for (const QString &key : *required)
            ok = ok && !values->value(key).trimmed().isEmpty();
        confirm->setEnabled(ok);
    };
    for (const auto &field : fields) {
        values->insert(field.key, field.value);
        auto *caption = new QLabel(field.label, dialog);
        caption->setStyleSheet(QStringLiteral("font-weight: bold;"));
        if (field.kind == FormField::Kind::Text) {
            layout->addWidget(caption);
            auto *edit = new QLineEdit(field.value, dialog);
            required->append(field.key);
            QObject::connect(edit, &QLineEdit::textChanged, dialog, [values, refresh, key = field.key](const QString &text) {
                values->insert(key, text);
                refresh();
            });
            layout->addWidget(edit);
        } else if (field.kind == FormField::Kind::Color) {
            layout->addWidget(caption);
            // Swatches like Discord's color picker, plus a custom color
            auto *row = new QHBoxLayout;
            row->setSpacing(4);
            auto *group = new QList<QPushButton *>;
            QObject::connect(dialog, &QObject::destroyed, [group]() { delete group; });
            auto style = [](QColor color, bool selected) {
                return QStringLiteral("QPushButton { background-color: %1; border-radius: 4px; border: 2px solid %2; }")
                        .arg(color.name(), selected ? QStringLiteral("white") : QStringLiteral("transparent"));
            };
            auto select = [values, group, style, key = field.key](quint32 rgb) {
                values->insert(key, QString::number(rgb));
                for (QPushButton *b : *group)
                    b->setStyleSheet(style(QColor::fromRgb(b->property("rgb").toUInt()),
                                           b->property("rgb").toUInt() == rgb));
            };
            for (quint32 rgb : field.colors) {
                auto *swatch = new QPushButton(dialog);
                swatch->setFixedSize(20, 20);
                swatch->setCursor(Qt::PointingHandCursor);
                swatch->setProperty("rgb", rgb);
                swatch->setToolTip(QColor::fromRgb(rgb).name());
                group->append(swatch);
                QObject::connect(swatch, &QPushButton::clicked, dialog, [select, rgb]() { select(rgb); });
                row->addWidget(swatch);
            }
            auto *custom = new QPushButton(QObject::tr("Custom..."), dialog);
            QObject::connect(custom, &QPushButton::clicked, dialog, [dialog, values, select, key = field.key]() {
                const QColor color = QColorDialog::getColor(QColor::fromRgb(values->value(key).toUInt()), dialog);
                if (color.isValid())
                    select(color.rgb() & 0xffffff);
            });
            row->addWidget(custom);
            row->addStretch();
            layout->addLayout(row);
            select(field.value.toUInt());
        } else if (field.choices.isEmpty()) {
            delete caption;
            auto *box = new QCheckBox(field.label, dialog);
            box->setChecked(field.value == QLatin1String("true"));
            QObject::connect(box, &QCheckBox::toggled, dialog, [values, key = field.key](bool on) {
                values->insert(key, on ? QStringLiteral("true") : QStringLiteral("false"));
            });
            layout->addWidget(box);
        } else {
            layout->addWidget(caption);
            auto *combo = new QComboBox(dialog);
            for (const auto &choice : field.choices)
                combo->addItem(choice.second, choice.first);
            combo->setCurrentIndex(combo->findData(field.value));
            QObject::connect(combo, qOverload<int>(&QComboBox::activated), dialog, [combo, values, key = field.key](int i) {
                values->insert(key, combo->itemData(i).toString());
            });
            layout->addWidget(combo);
        }
        if (!field.description.isEmpty()) {
            auto *help = new QLabel(field.description, dialog);
            help->setWordWrap(true);
            help->setStyleSheet(QStringLiteral("color: #949ba4;"));
            layout->addWidget(help);
        }
        layout->addSpacing(8);
    }
    layout->addWidget(buttons);
    refresh();
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, [dialog, values, done]() {
        dialog->accept();
        done(*values);
    });
    dialog->open();
}

bool QtUiService::confirm(Confirmation &confirmation)
{
    QMessageBox box(QApplication::activeWindow());
    box.setWindowTitle(confirmation.title);
    box.setText(confirmation.title);
    box.setInformativeText(confirmation.text);
    box.setIcon(confirmation.cancelText.isEmpty() ? QMessageBox::Information : QMessageBox::Question);
    QPushButton *ok = box.addButton(confirmation.confirmText.isEmpty() ? QObject::tr("OK") : confirmation.confirmText,
                                    QMessageBox::AcceptRole);
    if (!confirmation.cancelText.isEmpty())
        box.addButton(confirmation.cancelText, QMessageBox::RejectRole);
    box.setDefaultButton(ok);
    QCheckBox *check = nullptr;
    if (!confirmation.checkboxText.isEmpty()) {
        check = new QCheckBox(confirmation.checkboxText, &box);
        check->setChecked(confirmation.checkboxChecked);
        box.setCheckBox(check);
    }
    box.exec();
    if (check)
        confirmation.checkboxChecked = check->isChecked();
    return box.clickedButton() == ok;
}

std::function<void(const QString &, const QByteArray &, const QString &)> &QtUiService::fileAttacher()
{
    static std::function<void(const QString &, const QByteArray &, const QString &)> attacher;
    return attacher;
}

void QtUiService::attachFile(const QString &fileName, const QByteArray &data, const QString &mimeType)
{
    if (fileAttacher())
        fileAttacher()(fileName, data, mimeType);
}

int SessionExpressions::premiumType(Id accountId) const
{
    Acheron::Core::ClientInstance *instance = session->client(Acheron::Core::Snowflake(accountId));
    if (!instance)
        return 0;
    const auto me = instance->discord()->getMe();
    return me.premiumType.hasValue() ? int(me.premiumType.get()) : 0;
}

std::optional<CustomEmojiInfo> SessionExpressions::customEmoji(Id accountId, Id emojiId) const
{
    Acheron::Core::ClientInstance *instance = session->client(Acheron::Core::Snowflake(accountId));
    if (!instance || !instance->emojis())
        return std::nullopt;
    const auto found = instance->emojis()->customEmojiById(Acheron::Core::Snowflake(emojiId));
    if (!found)
        return std::nullopt;
    const Acheron::Discord::Emoji &emoji = found->first;
    CustomEmojiInfo info;
    info.id = emojiId;
    info.name = emoji.name.hasValue() ? emoji.name.get() : QString();
    info.animated = emoji.animated.hasValue() && emoji.animated.get();
    info.guildId = quint64(found->second);
    info.available = !emoji.available.hasValue() || emoji.available.get();
    info.managed = emoji.managed.hasValue() && emoji.managed.get();
    if (emoji.roles.hasValue())
        for (const auto &role : emoji.roles.get())
            info.roles.append(quint64(role));
    return info;
}

std::optional<StickerInfo> SessionExpressions::sticker(Id accountId, Id stickerId) const
{
    Acheron::Core::ClientInstance *instance = session->client(Acheron::Core::Snowflake(accountId));
    if (!instance || !instance->emojis())
        return std::nullopt;
    const auto found = instance->emojis()->stickerById(Acheron::Core::Snowflake(stickerId));
    if (!found) {
        // Discord's own packs (once the sticker picker loaded them)
        for (const auto &pack : instance->discord()->cachedStickerPacks())
            for (const auto &sticker : pack.stickers)
                if (quint64(sticker.id.get()) == stickerId) {
                    StickerInfo info;
                    info.id = stickerId;
                    info.name = sticker.name.get();
                    info.formatType = int(sticker.formatType.get());
                    info.standard = true;
                    return info;
                }
        return std::nullopt;
    }
    StickerInfo info;
    info.id = stickerId;
    info.name = found->name.get();
    info.formatType = int(found->formatType.get());
    info.guildId = found->guildId.hasValue() ? quint64(found->guildId.get()) : 0;
    info.available = !found->available.hasValue() || found->available.get();
    return info;
}

QList<Id> SessionExpressions::ownRoles(Id accountId, Id guildId) const
{
    Acheron::Core::ClientInstance *instance = session->client(Acheron::Core::Snowflake(accountId));
    if (!instance || !instance->users())
        return {};
    QList<Id> out;
    if (auto roles = instance->users()->getMemberRoles(Acheron::Core::Snowflake(guildId),
                                                       Acheron::Core::Snowflake(accountId)))
        for (const auto &role : *roles)
            out.append(quint64(role));
    return out;
}

bool SessionExpressions::hasChannelPermission(Id accountId, Id channelId, quint64 permission) const
{
    Acheron::Core::ClientInstance *instance = session->client(Acheron::Core::Snowflake(accountId));
    if (!instance)
        return true;
    const Acheron::Core::Snowflake channel(channelId);
    if (!instance->discord()->getGuildIdForChannel(channel).isValid())
        return true; // DMs and group DMs
    return instance->permissions()->hasChannelPermission(instance->accountId(), channel,
                                                         Acheron::Discord::Permission(permission));
}

void SessionExpressions::setChatBypass(bool emojis, bool stickers)
{
    Acheron::Core::expressionBypass().emojis = emojis;
    Acheron::Core::expressionBypass().stickers = stickers;
}

bool SessionExpressions::hasGuildPermission(Id accountId, Id guildId, quint64 permission) const
{
    Acheron::Core::ClientInstance *instance = session->client(Acheron::Core::Snowflake(accountId));
    if (!instance || !guildId)
        return false;
    return instance->permissions()->hasGuildPermission(instance->accountId(), Acheron::Core::Snowflake(guildId),
                                                       Acheron::Discord::Permission(permission));
}

void SessionExpressions::setStreamQualityBypass(bool on)
{
    Acheron::Core::expressionBypass().streamQuality = on;
}

void SessionExpressions::setOversizeImageHandler(ImageShrinker shrink)
{
    if (!shrink) {
        Acheron::Core::expressionBypass().shrinkImage = nullptr;
        return;
    }
    Acheron::Core::expressionBypass().shrinkImage =
            [shrink](const QString &fileName, const QString &mimeType, const QByteArray &data,
                     qint64 limit) -> std::optional<Acheron::Core::ShrunkImage> {
        const std::optional<AttachedFile> out = shrink({ fileName, mimeType, data }, limit);
        if (!out)
            return std::nullopt;
        return Acheron::Core::ShrunkImage { out->fileName, out->mimeType, out->data };
    };
}

std::optional<GuildInfo> SessionGuilds::guild(Id accountId, Id guildId) const
{
    Acheron::Core::ClientInstance *instance = session->client(Acheron::Core::Snowflake(accountId));
    if (!instance || !guildId)
        return std::nullopt;
    const auto guild = instance->getGuild(Acheron::Core::Snowflake(guildId));
    if (!guild)
        return std::nullopt;
    GuildInfo out;
    out.id = guildId;
    out.name = guild->name.get();
    out.ownerId = guild->ownerId.hasValue() ? quint64(guild->ownerId.get()) : 0;
    auto roles = instance->getRolesForGuild(Acheron::Core::Snowflake(guildId));
    // Discord's order: highest position first, the older role first on ties
    std::sort(roles.begin(), roles.end(), [](const Acheron::Discord::Role &a, const Acheron::Discord::Role &b) {
        if (a.position.get() != b.position.get())
            return a.position.get() > b.position.get();
        return quint64(a.id.get()) < quint64(b.id.get());
    });
    for (const auto &role : roles) {
        RoleInfo info;
        info.id = quint64(role.id.get());
        info.name = role.name.get();
        info.color = role.hasColor() ? quint32(role.color.get()) : 0;
        info.position = role.position.get();
        info.permissions = role.permissions.get().toInt();
        info.iconUrl = Acheron::Discord::Cdn::roleIcon(role.id.get(), role.icon.valueOr(), 32).toString();
        info.unicodeEmoji = role.unicodeEmoji.valueOr();
        out.roles.append(info);
    }
    const Acheron::Core::Snowflake id(guildId);
    out.description = guild->description.valueOr();
    if (!guild->icon.get().isEmpty())
        out.iconUrl = Acheron::Discord::Cdn::guildIcon(id, guild->icon.get(), 512).toString();
    if (!guild->banner.valueOr().isEmpty())
        out.bannerUrl = Acheron::Discord::Cdn::guildBanner(id, guild->banner.valueOr(), 1024).toString();
    const QJsonObject extra = QJsonDocument::fromJson(guild->extraJson.toUtf8()).object();
    out.vanityUrlCode = extra.value(QStringLiteral("vanity_url_code")).toString();
    out.preferredLocale = extra.value(QStringLiteral("preferred_locale")).toString();
    if (guild->verificationLevel.hasValue())
        out.verificationLevel = int(guild->verificationLevel.get());
    out.premiumSubscriptionCount = guild->premiumSubscriptionCount.valueOr(0);
    out.premiumTier = guild->premiumTier.hasValue() ? int(guild->premiumTier.get()) : 0;
    if (instance->users())
        if (const auto self = instance->users()->getMember(id, instance->accountId()); self && self->joinedAt.hasValue())
            out.joinedAt = self->joinedAt.get();
    if (auto *perms = instance->permissions()) {
        int count = 0;
        for (const auto &channel : instance->getGuildChannels(id))
            if (!channel.isThread() &&
                perms->hasChannelPermission(instance->accountId(), channel.id.get(), Acheron::Discord::Permission::VIEW_CHANNEL))
                ++count;
        out.channelCount = count;
    }
    return out;
}

bool SessionUsers::isKnown(Id accountId, Id userId) const
{
    Acheron::Core::ClientInstance *instance = session->client(Acheron::Core::Snowflake(accountId));
    return instance && instance->users() && instance->users()->getUser(Acheron::Core::Snowflake(userId)).has_value();
}

void SessionUsers::fetchUser(Id accountId, Id userId, Callback done)
{
    QPointer<Acheron::Core::ClientInstance> instance = session->client(Acheron::Core::Snowflake(accountId));
    if (!instance) {
        done({});
        return;
    }
    instance->discord()->fetchUser(Acheron::Core::Snowflake(userId),
                                   [instance, done](const Acheron::Discord::HttpResponse &response,
                                                    const std::optional<Acheron::Discord::User> &user) {
                                       Result result;
                                       result.status = response.statusCode;
                                       if (response.rateLimited())
                                           result.retryAfterMs = int(QJsonDocument::fromJson(response.body)
                                                                             .object()
                                                                             .value(QStringLiteral("retry_after"))
                                                                             .toDouble(1.0) *
                                                                     1000);
                                       if (user && instance && instance->users()) {
                                           instance->users()->saveUsers({ *user });
                                           result.ok = true;
                                       }
                                       done(result);
                                   });
}

void SessionUsers::refreshMessage(Id accountId, Id channelId, Id messageId)
{
    if (auto *instance = session->client(Acheron::Core::Snowflake(accountId)))
        instance->messages()->refreshMessage(Acheron::Core::Snowflake(channelId), Acheron::Core::Snowflake(messageId));
}

std::function<QList<Id>(Id)> &SessionGuilds::guildIdsProvider()
{
    static std::function<QList<Id>(Id)> provider;
    return provider;
}

QList<Id> SessionGuilds::guildIds(Id accountId) const
{
    return guildIdsProvider() ? guildIdsProvider()(accountId) : QList<Id>();
}

QList<Id> SessionGuilds::memberIds(Id accountId, Id guildId) const
{
    Acheron::Core::ClientInstance *instance = session->client(Acheron::Core::Snowflake(accountId));
    if (!instance || !instance->users())
        return {};
    QList<Id> out;
    for (const auto &member : instance->users()->getKnownMembers(Acheron::Core::Snowflake(guildId)))
        if (member.user.hasValue())
            out.append(quint64(member.user->id.get()));
    return out;
}

std::optional<ChannelInfo> SessionGuilds::channel(Id accountId, Id channelId) const
{
    Acheron::Core::ClientInstance *instance = session->client(Acheron::Core::Snowflake(accountId));
    if (!instance || !channelId)
        return std::nullopt;
    const auto channel = instance->getChannel(Acheron::Core::Snowflake(channelId));
    if (!channel)
        return std::nullopt;
    ChannelInfo out;
    out.id = channelId;
    out.guildId = channel->guildId.hasValue() ? quint64(channel->guildId.get()) : 0;
    out.name = channel->name.hasValue() ? channel->name.get() : QString();
    if (channel->permissionOverwrites.hasValue()) {
        for (const auto &overwrite : channel->permissionOverwrites.get()) {
            PermissionOverwriteInfo info;
            info.type = overwrite.type.get() == Acheron::Discord::PermissionOverwrite::Type::Member
                                ? PermissionOverwriteInfo::Type::Member
                                : PermissionOverwriteInfo::Type::Role;
            info.id = quint64(overwrite.id.get());
            info.allow = overwrite.allow.get().toInt();
            info.deny = overwrite.deny.get().toInt();
            out.overwrites.append(info);
        }
    }
    out.type = channel->type.hasValue() ? int(channel->type.get()) : 0;
    out.topic = channel->topic.valueOr();
    out.lastMessageId = channel->lastMessageId.hasValue() && channel->lastMessageId.get().isValid()
                                ? quint64(channel->lastMessageId.get())
                                : 0;
    out.rateLimitPerUser = channel->rateLimitPerUser.valueOr(0);
    if (channel->defaultSortOrder.hasValue())
        out.defaultSortOrder = channel->defaultSortOrder.get();
    out.flags = channel->flags.hasValue() ? quint64(channel->flags.get().toInt()) : 0;
    if (channel->availableTags.hasValue())
        for (const auto &tag : channel->availableTags.get())
            out.availableTags.append(tag.name.get());
    const QJsonObject extra = QJsonDocument::fromJson(channel->extraJson.toUtf8()).object();
    out.nsfw = extra.value(QStringLiteral("nsfw")).toBool();
    if (extra.value(QStringLiteral("bitrate")).isDouble())
        out.bitrate = extra.value(QStringLiteral("bitrate")).toInt();
    out.hasRtcRegion = extra.contains(QStringLiteral("rtc_region"));
    out.rtcRegion = extra.value(QStringLiteral("rtc_region")).toString();
    if (extra.value(QStringLiteral("video_quality_mode")).isDouble())
        out.videoQualityMode = extra.value(QStringLiteral("video_quality_mode")).toInt();
    out.defaultAutoArchiveDuration = extra.value(QStringLiteral("default_auto_archive_duration")).toInt();
    out.defaultThreadRateLimitPerUser = extra.value(QStringLiteral("default_thread_rate_limit_per_user")).toInt();
    if (extra.value(QStringLiteral("default_forum_layout")).isDouble())
        out.defaultForumLayout = extra.value(QStringLiteral("default_forum_layout")).toInt();
    const QJsonObject reaction = extra.value(QStringLiteral("default_reaction_emoji")).toObject();
    out.defaultReactionEmojiName = reaction.value(QStringLiteral("emoji_name")).toString();
    out.defaultReactionEmojiId = reaction.value(QStringLiteral("emoji_id")).toString().toULongLong();
    out.lastPinTimestamp = QDateTime::fromString(extra.value(QStringLiteral("last_pin_timestamp")).toString(),
                                                 Qt::ISODateWithMs);
    return out;
}

std::optional<MemberInfo> SessionGuilds::member(Id accountId, Id guildId, Id userId) const
{
    Acheron::Core::ClientInstance *instance = session->client(Acheron::Core::Snowflake(accountId));
    if (!instance || !instance->users())
        return std::nullopt;
    const Acheron::Core::Snowflake user(userId);
    const auto member = guildId ? instance->users()->getMember(Acheron::Core::Snowflake(guildId), user) : std::nullopt;
    auto known = instance->users()->getUser(user);
    if (!known && member && member->user.hasValue())
        known = member->user.get();
    if (!known)
        return std::nullopt;
    MemberInfo out;
    out.userId = userId;
    out.username = known->username.get();
    out.globalName = known->globalName.valueOr();
    out.avatarUrl = Acheron::Discord::Cdn::userAvatar(user, known->avatar.valueOr(), 64).toString();
    if (member) {
        out.isMember = true;
        out.nick = member->nick.valueOr();
        if (member->roles.hasValue())
            for (const auto &role : member->roles.get())
                out.roles.append(quint64(role));
    }
    return out;
}

void SessionGuilds::requestMembers(Id accountId, Id guildId, const QList<Id> &userIds)
{
    Acheron::Core::ClientInstance *instance = session->client(Acheron::Core::Snowflake(accountId));
    if (!instance || !guildId || userIds.isEmpty())
        return;
    // Discord takes at most 100 user ids per request
    QList<Acheron::Core::Snowflake> ids;
    for (Id id : userIds) {
        ids.append(Acheron::Core::Snowflake(id));
        if (ids.size() == 100) {
            instance->discord()->requestGuildMembers(Acheron::Core::Snowflake(guildId), ids, false);
            ids.clear();
        }
    }
    if (!ids.isEmpty())
        instance->discord()->requestGuildMembers(Acheron::Core::Snowflake(guildId), ids, false);
}

void QtUiService::insertIntoMessageBox(const QString &text)
{
    if (messageBoxInserter())
        messageBoxInserter()(text);
}

void QtUiService::pickTimestamp(std::function<void(const QString &)> done)
{
    // Discord's timestamp picker: when, which format, and a live preview
    auto *dialog = new QDialog(QApplication::activeWindow());
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QObject::tr("Timestamp Picker"));
    auto *layout = new QVBoxLayout(dialog);
    auto *when = new QDateTimeEdit(QDateTime::currentDateTime(), dialog);
    when->setCalendarPopup(true);
    when->setDisplayFormat(QLocale().dateTimeFormat(QLocale::ShortFormat));
    layout->addWidget(when);
    layout->addWidget(new QLabel(QObject::tr("<b>Timestamp Format</b>"), dialog));
    auto *format = new QComboBox(dialog);
    layout->addWidget(format);
    layout->addWidget(new QLabel(QObject::tr("<b>Preview</b>"), dialog));
    auto *preview = new QLabel(dialog);
    preview->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(preview);
    auto *buttons = new QDialogButtonBox(dialog);
    QPushButton *insert = buttons->addButton(QObject::tr("Insert"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Cancel);
    layout->addWidget(buttons);

    // Upstream also offers "s" and "S"; Qord's renderer knows Discord's seven styles
    static const char *const styles[] = { "", "t", "T", "d", "D", "f", "F", "R" };
    const auto markup = [when, format]() {
        const qint64 secs = when->dateTime().toSecsSinceEpoch();
        const QString style = format->currentData().toString();
        return style.isEmpty() ? QStringLiteral("<t:%1>").arg(secs) : QStringLiteral("<t:%1:%2>").arg(secs).arg(style);
    };
    const auto refresh = [when, format, preview, markup]() {
        const qint64 secs = when->dateTime().toSecsSinceEpoch();
        const int current = format->currentIndex();
        format->blockSignals(true);
        format->clear();
        for (const char *style : styles) {
            const QChar c = *style ? QChar::fromLatin1(*style) : QChar('f');
            format->addItem(QStringLiteral("%1  —  %2")
                                    .arg(*style ? QString::fromLatin1(style) : QObject::tr("default"),
                                         Acheron::Core::Markdown::formatDiscordTimestamp(secs, c, QDateTime::currentDateTime(), QLocale())),
                            QString::fromLatin1(style));
        }
        format->setCurrentIndex(qMax(0, current));
        format->blockSignals(false);
        const QString style = format->currentData().toString();
        preview->setText(QStringLiteral("%1  (%2)").arg(
                Acheron::Core::Markdown::formatDiscordTimestamp(secs, style.isEmpty() ? QChar('f') : style.at(0),
                                                                QDateTime::currentDateTime(), QLocale()),
                markup()));
    };
    refresh();
    QObject::connect(when, &QDateTimeEdit::dateTimeChanged, dialog, refresh);
    QObject::connect(format, qOverload<int>(&QComboBox::currentIndexChanged), dialog, refresh);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    QObject::connect(insert, &QPushButton::clicked, dialog, [dialog, markup, done]() {
        done(markup());
        dialog->accept();
    });
    dialog->open();
}

void QtDisplayOptions::setRelativeTimeRoundsDown(bool floor)
{
    Acheron::Core::Markdown::setRelativeTimeRoundsDown(floor);
    // Re-render what is on screen (signals are public; the window listens)
    if (host)
        emit host->displayTransformsChanged();
}

void SessionMessageActions::deleteMessage(Id accountId, Id channelId, Id messageId)
{
    auto *client = session->client(Acheron::Core::Snowflake(accountId));
    if (!client)
        return;
    client->discord()->deleteMessage(Acheron::Core::Snowflake(channelId), Acheron::Core::Snowflake(messageId));
}

void SessionMessageActions::setPinned(Id accountId, Id channelId, Id messageId, bool pinned)
{
    auto *client = session->client(Acheron::Core::Snowflake(accountId));
    if (!client)
        return;
    if (pinned)
        client->discord()->pinMessage(Acheron::Core::Snowflake(channelId), Acheron::Core::Snowflake(messageId));
    else
        client->discord()->unpinMessage(Acheron::Core::Snowflake(channelId), Acheron::Core::Snowflake(messageId));
}

void SessionMessageActions::setReaction(Id accountId, Id channelId, Id messageId, const QString &emoji, bool add)
{
    auto *client = session->client(Acheron::Core::Snowflake(accountId));
    if (!client)
        return;
    if (add)
        client->discord()->addReaction(Acheron::Core::Snowflake(channelId), Acheron::Core::Snowflake(messageId), emoji);
    else
        client->discord()->removeReaction(Acheron::Core::Snowflake(channelId), Acheron::Core::Snowflake(messageId), emoji);
}

void SessionMessageActions::setMessageFlags(Id accountId, Id channelId, Id messageId, quint64 flags)
{
    if (auto *instance = session->client(Acheron::Core::Snowflake(accountId)))
        instance->discord()->setMessageFlags(Acheron::Core::Snowflake(channelId), Acheron::Core::Snowflake(messageId),
                                             flags);
}

void SessionMessageService::refreshMessage(Id accountId, Id channelId, Id messageId)
{
    if (auto *instance = session->client(Acheron::Core::Snowflake(accountId)))
        instance->messages()->refreshMessage(Acheron::Core::Snowflake(channelId), Acheron::Core::Snowflake(messageId));
}

void SessionMessageService::removeMessage(Id accountId, Id channelId, Id messageId)
{
    if (auto *instance = session->client(Acheron::Core::Snowflake(accountId)))
        instance->messages()->removeKeptMessage(Acheron::Core::Snowflake(channelId), Acheron::Core::Snowflake(messageId));
}

void SessionMessageService::refreshFilters()
{
    if (host)
        host->requestMessageFilterRefresh();
}

void SessionReactions::fetchReactionUsers(Id accountId, Id channelId, Id messageId, const QString &emojiKey, int type,
                                          Callback done)
{
    QPointer<Acheron::Core::ClientInstance> instance = session->client(Acheron::Core::Snowflake(accountId));
    if (!instance) {
        done(false, {});
        return;
    }
    instance->discord()->fetchReactionUsers(
            Acheron::Core::Snowflake(channelId), Acheron::Core::Snowflake(messageId), emojiKey, type,
            [instance, done](const Acheron::Core::Result<QList<Acheron::Discord::User>> &result) {
                if (!result.success() || !instance) {
                    done(false, {});
                    return;
                }
                instance->users()->saveUsers(*result.value);
                QList<Id> ids;
                for (const auto &user : *result.value)
                    ids.append(quint64(user.id.get()));
                done(true, ids);
            });
}

std::function<void(Id, Id, Id)> &SessionReactions::onChanged()
{
    static std::function<void(Id, Id, Id)> changed;
    return changed;
}

void SessionReactions::reactionUsersChanged(Id accountId, Id channelId, Id messageId)
{
    if (onChanged())
        onChanged()(accountId, channelId, messageId);
}

static QString pluginDataDirectory()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("plugins"));
}

namespace {
// Secret plugin settings (API keys, session tokens) in the system keychain
class KeychainSecretStore : public SecretStore
{
public:
    QString read(const QString &key) const override
    {
        auto result = Acheron::Core::TokenStore::loadSecret(key);
        return result.success() ? *result.value : QString();
    }
    bool write(const QString &key, const QString &value) override
    {
        return Acheron::Core::TokenStore::saveSecret(key, value);
    }
    bool remove(const QString &key) override { return Acheron::Core::TokenStore::deleteSecret(key); }
};
} // namespace

ClientPlugins::ClientPlugins(Acheron::Core::Session *session)
    : members(session), relationships(session), messages(session), messageActions(session), expressions(session),
      reactions(session), guilds(session), users(session), messageLookup(session), voiceStates(session), activity(session), devices(session), presences(session)
{
    QElapsedTimer timer;
    timer.start();

    Registry registry;
    registerBuiltinPlugins(registry);
    storage = std::make_unique<FileStorage>(pluginDataDirectory());
    // The mock server never touches the keychain; its secrets last one run
    if (!Qord::DevMock::enabled()) {
        static KeychainSecretStore keychain;
        PluginSettings::secretStore() = &keychain;
    }
    Services services;
    services.network = &network;
    services.members = &members;
    services.clipboard = &clipboard;
    services.relationships = &relationships;
    services.ui = &ui;
    services.messages = &messages;
    services.messageActions = &messageActions;
    services.displayOptions = &displayOptions;
    services.readStates = &readStates;
    services.expressions = &expressions;
    services.reactions = &reactions;
    services.guilds = &guilds;
    services.users = &users;
    services.privateChannels = &privateChannels;
    services.audio = &audio;
    services.messageLookup = &messageLookup;
    services.voiceStates = &voiceStates;
    services.activity = &activity;
    services.sessions = &devices;
    services.presences = &presences;
    pluginHost = std::make_unique<PluginHost>(registry, storage.get(), services,
                                              QDir(pluginDataDirectory()).filePath(QStringLiteral("data")));
    messages.host = pluginHost.get();
    displayOptions.host = pluginHost.get();
    audio.host = pluginHost.get();
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
