#pragma once

// Discord's toasts: a short message near the bottom of the window (a green
// check for success, a red cross for failure) that fades out by itself.
// One at a time: a new toast replaces the one showing.

#include <QWidget>

class QGraphicsOpacityEffect;
class QPropertyAnimation;
class QTimer;

namespace Acheron {
namespace UI {

class Toast : public QWidget
{
    Q_OBJECT
public:
    enum class Type { Message, Success, Failure };

    // Shows `text` over `window` for a few seconds
    static Toast *show(QWidget *window, const QString &text, Type type = Type::Message);

    [[nodiscard]] QString text() const { return message; }
    [[nodiscard]] Type type() const { return kind; }

    static constexpr int VisibleMs = 2800;
    static constexpr int FadeMs = 250;

protected:
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    Toast(QWidget *window, const QString &text, Type type);
    void place();
    [[nodiscard]] QSize contentSize() const;

    QString message;
    Type kind;
    QGraphicsOpacityEffect *opacity;
    QPropertyAnimation *fade;
    QTimer *lifetime;
};

} // namespace UI
} // namespace Acheron
