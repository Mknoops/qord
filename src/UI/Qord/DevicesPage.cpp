#include "DevicesPage.hpp"
#include "Qord/Preferences.hpp"

#include "Core/ClientInstance.hpp"
#include "Core/Markdown/Parser.hpp"
#include "Core/Theme/Icons.hpp"
#include "Core/Theme/Manager.hpp"
#include "Plugins/PluginHost.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSvgRenderer>
#include <QToolButton>
#include <QVBoxLayout>

namespace Acheron {
namespace UI {

namespace {
using Core::Theme::Token;
namespace Icons = Core::Theme::Icons;

QPointer<DevicesPage> shown;
constexpr int IconSize = 48;

bool isMobile(const Qord::Plugins::DeviceSession &s)
{
    return s.os == QLatin1String("Android") || s.os == QLatin1String("iOS") ||
           s.platform.contains(QLatin1String("Android")) || s.platform.contains(QLatin1String("iOS"));
}

// The device icon on a rounded square, with a plugin's round badge in the
// bottom right corner (BetterSessions' platform badge)
QPixmap deviceIcon(const Qord::Plugins::DeviceSession &session, const QString &color, const QByteArray &badgeSvg,
                   qreal dpr)
{
    const auto &theme = Core::Theme::Manager::instance();
    QPixmap out(QSize(IconSize + 4, IconSize + 4) * dpr);
    out.fill(Qt::transparent);
    out.setDevicePixelRatio(dpr);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor background = color.isEmpty() ? theme.color(Token::HoverBg) : QColor(color);
    p.setPen(Qt::NoPen);
    p.setBrush(background);
    p.drawRoundedRect(QRectF(0, 0, IconSize, IconSize), 8, 8);
    // Dark glyph on a light background, light on a dark one
    const QColor glyph = background.lightnessF() > 0.6 ? QColor(0x1e, 0x1f, 0x22) : theme.color(Token::PrimaryText);
    const QPixmap icon = Icons::pixmap(isMobile(session) ? Icons::Name::Smartphone : Icons::Name::Monitor, 24, glyph, dpr);
    p.drawPixmap(QRectF(12, 12, 24, 24), icon, QRectF(QPointF(0, 0), QSizeF(icon.size())));
    if (!badgeSvg.isEmpty()) {
        const QRectF badge(IconSize - 16, IconSize - 16, 20, 20);
        p.setBrush(theme.color(Token::MutedText));
        p.drawEllipse(badge);
        QByteArray svg = badgeSvg;
        svg.replace("currentColor", theme.color(Token::BaseBg).name().toLatin1());
        QSvgRenderer renderer(svg);
        renderer.render(&p, badge.adjusted(3, 3, -3, -3));
    }
    return out;
}
} // namespace

DevicesPage::DevicesPage(QWidget *parent) : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll);
    auto *content = new QWidget(scroll);
    scroll->setWidget(content);
    auto *v = new QVBoxLayout(content);

    auto *title = new QLabel(tr("Devices"), content);
    title->setObjectName(QStringLiteral("ChannelHeaderName"));
    v->addWidget(title);
    auto *intro = new QLabel(tr("Here are all the devices that are currently logged in with your Discord account. "
                                "You can log out of each one individually or all other devices."),
                             content);
    intro->setWordWrap(true);
    intro->setForegroundRole(QPalette::PlaceholderText);
    v->addWidget(intro);
    status = new QLabel(content);
    status->setWordWrap(true);
    v->addWidget(status);
    list = new QVBoxLayout;
    list->setSpacing(8);
    v->addLayout(list);

