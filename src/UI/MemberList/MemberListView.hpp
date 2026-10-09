#pragma once

#include <QListView>

namespace Acheron {
namespace UI {

class MemberListView : public QListView
{
    Q_OBJECT
public:
    explicit MemberListView(QWidget *parent = nullptr);

    // Qord: a widget pinned above the members (MemberCount); the list
    // scrolls below it
    void setHeaderWidget(QWidget *header);
    void updateHeader();

signals:
    void visibleRangeChanged(int firstVisible, int lastVisible);

protected:
    void scrollContentsBy(int dx, int dy) override;
    void resizeEvent(QResizeEvent *event) override;
    QSize sizeHint() const override { return { 240, 0 }; }

private:
    void emitVisibleRange();

    QWidget *header = nullptr;
    int lastFirstVisible = -1;
    int lastLastVisible = -1;
};

} // namespace UI
} // namespace Acheron
