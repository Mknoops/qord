#include "UserPanel.hpp"
#include "Qord/Preferences.hpp"

#include "Core/ClientInstance.hpp"
#include "Core/ImageManager.hpp"
#include "Core/Theme/Icons.hpp"
#include "Core/Theme/Manager.hpp"
#include "Discord/CdnUrls.hpp"
#include "UI/StatusIndicator.hpp"
#include "Plugins/PluginHost.hpp"

#ifndef ACHERON_NO_VOICE
#include "Core/Audio/VoiceManager.hpp"
#endif

#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSvgRenderer>
#include <QToolButton>
#include <QVBoxLayout>

namespace Acheron {
namespace UI {

using Core::Theme::Token;
namespace Icons = Core::Theme::Icons;

namespace {

constexpr int AvatarSize = 32;

QPixmap circular(const QPixmap &source, int size, qreal dpr)
{
    QPixmap out(QSize(size, size) * dpr);
    out.setDevicePixelRatio(dpr);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath clip;
    clip.addEllipse(QRectF(0, 0, size, size));
    p.setClipPath(clip);
    p.drawPixmap(QRect(0, 0, size, size), source);
    return out;
}

QToolButton *panelButton(QWidget *parent, const QString &tooltip)
{
    auto *b = new QToolButton(parent);
    b->setAutoRaise(true);
    b->setIconSize(QSize(20, 20));
    b->setFixedSize(32, 32);
    b->setCursor(Qt::PointingHandCursor);
    b->setToolTip(tooltip);
    b->setObjectName(QStringLiteral("UserPanelButton"));
    return b;
}

QString statusLabel(const QString &status)
{
    if (status == QLatin1String("idle"))
        return UserPanel::tr("Idle");
    if (status == QLatin1String("dnd"))
        return UserPanel::tr("Do Not Disturb");
    if (status == QLatin1String("invisible"))
        return UserPanel::tr("Invisible");
    return UserPanel::tr("Online");
}

} // namespace

UserPanel::UserPanel(Core::ImageManager *images, QWidget *parent) : QWidget(parent), images(images)
{
    setObjectName(QStringLiteral("UserPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedHeight(52);
    connect(&Qord::Preferences::instance(), &Qord::Preferences::changed, this, [this](const QString &key) {
        if (key.startsWith(QLatin1String("streamer/")))
            refresh();
    });

    auto *layout = new QHBoxLayout(this);
    panelLayout = layout;
    layout->setContentsMargins(8, 0, 8, 0);
    layout->setSpacing(2);

    identity = new QWidget(this);
    identity->setObjectName(QStringLiteral("UserPanelIdentity"));
    identity->setAttribute(Qt::WA_StyledBackground, true);
    identity->setCursor(Qt::PointingHandCursor);
    identity->setToolTip(tr("Set status"));
    auto *idLayout = new QHBoxLayout(identity);
    idLayout->setContentsMargins(2, 4, 6, 4);
    idLayout->setSpacing(8);

    avatarLabel = new QLabel(identity);
    avatarLabel->setFixedSize(AvatarSize, AvatarSize);
    idLayout->addWidget(avatarLabel);
    statusDot = attachStatusDot(avatarLabel, QRect(0, 0, AvatarSize, AvatarSize), 10);

    auto *textBox = new QVBoxLayout;
    textBox->setContentsMargins(0, 0, 0, 0);
    textBox->setSpacing(0);
    nameLabel = new QLabel(identity);
    nameLabel->setObjectName(QStringLiteral("UserPanelName"));
    subtitleLabel = new QLabel(identity);
    subtitleLabel->setObjectName(QStringLiteral("UserPanelSubtitle"));
    textBox->addWidget(nameLabel);
    textBox->addWidget(subtitleLabel);
    idLayout->addLayout(textBox, 1);
    layout->addWidget(identity, 1);

    muteButton = panelButton(this, tr("Mute"));
    deafenButton = panelButton(this, tr("Deafen"));
    settingsButton = panelButton(this, tr("User Settings"));
    settingsButton->setIcon(Icons::icon(Icons::Name::Settings, Token::MutedText));
    layout->addWidget(muteButton);
    layout->addWidget(deafenButton);
    layout->addWidget(settingsButton);

    connect(muteButton, &QToolButton::clicked, this, [this]() {
        if (deafened) {
            deafened = false;
            muted = false;
        } else {
            muted = !muted;
        }
        refreshButtons();
        applyVoiceFlags();
    });
    connect(deafenButton, &QToolButton::clicked, this, [this]() {
        deafened = !deafened;
        refreshButtons();
        applyVoiceFlags();
    });
    connect(settingsButton, &QToolButton::clicked, this, &UserPanel::settingsRequested);

    connect(&Core::Theme::Manager::instance(), &Core::Theme::Manager::themeChanged, this, [this]() {
        settingsButton->setIcon(Icons::icon(Icons::Name::Settings, Token::MutedText));
        refreshButtons();
        rebuildPluginButtons();
    });
    if (auto *host = Qord::Plugins::PluginHost::instance())
        connect(host, &Qord::Plugins::PluginHost::userPanelChanged, this, &UserPanel::rebuildPluginButtons);
    connect(images, &Core::ImageManager::imageFetched, this, [this](const QUrl &url, const QSize &, const QPixmap &) {
        if (url == avatarUrl)
            refreshAvatar();
    });

    refreshButtons();
    refresh();
}

void UserPanel::setInstance(Core::ClientInstance *newInstance)
{
    for (const auto &c : instanceConnections)
        disconnect(c);
    instanceConnections.clear();
    instance = newInstance;
    avatarLabel->clear();

    if (instance) {
        instanceConnections.append(connect(instance->presences(), &Core::PresenceManager::presencesChanged, this,
                                           [this](const QList<Core::Snowflake> &ids) {
                                               if (instance && ids.contains(instance->accountId()))
                                                   refresh();
                                           }));
        instanceConnections.append(
                connect(instance, &Core::ClientInstance::detailsUpdated, this, [this](const Core::AccountInfo &) {
                    refresh();
                    refreshAvatar();
                }));
        // Plugin buttons can follow Discord's settings (GameActivityToggle)
        instanceConnections.append(connect(instance->discord(), &Discord::Client::settingsChanged, this,
                                           &UserPanel::rebuildPluginButtons));
#ifndef ACHERON_NO_VOICE
        // Joining already sends mute/deafen; re-sending here would answer every
        // voice state Discord echoes with another one
        instanceConnections.append(connect(instance, &Core::ClientInstance::voiceStateChanged, this,
                                           [this](Core::Snowflake, Core::Snowflake) { applyLocalVoiceFlags(); }));
#endif
    }
    refresh();
    refreshAvatar();
    rebuildPluginButtons();
}

void UserPanel::rebuildPluginButtons()
{
    for (QToolButton *b : std::as_const(pluginButtons))
        b->deleteLater();
    pluginButtons.clear();
    auto *host = Qord::Plugins::PluginHost::instance();
    if (!instance || !host || !host->hasHook(Qord::Plugins::Hook::UserPanelButtons))
        return;
    const Qord::Plugins::UserPanelContext panel{ quint64(instance->accountId()) };
    int at = panelLayout->indexOf(muteButton);
    for (const auto &entry : host->dispatchUserPanelButtons(panel)) {
        QToolButton *b = panelButton(this, entry.button.tooltip);
        // "currentColor" in the theme's icon color, as the built-in buttons
        QByteArray svg = entry.button.svg;
        svg.replace("currentColor", Core::Theme::Manager::instance().color(Token::MutedText).name().toLatin1());
        QSvgRenderer renderer(svg);
        const qreal dpr = devicePixelRatioF();
        QPixmap pixmap(QSize(20, 20) * dpr);
        pixmap.fill(Qt::transparent);
        {
            QPainter p(&pixmap);
            p.setRenderHint(QPainter::Antialiasing);
            renderer.render(&p, QRectF(0, 0, pixmap.width(), pixmap.height()));
        }
        pixmap.setDevicePixelRatio(dpr);
        b->setIcon(QIcon(pixmap));
        // Discord's red glow on a switch that is off
        if (entry.button.danger)
            b->setStyleSheet(QStringLiteral("QToolButton { background: rgba(242, 63, 67, 0.15); border-radius: 4px; }"));
        const QString pluginId = entry.pluginId;
        const QString id = entry.button.id;
        connect(b, &QToolButton::clicked, this, [pluginId, id, panel]() {
            if (auto *h = Qord::Plugins::PluginHost::instance())
                h->dispatchUserPanelButtonClicked(pluginId, id, panel);
        });
        b->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(b, &QWidget::customContextMenuRequested, this, [this, b, pluginId, id, panel](const QPoint &pos) {
            auto *h = Qord::Plugins::PluginHost::instance();
            const auto items = h ? h->dispatchUserPanelButtonMenu(pluginId, id, panel) : QList<Qord::Plugins::MenuItem>();
            if (items.isEmpty())
                return;
            QMenu menu(this);
            for (const auto &item : items) {
                if (item.id.isEmpty() && item.label.isEmpty()) {
                    menu.addSeparator();
                    continue;
                }
                QAction *action = menu.addAction(item.label);
                if (item.checked) {
                    action->setCheckable(true);
                    action->setChecked(*item.checked);
                }
                connect(action, &QAction::triggered, this, [pluginId, id, itemId = item.id, panel]() {
                    if (auto *host = Qord::Plugins::PluginHost::instance())
                        host->dispatchUserPanelButtonMenuAction(pluginId, id, itemId, panel);
                });
            }
            menu.exec(b->mapToGlobal(pos));
        });
        panelLayout->insertWidget(at++, b);
        pluginButtons.append(b);
    }
}

void UserPanel::toggleMute()
{
    muteButton->click();
}

void UserPanel::toggleDeafen()
{
    deafenButton->click();
}

void UserPanel::refresh()
{
    if (!instance) {
        nameLabel->setText(tr("Not connected"));
        subtitleLabel->setText(QString());
        statusDot->setBadge({});
        return;
    }
    const Core::AccountInfo &info = instance->accountInfo();
    nameLabel->setText(info.displayName.isEmpty() ? info.username : info.displayName);
    statusDot->setBadge(instance->presences()->badge(instance->accountId()));
    // Streamer mode shows that it is on instead of your custom status
    subtitleLabel->setText(Qord::Preferences::streamerHides(Qord::Pref::StreamerHidePersonal) ? tr("Streamer mode")
                                                                                              : currentStatusText());
}

QString UserPanel::currentStatusText() const
{
    if (!instance)
        return {};
    for (const auto &activity : instance->presences()->activities(instance->accountId())) {
        if (activity.isCustom() && activity.state.hasValue() && !activity.state->isEmpty())
            return activity.state.get();
    }
    if (!chosenStatus.isEmpty())
        return statusLabel(chosenStatus);
    switch (instance->presences()->badge(instance->accountId()).status) {
    case Discord::StatusType::IDLE:
        return statusLabel(QStringLiteral("idle"));
    case Discord::StatusType::DND:
        return statusLabel(QStringLiteral("dnd"));
    case Discord::StatusType::INVISIBLE:
    case Discord::StatusType::OFFLINE:
        return statusLabel(QStringLiteral("invisible"));
    default:
        return statusLabel(QStringLiteral("online"));
    }
}

void UserPanel::refreshAvatar()
{
    if (!instance)
        return;
    const Core::AccountInfo &info = instance->accountInfo();
    avatarUrl = Discord::Cdn::userAvatar(info.id, info.avatar, 64);
    const QPixmap pm = images->get(avatarUrl, QSize(AvatarSize, AvatarSize), info.id);
    if (!pm.isNull())
        avatarLabel->setPixmap(circular(pm, AvatarSize, devicePixelRatioF()));
}

void UserPanel::refreshButtons()
{
    const bool mutedNow = muted || deafened;
    muteButton->setIcon(Icons::icon(mutedNow ? Icons::Name::MicOff : Icons::Name::Mic,
                                    mutedNow ? Token::StatusDnd : Token::MutedText));
    muteButton->setToolTip(mutedNow ? tr("Unmute") : tr("Mute"));
    deafenButton->setIcon(Icons::icon(deafened ? Icons::Name::HeadphoneOff : Icons::Name::Headphones,
                                      deafened ? Token::StatusDnd : Token::MutedText));
    deafenButton->setToolTip(deafened ? tr("Undeafen") : tr("Deafen"));
}

void UserPanel::applyVoiceFlags()
{
#ifndef ACHERON_NO_VOICE
    if (!instance || !instance->isInVoice())
        return;
    instance->discord()->sendVoiceStateUpdate(instance->voiceGuildId(), instance->voiceChannelId(), selfMute(),
                                              selfDeaf());
#endif
    applyLocalVoiceFlags();
}

void UserPanel::applyLocalVoiceFlags()
{
#ifndef ACHERON_NO_VOICE
    if (!instance || !instance->isInVoice())
        return;
    instance->voice()->setInputGain(selfMute() ? 0.0f : 1.0f);
    instance->voice()->setOutputVolume(selfDeaf() ? 0.0f : 1.0f);
#endif
}

void UserPanel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && identity->geometry().contains(event->pos())) {
        showStatusMenu();
        return;
    }
    QWidget::mousePressEvent(event);
}

void UserPanel::showStatusMenu()
{
    QMenu menu(this);
    if (instance) {
        const auto addStatus = [&](const QString &status, Token color) {
            QAction *a = menu.addAction(statusLabel(status));
            QPixmap dot(12, 12);
            dot.fill(Qt::transparent);
            QPainter p(&dot);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(Qt::NoPen);
            p.setBrush(Core::Theme::Manager::instance().color(color));
            p.drawEllipse(1, 1, 10, 10);
            p.end();
            a->setIcon(QIcon(dot));
            connect(a, &QAction::triggered, this, [this, status]() { setStatus(status); });
        };
        addStatus(QStringLiteral("online"), Token::StatusOnline);
        addStatus(QStringLiteral("idle"), Token::StatusIdle);
        addStatus(QStringLiteral("dnd"), Token::StatusDnd);
        addStatus(QStringLiteral("invisible"), Token::StatusOffline);
        menu.addSeparator();
        menu.addAction(customText.isEmpty() ? tr("Set Custom Status…") : tr("Edit Custom Status…"), this,
                       &UserPanel::editCustomStatus);
        if (!customText.isEmpty() || !customEmoji.isEmpty()) {
            menu.addAction(tr("Clear Custom Status"), this, [this]() {
                customText.clear();
                customEmoji.clear();
                setStatus(chosenStatus.isEmpty() ? QStringLiteral("online") : chosenStatus);
            });
        }
        if (Qord::Preferences::developerMode())
            menu.addAction(tr("Copy User ID"), this, [this]() {
                if (instance)
                    QGuiApplication::clipboard()->setText(QString::number(quint64(instance->accountId())));
            });
        menu.addSeparator();
    }
    menu.addAction(tr("Switch Accounts…"), this, &UserPanel::accountsRequested);
    menu.addAction(tr("Show/Hide Menu Bar"), this, &UserPanel::menuBarToggleRequested);
    menu.exec(mapToGlobal(identity->geometry().topLeft()) - QPoint(0, menu.sizeHint().height()));
}

void UserPanel::setStatus(const QString &status)
{
    if (!instance)
        return;
    chosenStatus = status;
    instance->discord()->setStatus(status, customText, customEmoji);
    refresh();
}

void UserPanel::editCustomStatus()
{
    bool ok = false;
    const QString text = QInputDialog::getText(this, tr("Custom Status"), tr("What's cookin'?"),
                                               QLineEdit::Normal, customText, &ok);
    if (!ok)
        return;
    customText = text.trimmed().left(128);
    setStatus(chosenStatus.isEmpty() ? QStringLiteral("online") : chosenStatus);
}

} // namespace UI
} // namespace Acheron
