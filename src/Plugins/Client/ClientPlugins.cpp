#include "ClientPlugins.hpp"

#include "Core/ClientInstance.hpp"
#include "Core/Logging.hpp"
#include "Core/MessageManager.hpp"
#include "Core/NetworkRequest.hpp"
#include "Core/Presence/PresenceBadge.hpp"
#include "Core/RelationshipManager.hpp"
#include "Core/Session.hpp"
#include "Core/UserManager.hpp"
#include "Core/Markdown/Parser.hpp"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QLineEdit>
#include <QLocale>
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

static QString pluginDataDirectory()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("plugins"));
}

ClientPlugins::ClientPlugins(Acheron::Core::Session *session)
    : members(session), relationships(session), messages(session), messageActions(session)
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
    services.messageActions = &messageActions;
    services.displayOptions = &displayOptions;
    services.readStates = &readStates;
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
