#pragma once

#include <QHash>
#include <QStyledItemDelegate>

#include "MemberListModel.hpp"

namespace Acheron {
namespace UI {

class MemberListDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit MemberListDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override;
    // Qord: tooltips for name decorations
    bool helpEvent(QHelpEvent *event, QAbstractItemView *view, const QStyleOptionViewItem &option,
                   const QModelIndex &index) override;

private:
    void paintGroup(QPainter *painter, const QStyleOptionViewItem &option,
                    const QModelIndex &index) const;
    void paintMember(QPainter *painter, const QStyleOptionViewItem &option,
                     const QModelIndex &index) const;
    void paintActivityLine(QPainter *painter, const QStyleOptionViewItem &option,
                           const MemberActivity &activity, const QRect &lineRect) const;
    void paintPlaceholder(QPainter *painter, const QStyleOptionViewItem &option) const;

    static QString activityIconName(Discord::ActivityType kind);

    // Qord: where each row's name decorations were last painted
    mutable QHash<int, QList<QPair<QRect, QString>>> decorationRects;
};

} // namespace UI
} // namespace Acheron
