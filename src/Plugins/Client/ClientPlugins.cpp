#include "ClientPlugins.hpp"

#include "Core/ClientInstance.hpp"
#include "Core/Logging.hpp"
#include "Core/MessageManager.hpp"
#include "Core/NetworkRequest.hpp"
#include "Core/RelationshipManager.hpp"
#include "Core/Session.hpp"
#include "Core/UserManager.hpp"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QLabel>
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
        QObject::connect(copy, &QPushButton::clicked, dialog, [value] { QGuiApplication::clipboard()->setText(value); });
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    layout->addWidget(buttons);
    dialog->show();
}

void QtUiService::openUrl(const QUrl &url)
{
    QDesktopServices::openUrl(url);
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

static QString pluginDataDirectory()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("plugins"));
}

ClientPlugins::ClientPlugins(Acheron::Core::Session *session)
    : members(session), relationships(session), messages(session)
{
    QElapsedTimer timer;
    timer.start();

    Registry registry;
    registerBuiltinPlugins(registry);
    storage = std::make_unique<FileStorage>(pluginDataDirectory());
    Services services;
    services.network = &network;
    services.members = &members;
    services.clipboard = &clipboard;
    services.relationships = &relationships;
    services.ui = &ui;
    services.messages = &messages;
    pluginHost = std::make_unique<PluginHost>(registry, storage.get(), services,
                                              QDir(pluginDataDirectory()).filePath(QStringLiteral("data")));
    messages.host = pluginHost.get();
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