    auto *allTitle = new QLabel(tr("LOG OUT OF ALL KNOWN DEVICES"), content);
    allTitle->setObjectName(QStringLiteral("ChannelHeaderName"));
    v->addSpacing(12);
    v->addWidget(allTitle);
    auto *allText = new QLabel(tr("You'll have to log back in on all logged out devices."), content);
    allText->setForegroundRole(QPalette::PlaceholderText);
    v->addWidget(allText);
    logOutAll = new QPushButton(tr("Log Out All Known Devices"), content);
    logOutAll->setObjectName(QStringLiteral("DangerButton"));
    logOutAll->setCursor(Qt::PointingHandCursor);
    logOutAll->setEnabled(false);
    connect(logOutAll, &QPushButton::clicked, this, [this]() {
        QStringList others;
        for (const auto &s : std::as_const(sessions))
            if (!s.current)
                others.append(s.idHash);
        logOut(others);
    });
    auto *allRow = new QHBoxLayout;
    allRow->addWidget(logOutAll);
    allRow->addStretch();
    v->addLayout(allRow);
    v->addStretch();

    if (auto *host = Qord::Plugins::PluginHost::instance())
        connect(host, &Qord::Plugins::PluginHost::pluginStateChanged, this, [this]() {
            if (isVisible())
                rerender();
        });
}

DevicesPage *DevicesPage::visiblePage()
{
    return shown.data();
}

void DevicesPage::setInstance(Core::ClientInstance *newInstance)
{
    if (instance == newInstance)
        return;
    instance = newInstance;
    sessions.clear();
    loaded = false;
    if (isVisible())
        reload();
}

void DevicesPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    shown = this;
    reload();
}

void DevicesPage::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    if (shown == this)
        shown = nullptr;
    // BetterSessions: the devices have been seen
    auto *host = Qord::Plugins::PluginHost::instance();
    if (loaded && instance && host && host->hasHook(Qord::Plugins::Hook::DeviceSessions))
        host->dispatchDevicesPageClosed(quint64(instance->accountId()), sessions);
}

void DevicesPage::reload()
{
    if (!instance) {
        status->setText(tr("Log in to see your devices."));
        return;
    }
    status->setText(tr("Loading…"));
    const QPointer<DevicesPage> guard(this);
    const QString current = instance->discord()->currentAuthSessionHash();
    instance->discord()->fetchAuthSessions(
            [guard, current](bool ok, const QList<Discord::Client::AuthSession> &list) {
                if (!guard)
                    return;
                if (!ok) {
                    guard->status->setText(tr("Couldn't load your devices."));
                    return;
                }
                guard->sessions.clear();
                for (const auto &a : list) {
                    Qord::Plugins::DeviceSession s;
                    s.idHash = a.idHash;
                    s.os = a.os;
                    s.platform = a.platform;
                    s.location = a.location;
                    s.lastUsedMs = a.lastUsed.isValid() ? a.lastUsed.toMSecsSinceEpoch() : 0;
                    s.current = !current.isEmpty() && a.idHash == current;
                    guard->sessions.append(s);
                }
                // The current device first, then the most recently used
                std::stable_sort(guard->sessions.begin(), guard->sessions.end(), [](const auto &a, const auto &b) {
                    if (a.current != b.current)
                        return a.current;
                    return a.lastUsedMs > b.lastUsedMs;
                });
                guard->loaded = true;
                guard->status->clear();
                guard->rerender();
            });
}

