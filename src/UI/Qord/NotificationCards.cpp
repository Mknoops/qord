#include "NotificationCards.hpp"

#include "Core/Theme/Icons.hpp"
#include "Core/Theme/Manager.hpp"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace Acheron {
namespace UI {

namespace {
using Core::Theme::Token;
constexpr int Margin = 16;
constexpr int Gap = 8;
constexpr int TickMs = 50;
constexpr int IconSize = 40;
} // namespace

NotificationCard *NotificationCard::show(QWidget *window, const QString &title, const QString &body, bool permanent,
                                         std::function<void()> clicked)
{
    if (!window)
        return nullptr;
    auto *card = new NotificationCard(window, title, body, permanent, std::move(clicked));
    card->QWidget::show();
    card->raise();
    layoutCards(window);
    return card;
}

NotificationCard::NotificationCard(QWidget *window, const QString &title, const QString &body, bool permanent,
                                   std::function<void()> clicked)
    : QFrame(window), clicked(std::move(clicked)), permanent(permanent)
{
    setObjectName(QStringLiteral("QordNotificationCard"));
    setAttribute(Qt::WA_DeleteOnClose);
    setCursor(Qt::PointingHandCursor);
    setFixedWidth(Width);
    const auto &theme = Core::Theme::Manager::instance();
    setStyleSheet(QStringLiteral("#QordNotificationCard { background: %1; border: 1px solid %2; border-radius: 8px; }")
                          .arg(theme.color(Token::RailBg).name(), theme.color(Token::Divider).name()));

    auto *outer = new QHBoxLayout(this);
    outer->setContentsMargins(14, 10, 8, 14);
    outer->setSpacing(10);
    iconLabel = new QLabel(this);
    iconLabel->setFixedSize(IconSize, IconSize);
    iconLabel->hide();
    outer->addWidget(iconLabel, 0, Qt::AlignTop);
    auto *v = new QVBoxLayout;
    v->setContentsMargins(0, 0, 0, 0);
    outer->addLayout(v, 1);
    auto *top = new QHBoxLayout;
    auto *titleLabel = new QLabel(title, this);
    QFont bold = titleLabel->font();
    bold.setWeight(QFont::DemiBold);
    titleLabel->setFont(bold);
    titleLabel->setWordWrap(true);
    top->addWidget(titleLabel, 1);
    auto *close = new QToolButton(this);
    close->setAutoRaise(true);
    close->setIcon(Core::Theme::Icons::icon(Core::Theme::Icons::Name::X, Token::MutedText));
    close->setToolTip(tr("Close"));
    connect(close, &QToolButton::clicked, this, &NotificationCard::dismiss);
    top->addWidget(close, 0, Qt::AlignTop);
    v->addLayout(top);
    auto *bodyLabel = new QLabel(body, this);
    bodyLabel->setWordWrap(true);
    bodyLabel->setTextFormat(Qt::PlainText);
    bodyLabel->setForegroundRole(QPalette::PlaceholderText);
    v->addWidget(bodyLabel);
    adjustSize();

    window->installEventFilter(this);
    if (!permanent) {
        tick = new QTimer(this);
        tick->setInterval(TickMs);
        connect(tick, &QTimer::timeout, this, [this]() {
            if (hovered)
                return;
            remainingMs -= TickMs;
            update();
            if (remainingMs <= 0)
                dismiss();
        });
        tick->start();
    }
}

void NotificationCard::setIcon(const QPixmap &pixmap)
{
    if (pixmap.isNull())
        return;
    const qreal dpr = devicePixelRatioF();
    QPixmap round(QSize(IconSize, IconSize) * dpr);
    round.setDevicePixelRatio(dpr);
    round.fill(Qt::transparent);
    QPainter p(&round);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath clip;
    clip.addEllipse(QRectF(0, 0, IconSize, IconSize));
    p.setClipPath(clip);
    p.drawPixmap(QRect(0, 0, IconSize, IconSize),
                 pixmap.scaled(QSize(IconSize, IconSize) * dpr, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    p.end();
    iconLabel->setPixmap(round);
    iconLabel->show();
    if (QWidget *window = parentWidget())
        layoutCards(window);
}

void NotificationCard::dismiss()
{
    QWidget *window = parentWidget();
    hide();
    close();
    if (window)
        QTimer::singleShot(0, window, [window]() { layoutCards(window); });
}

void NotificationCard::layoutCards(QWidget *window)
{
    // Newest at the bottom, older ones above it
    int bottom = window->height() - Margin;
    const auto cards = window->findChildren<NotificationCard *>(QString(), Qt::FindDirectChildrenOnly);
    for (int i = int(cards.size()) - 1; i >= 0; --i) {
        NotificationCard *card = cards[i];
        if (!card->isVisible())
            continue;
        card->adjustSize();
        bottom -= card->height();
        card->move(window->width() - Margin - card->width(), bottom);
        card->raise();
        bottom -= Gap;
    }
}

bool NotificationCard::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize)
        layoutCards(parentWidget());
    return QFrame::eventFilter(watched, event);
}

void NotificationCard::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return QFrame::mousePressEvent(event);
    const auto action = clicked;
    dismiss();
    if (action)
        action();
}

bool NotificationCard::event(QEvent *event)
{
    if (event->type() == QEvent::Enter)
        hovered = true;
    else if (event->type() == QEvent::Leave)
        hovered = false;
    return QFrame::event(event);
}

void NotificationCard::paintEvent(QPaintEvent *event)
{
    QFrame::paintEvent(event);
    if (permanent)
        return;
    // The countdown along the bottom edge
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal fraction = qBound(0.0, remainingMs / qreal(TimeoutMs), 1.0);
    p.setPen(Qt::NoPen);
    p.setBrush(Core::Theme::Manager::instance().color(Token::Highlight));
    p.drawRoundedRect(QRectF(2, height() - 5, (width() - 4) * fraction, 3), 1.5, 1.5);
}

} // namespace UI
} // namespace Acheron
