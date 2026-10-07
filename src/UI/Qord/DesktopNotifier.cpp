#include "DesktopNotifier.hpp"

#include "Core/ClientInstance.hpp"
#include "Core/Presence/PresenceManager.hpp"
#include "Core/ReadStateManager.hpp"
#include "Core/UserManager.hpp"

#include <QApplication>
#include <QMenu>
#include <QSettings>
#include <QSystemTrayIcon>
#include <QWidget>

namespace Acheron {
namespace UI {

namespace {
constexpr const char *EnabledKey = "notifications/desktop";
}

DesktopNotifier::DesktopNotifier(QWidget *window) : QObject(window), window(window)
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;
    tray = new QSystemTrayIcon(window->windowIcon().isNull() ? qApp->windowIcon() : window->windowIcon(), this);
    tray->setToolTip(QApplication::applicationDisplayName());
    auto *menu = new QMenu(window);
    menu->addAction(tr("Open"), this, [this]() {
        this->window->showNormal();
        this->window->raise();
        this->window->activateWindow();
    });
    menu->addAction(tr("Quit"), qApp, &QApplication::quit);
    tray->setContextMenu(menu);
    connect(tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
            this->window->showNormal();
            this->window->raise();
            this->window->activateWindow();
        }
    });
    connect(tray, &QSystemTrayIcon::messageClicked, this, [this]() {
        this->window->showNormal();
        this->window->raise();
        this->window->activateWindow();
        if (lastChannel.isValid())
            emit openRequested(lastAccount, lastChannel, lastMessage);
    });
    tray->show();
}

bool DesktopNotifier::enabled()
{
    return QSettings().value(EnabledKey, true).toBool();
}

void DesktopNotifier::setEnabled(bool enabled)
{
    QSettings().setValue(EnabledKey, enabled);
}

void DesktopNotifier::consider(Core::ClientInstance *instance, const Discord::Message &message, bool mentionsMe,
                               Core::Snowflake viewedChannelId)
{
    if (!instance || !enabled())
        return;
    const bool isDm = !message.guildId.hasValue();
    if (!isDm && !mentionsMe)
        return;
    const Core::Snowflake channelId = message.channelId.get();
    // Looking at it already
    if (window->isActiveWindow() && channelId == viewedChannelId)
        return;
    // Do Not Disturb silences everything, like Discord
    if (instance->presences()->badge(instance->accountId()).status == Discord::StatusType::DND)
        return;
    if (instance->readState()->isChannelMuted(channelId))
        return;
    if (!isDm && instance->readState()->isGuildMuted(message.guildId.get()))
        return;

    const Core::Snowflake guildId = isDm ? Core::Snowflake::Invalid : message.guildId.get();
    QString title = instance->users()->getAuthorDisplayName(message.author.get(), guildId);
    if (!isDm) {
        const auto channel = instance->getChannel(channelId);
        if (channel && channel->name.hasValue())
            title += QStringLiteral(" (#%1)").arg(channel->name.get());
    }
    QString body = message.content.get();
    if (body.isEmpty() && message.attachments.hasValue() && !message.attachments->isEmpty())
        body = tr("Uploaded a file");
    if (body.size() > 200)
        body = body.left(200) + QStringLiteral("…");

    lastAccount = instance->accountId();
    lastChannel = channelId;
    lastMessage = message.id.get();
    if (tray)
        tray->showMessage(title, body, QSystemTrayIcon::Information, 5000);
    QApplication::alert(window, 0);
}

} // namespace UI
} // namespace Acheron