void DevicesPage::rerender()
{
    while (QLayoutItem *item = list->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    bool others = false;
    bool headedOthers = false;
    for (int i = 0; i < sessions.size(); ++i) {
        const auto &s = sessions[i];
        if (i == 0 || (!s.current && !headedOthers)) {
            auto *heading = new QLabel(s.current ? tr("CURRENT DEVICE") : tr("OTHER DEVICES"), this);
            heading->setForegroundRole(QPalette::PlaceholderText);
            list->addWidget(heading);
            headedOthers = !s.current;
        }
        others |= !s.current;
        list->addWidget(row(s));
    }
    logOutAll->setEnabled(others);
}

QWidget *DevicesPage::row(const Qord::Plugins::DeviceSession &session)
{
    auto *host = Qord::Plugins::PluginHost::instance();
    const Qord::Plugins::Id accountId = instance ? quint64(instance->accountId()) : 0;
    std::optional<QPair<QString, Qord::Plugins::DeviceSessionView>> plugin;
    if (host && host->hasHook(Qord::Plugins::Hook::DeviceSessions))
        plugin = host->dispatchDeviceSessionView(accountId, session);
    const Qord::Plugins::DeviceSessionView view = plugin ? plugin->second : Qord::Plugins::DeviceSessionView();

    auto *w = new QWidget(this);
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(0, 4, 0, 4);
    h->setSpacing(12);
    auto *icon = new QLabel(w);
    icon->setPixmap(deviceIcon(session, view.iconColor, view.badgeSvg, devicePixelRatioF()));
    h->addWidget(icon);

    auto *text = new QVBoxLayout;
    text->setSpacing(2);
    auto *titleRow = new QHBoxLayout;
    titleRow->setSpacing(6);
    auto *title = new QLabel(view.title.isEmpty() ? session.os + QStringLiteral(" · ") + session.platform : view.title, w);
    QFont bold = title->font();
    bold.setWeight(QFont::DemiBold);
    title->setFont(bold);
    titleRow->addWidget(title);
    if (view.isNew) {
        auto *badge = new QLabel(tr("NEW"), w);
        badge->setStyleSheet(QStringLiteral("background: #da373c; color: white; border-radius: 3px; padding: 0 4px; "
                                            "font-size: 10px; font-weight: 600;"));
        titleRow->addWidget(badge);
    }
    for (const auto &button : view.buttons) {
        auto *b = new QPushButton(button.label, w);
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(QStringLiteral("QPushButton { padding: 0 8px; font-size: 11px; }"));
        const QString pluginId = plugin->first;
        const QString buttonId = button.id;
        connect(b, &QPushButton::clicked, this, [pluginId, buttonId, accountId, session]() {
            if (auto *host = Qord::Plugins::PluginHost::instance())
                host->dispatchDeviceSessionButton(pluginId, accountId, buttonId, session);
        });
        titleRow->addWidget(b);
    }
    titleRow->addStretch();
    text->addLayout(titleRow);

    // "Location · 3 hours ago"; the current device is in use, so no time
    auto *subRow = new QHBoxLayout;
    subRow->setSpacing(0);
    // Streamer mode keeps where you log in from off screen
    auto *location = new QLabel(Qord::Preferences::streamerHides(Qord::Pref::StreamerHidePersonal)
                                        ? tr("Location hidden")
                                        : session.location,
                                w);
    location->setForegroundRole(QPalette::PlaceholderText);
    subRow->addWidget(location);
    if (!session.current && session.lastUsedMs > 0) {
        const QString when = Core::Markdown::formatDiscordTimestamp(session.lastUsedMs / 1000, QLatin1Char('R'),
                                                                    QDateTime::currentDateTime(), QLocale());
        auto *time = new QLabel(QStringLiteral(" · ") + when, w);
        time->setForegroundRole(QPalette::PlaceholderText);
        time->setToolTip(view.timeTooltip);
        subRow->addWidget(time);
    }
    subRow->addStretch();
    text->addLayout(subRow);
    h->addLayout(text, 1);

    if (!session.current) {
        auto *out = new QToolButton(w);
        out->setIcon(Icons::icon(Icons::Name::X, Token::MutedText));
        out->setAutoRaise(true);
        out->setToolTip(tr("Log out"));
        out->setCursor(Qt::PointingHandCursor);
        connect(out, &QToolButton::clicked, this, [this, hash = session.idHash]() { logOut({ hash }); });
        h->addWidget(out);
    }
    return w;
}

void DevicesPage::logOut(const QStringList &idHashes)
{
    if (!instance || idHashes.isEmpty())
        return;
    const QString question = idHashes.size() == 1 ? tr("Log out of this device?")
                                                  : tr("Log out of all %1 other devices?").arg(idHashes.size());
    if (QMessageBox::question(this, tr("Log Out"), question) != QMessageBox::Yes)
        return;
    const QPointer<DevicesPage> guard(this);
    instance->discord()->logoutAuthSessions(idHashes, [guard](bool ok, const QString &error) {
        if (!guard)
            return;
        if (!ok) {
            guard->status->setText(tr("Couldn't log out: %1").arg(error));
            return;
        }
        guard->reload();
    });
}

} // namespace UI
} // namespace Acheron
