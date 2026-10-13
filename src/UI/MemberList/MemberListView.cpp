#include "MemberListView.hpp"

#include "MemberListDelegate.hpp"
#include "MemberListModel.hpp"

#include "UI/Qord/Toast.hpp"

#include <QClipboard>
#include <QGuiApplication>
#include <QMouseEvent>

namespace Acheron {
namespace UI {

MemberListView::MemberListView(QWidget *parent)
    : QListView(parent)
{
    setObjectName("MemberList");

    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setSelectionMode(QAbstractItemView::NoSelection);
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(true);
    setUniformItemSizes(false);
    setContextMenuPolicy(Qt::CustomContextMenu);

    setFrameShape(QFrame::NoFrame);
    viewport()->setBackgroundRole(QPalette::Window);
    viewport()->setAutoFillBackground(true);
}

void MemberListView::scrollContentsBy(int dx, int dy)
{
    QListView::scrollContentsBy(dx, dy);
    emitVisibleRange();
}

void MemberListView::setHeaderWidget(QWidget *widget)
{
    header = widget;
    header->setParent(this);
    updateHeader();
}

void MemberListView::updateHeader()
{
    if (!header)
        return;
    const int height = header->isVisibleTo(this) ? header->sizeHint().height() : 0;
    setViewportMargins(0, height, 0, 0);
    header->setGeometry(frameWidth(), frameWidth(), width() - 2 * frameWidth(), height);
}

void MemberListView::resizeEvent(QResizeEvent *event)
{
    QListView::resizeEvent(event);
    updateHeader();
    emitVisibleRange();
}

bool MemberListView::clickDecoration(QMouseEvent *event, bool doubleClick)
{
    auto *delegate = qobject_cast<MemberListDelegate *>(itemDelegate());
    auto *provider = NameDecorations::instance();
    const QModelIndex index = indexAt(event->pos());
    if (event->button() != Qt::LeftButton || !delegate || !index.isValid())
        return false;
    const auto decoration = delegate->clickableDecorationAt(index.row(), event->pos());
    if (!decoration)
        return false;
    if (!decoration->copyText.isEmpty()) {
        // BetterRoleDot: copyWithToast(color)
        if (!doubleClick) {
            QGuiApplication::clipboard()->setText(decoration->copyText);
            Toast::show(window(), tr("Copied to clipboard!"), Toast::Type::Success);
        }
        return true;
    }
    if (!provider)
        return true;
    provider->click(Core::Snowflake(index.data(MemberListModel::AccountIdRole).toULongLong()),
                    Core::Snowflake(index.data(MemberListModel::GuildIdRole).toULongLong()),
                    Core::Snowflake(index.data(MemberListModel::UserIdRole).toULongLong()),
                    NameDecorations::Place::MemberList, *decoration, doubleClick);
    return true;
}

void MemberListView::mousePressEvent(QMouseEvent *event)
{
    if (clickDecoration(event, false))
        return;
    QListView::mousePressEvent(event);
}

void MemberListView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (clickDecoration(event, true))
        return;
    QListView::mouseDoubleClickEvent(event);
}

void MemberListView::mouseMoveEvent(QMouseEvent *event)
{
    QListView::mouseMoveEvent(event);
    auto *delegate = qobject_cast<MemberListDelegate *>(itemDelegate());
    const QModelIndex index = indexAt(event->pos());
    const bool clickable = delegate && index.isValid() && delegate->clickableDecorationAt(index.row(), event->pos());
    if (clickable)
        viewport()->setCursor(Qt::PointingHandCursor);
    else
        viewport()->unsetCursor();
}

void MemberListView::emitVisibleRange()
{
    if (!model() || model()->rowCount() == 0)
        return;

    QModelIndex topIndex = indexAt(viewport()->rect().topLeft());
    QModelIndex bottomIndex = indexAt(viewport()->rect().bottomLeft());

    int first = topIndex.isValid() ? topIndex.row() : 0;
    int last = bottomIndex.isValid() ? bottomIndex.row() : model()->rowCount() - 1;

    if (first != lastFirstVisible || last != lastLastVisible) {
        lastFirstVisible = first;
        lastLastVisible = last;
        emit visibleRangeChanged(first, last);
    }
}

} // namespace UI
} // namespace Acheron
