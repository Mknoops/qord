#include "PluginSurfaces.hpp"

#include "Core/Theme/Icons.hpp"
#include "Core/Theme/Manager.hpp"
#include "Plugins/PluginHost.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

namespace Acheron {
namespace UI {

using Core::Theme::Token;

namespace {
void clearLayout(QLayout *layout)
{
    while (QLayoutItem *item = layout->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete item;
    }
}

// The icons MemberCount draws: a filled online dot, an offline ring, a speaker
QPixmap badgeIcon(Qord::Plugins::CountBadge::Icon icon, qreal dpr)
{
    const auto &theme = Core::Theme::Manager::instance();
    if (icon == Qord::Plugins::CountBadge::Icon::Voice)
        return Core::Theme::Icons::pixmap(Core::Theme::Icons::Name::VolumeOn, 14, Token::MutedText, dpr);
    QPixmap pixmap(QSize(12, 12) * dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    if (icon == Qord::Plugins::CountBadge::Icon::Online) {
        p.setPen(Qt::NoPen);
        p.setBrush(theme.color(Token::StatusOnline));
        p.drawEllipse(QRectF(1, 1, 10, 10));
    } else {
        p.setPen(QPen(theme.color(Token::StatusOffline), 2));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QRectF(2, 2, 8, 8));
    }
    return pixmap;
}

QColor badgeColor(Qord::Plugins::CountBadge::Icon icon)
{
    const auto &theme = Core::Theme::Manager::instance();
    return icon == Qord::Plugins::CountBadge::Icon::Online ? theme.color(Token::StatusOnline)
                                                           : theme.color(Token::MutedText);
}
} // namespace

ServerListButtonBar::ServerListButtonBar(QWidget *parent)
    : QWidget(parent), layout(new QVBoxLayout(this))
{
    setObjectName(QStringLiteral("ServerListButtons"));
    layout->setContentsMargins(4, 6, 4, 2);
    layout->setSpacing(4);
    hide();
}

void ServerListButtonBar::refresh(Core::Snowflake account)
{
    accountId = account;
    clearLayout(layout);
    auto *host = Qord::Plugins::PluginHost::instance();
    if (!host || !host->hasHook(Qord::Plugins::Hook::ServerListButtons) || !accountId.isValid()) {
        hide();
        return;
    }
    Qord::Plugins::ServerListContext ctx;
    ctx.accountId = quint64(accountId);
    const auto buttons = host->dispatchServerListButtons(ctx);
    for (const auto &entry : buttons) {
        auto *button = new QPushButton(entry.button.label, this);
        button->setObjectName(QStringLiteral("ServerListTextButton"));
        button->setCursor(Qt::PointingHandCursor);
        button->setFlat(true);
        if (!entry.button.tooltip.isEmpty())
            button->setToolTip(entry.button.tooltip);
        connect(button, &QPushButton::clicked, this, [pluginId = entry.pluginId, id = entry.button.id, ctx]() {
            if (auto *h = Qord::Plugins::PluginHost::instance())
                h->dispatchServerListButtonClicked(pluginId, id, ctx);
        });
        layout->addWidget(button);
        button->show();
    }
    setVisible(!buttons.isEmpty());
}

MemberCountBar::MemberCountBar(QWidget *parent)
    : QWidget(parent), layout(new QHBoxLayout(this))
{
    setObjectName(QStringLiteral("MemberCountBar"));
    layout->setContentsMargins(12, 12, 12, 4);
    layout->setSpacing(12);
    hide();
}

void MemberCountBar::setBadges(const QList<Qord::Plugins::CountBadge> &badges)
{
    QStringList signature;
    for (const auto &badge : badges)
        signature << QString::number(int(badge.icon)) + badge.text + badge.tooltip;
    if (signature == shown && isHidden() == badges.isEmpty())
        return;
    shown = signature;
    clearLayout(layout);
    layout->addStretch(1);
    const qreal dpr = devicePixelRatioF();
    for (const auto &badge : badges) {
        auto *item = new QWidget(this);
        auto *h = new QHBoxLayout(item);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(6);
        auto *icon = new QLabel(item);
        icon->setPixmap(badgeIcon(badge.icon, dpr));
        auto *text = new QLabel(badge.text, item);
        QPalette pal = text->palette();
        pal.setColor(QPalette::WindowText, badgeColor(badge.icon));
        text->setPalette(pal);
        QFont font = text->font();
        font.setPixelSize(13);
        font.setWeight(QFont::DemiBold);
        text->setFont(font);
        h->addWidget(icon);
        h->addWidget(text);
        item->setToolTip(badge.tooltip);
        layout->addWidget(item);
        item->show(); // a layout only shows new children later, after the size is measured
    }
    layout->addStretch(1);
    setVisible(!badges.isEmpty());
}

QString MemberCountBar::badgesHtml(const QList<Qord::Plugins::CountBadge> &badges)
{
    QStringList parts;
    for (const auto &badge : badges) {
        QString symbol;
        switch (badge.icon) {
        case Qord::Plugins::CountBadge::Icon::Online:
            symbol = QStringLiteral("<span style=\"color:%1\">&#9679;</span>").arg(badgeColor(badge.icon).name());
            break;
        case Qord::Plugins::CountBadge::Icon::Total:
            symbol = QStringLiteral("<span style=\"color:%1\">&#9675;</span>").arg(badgeColor(badge.icon).name());
            break;
        case Qord::Plugins::CountBadge::Icon::Voice:
            symbol = QStringLiteral("&#128266;");
            break;
        }
        parts.append(symbol + QLatin1Char(' ') + badge.text.toHtmlEscaped());
    }
    return parts.join(QStringLiteral("&nbsp;&nbsp;&nbsp;"));
}

} // namespace UI
} // namespace Acheron
