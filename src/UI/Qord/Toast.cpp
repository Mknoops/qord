#include "Toast.hpp"

#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QTimer>

namespace Acheron {
namespace UI {

namespace {
constexpr int Padding = 12;
constexpr int IconSize = 16;
constexpr int BottomMargin = 96; // above the message box, as in Discord
constexpr int MaxTextWidth = 420;

QFont toastFont(const QWidget *widget)
{
    QFont font = widget->font();
    font.setPixelSize(14);
    font.setWeight(QFont::Medium);
    return font;
}
} // namespace

Toast *Toast::show(QWidget *window, const QString &text, Type type)
{
    if (!window || text.isEmpty())
        return nullptr;
    // One at a time: the newest replaces the one showing
    for (Toast *old : window->findChildren<Toast *>(QString(), Qt::FindDirectChildrenOnly))
        old->deleteLater();
    auto *toast = new Toast(window, text, type);
    toast->place();
    toast->QWidget::show();
    toast->raise();
    return toast;
}

Toast::Toast(QWidget *window, const QString &text, Type type) : QWidget(window), message(text), kind(type)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAccessibleName(text);
    opacity = new QGraphicsOpacityEffect(this);
    opacity->setOpacity(1.0);
    setGraphicsEffect(opacity);
    fade = new QPropertyAnimation(opacity, "opacity", this);
    fade->setDuration(FadeMs);
    fade->setStartValue(1.0);
    fade->setEndValue(0.0);
    connect(fade, &QPropertyAnimation::finished, this, &QObject::deleteLater);
    lifetime = new QTimer(this);
    lifetime->setSingleShot(true);
    connect(lifetime, &QTimer::timeout, fade, [this]() { fade->start(); });
    lifetime->start(VisibleMs);
    window->installEventFilter(this); // follow the window when it resizes
}

QSize Toast::contentSize() const
{
    const QFontMetrics fm(toastFont(this));
    const QRect text = fm.boundingRect(QRect(0, 0, MaxTextWidth, 1000), Qt::TextWordWrap, message);
    const int icon = kind == Type::Message ? 0 : IconSize + 8;
    return { Padding * 2 + icon + text.width(), Padding * 2 + qMax(text.height(), IconSize) };
}

void Toast::place()
{
    QWidget *window = parentWidget();
    const QSize size = contentSize();
    resize(size);
    move((window->width() - size.width()) / 2, window->height() - BottomMargin - size.height());
}

bool Toast::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize)
        place();
    return QWidget::eventFilter(watched, event);
}

void Toast::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath shape;
    shape.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
    p.fillPath(shape, QColor(0x11, 0x12, 0x14, 240));
    p.setPen(QColor(255, 255, 255, 20));
    p.drawPath(shape);

    int x = Padding;
    const int centerY = height() / 2;
    if (kind != Type::Message) {
        const QRectF circle(x, centerY - IconSize / 2.0, IconSize, IconSize);
        p.setPen(Qt::NoPen);
        p.setBrush(kind == Type::Success ? QColor(0x23, 0xa5, 0x5a) : QColor(0xf2, 0x3f, 0x43));
        p.drawEllipse(circle);
        QPen mark(Qt::white, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(mark);
        const QPointF c = circle.center();
        if (kind == Type::Success) {
            p.drawLine(QPointF(c.x() - 3.5, c.y()), QPointF(c.x() - 1, c.y() + 2.5));
            p.drawLine(QPointF(c.x() - 1, c.y() + 2.5), QPointF(c.x() + 3.5, c.y() - 2.5));
        } else {
            p.drawLine(QPointF(c.x() - 3, c.y() - 3), QPointF(c.x() + 3, c.y() + 3));
            p.drawLine(QPointF(c.x() + 3, c.y() - 3), QPointF(c.x() - 3, c.y() + 3));
        }
        x += IconSize + 8;
    }
    p.setFont(toastFont(this));
    p.setPen(QColor(0xdb, 0xde, 0xe1));
    p.drawText(QRect(x, Padding, width() - x - Padding, height() - Padding * 2), Qt::AlignVCenter | Qt::TextWordWrap,
               message);
}

} // namespace UI
} // namespace Acheron
