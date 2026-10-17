#include "CordOverlay.hpp"

#include "Core/Theme/Manager.hpp"
#include "UI/ChannelList/ServerRailModel.hpp"

#include <QAbstractItemView>
#include <QEvent>
#include <QItemSelectionModel>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QTimer>

namespace Acheron {
namespace UI {

namespace {
constexpr int CornerRadius = 8;
constexpr qreal LineWidth = 2.0;
constexpr int RailIconSize = 48; // ServerRailDelegate's icon, centred in its row
} // namespace

CordOverlay::CordOverlay(QAbstractItemView *rail, QAbstractItemView *channels, QWidget *parent)
    : QWidget(parent), rail(rail), channels(channels), refresh(new QTimer(this))
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::NoFocus);
    refresh->setSingleShot(true);
    refresh->setInterval(0);
    connect(refresh, &QTimer::timeout, this, qOverload<>(&QWidget::update));
    connect(&Core::Theme::Manager::instance(), &Core::Theme::Manager::themeChanged, this, &CordOverlay::schedule);
    parent->installEventFilter(this);
    setGeometry(parent->rect());
    watch(rail);
    watch(channels);
    raise();
}

void CordOverlay::watch(QAbstractItemView *view)
{
    if (!view)
        return;
    view->viewport()->installEventFilter(this);
    view->installEventFilter(this);
    connect(view->verticalScrollBar(), &QScrollBar::valueChanged, this, &CordOverlay::schedule);
    if (QAbstractItemModel *model = view->model()) {
        connect(model, &QAbstractItemModel::dataChanged, this, &CordOverlay::schedule);
        connect(model, &QAbstractItemModel::layoutChanged, this, &CordOverlay::schedule);
        connect(model, &QAbstractItemModel::modelReset, this, &CordOverlay::schedule);
        connect(model, &QAbstractItemModel::rowsInserted, this, &CordOverlay::schedule);
        connect(model, &QAbstractItemModel::rowsRemoved, this, &CordOverlay::schedule);
    }
    if (QItemSelectionModel *selection = view->selectionModel()) {
        connect(selection, &QItemSelectionModel::selectionChanged, this, &CordOverlay::schedule);
        connect(selection, &QItemSelectionModel::currentChanged, this, &CordOverlay::schedule);
    }
}

void CordOverlay::schedule()
{
    if (!refresh->isActive())
        refresh->start();
}

bool CordOverlay::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::Resize:
    case QEvent::Move:
    case QEvent::Show:
    case QEvent::Hide:
    case QEvent::LayoutRequest:
        if (watched == parentWidget()) {
            setGeometry(parentWidget()->rect());
            raise();
        }
        schedule();
        break;
    default:
        break;
    }
    return QWidget::eventFilter(watched, event);
}

void CordOverlay::paintEvent(QPaintEvent *)
{
    if (!rail || !channels || !rail->isVisibleTo(parentWidget()) || !channels->isVisibleTo(parentWidget())
        || !rail->model() || !channels->selectionModel())
        return;

    // Where it starts: the right side of the selected server's icon
    QModelIndex server;
    for (int row = 0; row < rail->model()->rowCount() && !server.isValid(); ++row) {
        const QModelIndex idx = rail->model()->index(row, 0);
        if (idx.data(ServerRailModel::IsSelectedRole).toBool())
            server = idx;
    }
    const QRect serverRect = server.isValid() ? rail->visualRect(server) : QRect();
    if (!serverRect.isValid() || !rail->viewport()->rect().intersects(serverRect))
        return;

    // Where it ends: the open channel's row, or the list's edge toward it
    const QModelIndexList selected = channels->selectionModel()->selectedIndexes();
    const QModelIndex channel = selected.isEmpty() ? channels->currentIndex() : selected.first();
    const QRect channelRect = channel.isValid() ? channels->visualRect(channel) : QRect();
    if (!channelRect.isValid())
        return;
    const QRect listRect = channels->viewport()->rect();
    const int channelY = channelRect.center().y();
    const int clampedY = qBound(listRect.top() + 1, channelY, listRect.bottom() - 1);
    const bool inView = clampedY == channelY;

    QWidget *host = parentWidget();
    const QPoint start = rail->viewport()->mapTo(
            host, QPoint(serverRect.center().x() + RailIconSize / 2 + 3, serverRect.center().y()));
    const QPoint listLeft = channels->viewport()->mapTo(host, QPoint(listRect.left(), clampedY));
    const qreal x = listLeft.x() + 2.5; // just inside the channel list's edge
    const qreal y0 = start.y();
    const qreal y1 = listLeft.y();
    const qreal end = listLeft.x() + 10;
    if (start.x() >= x)
        return;

    const qreal dir = y1 >= y0 ? 1.0 : -1.0;
    const qreal r = qMin<qreal>(CornerRadius, qAbs(y1 - y0) / 2.0);
    QPainterPath path(QPointF(start.x(), y0));
    path.lineTo(x - r, y0);
    path.quadTo(x, y0, x, y0 + dir * r);
    path.lineTo(x, y1 - dir * r);
    if (inView) {
        path.quadTo(x, y1, x + r, y1);
        path.lineTo(end, y1);
    }

    const QColor accent = Core::Theme::Manager::instance().color(Core::Theme::Token::Highlight).lighter(140);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(accent, LineWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);
    if (inView) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(accent);
        painter.drawEllipse(QPointF(end, y1), 3.0, 3.0);
    }
}

} // namespace UI
} // namespace Acheron
