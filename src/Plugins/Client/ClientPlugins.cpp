#include "ClientPlugins.hpp"
#include <QPointer>

#include "Core/ClientInstance.hpp"
#include "Core/Emoji/EmojiManager.hpp"
#include "Core/PermissionManager.hpp"
#include "Core/Logging.hpp"
#include "Core/MessageManager.hpp"
#include "Core/NetworkRequest.hpp"
#include "Core/Presence/PresenceBadge.hpp"
#include "Core/RelationshipManager.hpp"
#include "Core/OwnerCrown.hpp"
#include "Core/RoleColors.hpp"
#include "Core/Session.hpp"
#include "Core/TokenStore.hpp"
#include "Qord/DevMock.hpp"
#include "Core/UserManager.hpp"
#include "Core/Markdown/Parser.hpp"
#include "Discord/CdnUrls.hpp"
#include "Discord/HttpClient.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QMessageBox>
#include <QClipboard>
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

// body null: GET. Headers can hold API keys, so nothing here logs them.
NetworkService::Handle QtNetworkService::send(const QUrl &url, const Headers &headers, const QByteArray *body,
                                              Callback callback)
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
    for (const auto &header : headers)
        request.setRawHeader(header.first, header.second);
    QNetworkReply *reply = body ? nam->post(request, *body) : nam->get(request);
    replies.insert(handle, reply);

    connect(reply, &QNetworkReply::downloadProgress, this, [reply](qint64 received, qint64) {
        if (received > MaxResponseBytes)
            reply->abort();
    });
    const bool keepErrorBody = body != nullptr;
    connect(reply, &QNetworkReply::finished, this, [this, handle, reply, keepErrorBody, callback = std::move(callback)] {
        reply->deleteLater();
        if (!replies.remove(handle))
            return; // cancelled
        Reply r;
        r.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        r.ok = reply->error() == QNetworkReply::NoError && r.status >= 200 && r.status < 300;
        if (r.ok || (keepErrorBody && r.status))
            r.body = reply->readAll();
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
    QDesktopServices::openUrl(url);
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
    if (!found)
        return std::nullopt;
    StickerInfo info;
    info.id = stickerId;
    info.name = found->name.get();
    info.formatType = int(found->formatType.get());
    info.guildId = found->guildId.hasValue() ? quint64(found->guildId.get()) : 0;
    info.available = !found->available.hasValue() || found->available.get();
    info.standard = false; // only servers' stickers are known
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
    Acheron::Core::expressionBypass() = { emojis, stickers };
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
      reactions(session), guilds(session), users(session)
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
    pluginHost = std::make_unique<PluginHost>(registry, storage.get(), services,
                                              QDir(pluginDataDirectory()).filePath(QStringLiteral("data")));
    messages.host = pluginHost.get();
    displayOptions.host = pluginHost.get();
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
