#pragma once

#include <QPointer>
#include <QWidget>

class QAbstractItemView;
class QTimer;

namespace Acheron {
namespace UI {

// Qord's cord: an accent line from the selected server in the server rail,
// down the edge of the channel list, to the open channel. It shows where you
// are at a glance; when the channel is scrolled out of view the line runs to
// the list's edge, pointing at it. Drawn over both columns and transparent to
// the mouse.
class CordOverlay : public QWidget
{
    Q_OBJECT
public:
    // `rail` lists the servers (its selected row has IsSelectedRole); `channels`
    // marks the open channel with its selection. All three share `parent`.
    CordOverlay(QAbstractItemView *rail, QAbstractItemView *channels, QWidget *parent);

protected:
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void watch(QAbstractItemView *view);
    void schedule();

    QPointer<QAbstractItemView> rail;
    QPointer<QAbstractItemView> channels;
    QTimer *refresh;
};

} // namespace UI
} // namespace Acheron
