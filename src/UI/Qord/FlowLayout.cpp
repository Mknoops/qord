#include "FlowLayout.hpp"

#include <QWidget>

namespace Acheron {
namespace UI {

FlowLayout::FlowLayout(QWidget *parent, int gap) : QLayout(parent), gap(gap)
{
    setContentsMargins(0, 0, 0, 0);
}

FlowLayout::~FlowLayout()
{
    while (QLayoutItem *item = takeAt(0))
        delete item;
}

void FlowLayout::addItem(QLayoutItem *item)
{
    items.append(item);
}

QLayoutItem *FlowLayout::takeAt(int index)
{
    if (index < 0 || index >= items.size())
        return nullptr;
    return items.takeAt(index);
}

void FlowLayout::insertWidget(int index, QWidget *widget)
{
    addChildWidget(widget);
    items.insert(qBound(0, index, int(items.size())), new QWidgetItem(widget));
    invalidate();
}

int FlowLayout::indexOfWidget(const QWidget *widget) const
{
    for (int i = 0; i < items.size(); ++i)
        if (items[i]->widget() == widget)
            return i;
    return -1;
}

QSize FlowLayout::minimumSize() const
{
    QSize size;
    for (const QLayoutItem *item : items)
        size = size.expandedTo(item->minimumSize());
    const QMargins m = contentsMargins();
    return size + QSize(m.left() + m.right(), m.top() + m.bottom());
}

void FlowLayout::setGeometry(const QRect &rect)
{
    QLayout::setGeometry(rect);
    arrange(rect, false);
}

int FlowLayout::arrange(const QRect &rect, bool measureOnly) const
{
    const QMargins m = contentsMargins();
    const QRect area = rect.marginsRemoved(m);
    int x = area.x();
    int y = area.y();
    int lineHeight = 0;
    for (QLayoutItem *item : items) {
        if (item->isEmpty()) // a hidden widget
            continue;
        const QSize hint = item->sizeHint();
        if (x > area.x() && x + hint.width() > area.right() + 1) {
            x = area.x();
            y += lineHeight + gap;
            lineHeight = 0;
        }
        if (!measureOnly)
            item->setGeometry(QRect(QPoint(x, y), hint));
        x += hint.width() + gap;
        lineHeight = qMax(lineHeight, hint.height());
    }
    return y + lineHeight - rect.y() + m.bottom();
}

} // namespace UI
} // namespace Acheron
