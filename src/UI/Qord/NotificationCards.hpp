#pragma once

#include <QFrame>

#include <functional>

namespace Acheron {
namespace UI {

// Vencord's notifications for plugins: cards stacked in the bottom right of
// a window with a title, text and a close button. Clicking one runs its
// action and closes it; one that isn't permanent closes after a few
// seconds, with a bar counting down (paused while hovered).
class NotificationCard : public QFrame
{
    Q_OBJECT
public:
    static constexpr int TimeoutMs = 5000;
    static constexpr int Width = 360;

    static NotificationCard *show(QWidget *window, const QString &title, const QString &body, bool permanent,
                                  std::function<void()> clicked);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    bool event(QEvent *event) override; // hover pauses the countdown
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    NotificationCard(QWidget *window, const QString &title, const QString &body, bool permanent,
                     std::function<void()> clicked);
    void dismiss();
    static void layoutCards(QWidget *window);

    std::function<void()> clicked;
    bool permanent;
    int remainingMs = TimeoutMs;
    bool hovered = false;
    class QTimer *tick = nullptr;
};

} // namespace UI
} // namespace Acheron
