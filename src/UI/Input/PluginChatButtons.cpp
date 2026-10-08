#include "PluginChatButtons.hpp"

#include "Core/Theme/Manager.hpp"
#include "Plugins/PluginHost.hpp"

#include <QHBoxLayout>
#include <QPainter>
#include <QSvgRenderer>
#include <QMouseEvent>
#include <QToolButton>

#include <functional>

namespace {
class ClickButton : public QToolButton
{
public:
    using QToolButton::QToolButton;
    std::function<void(Qord::Plugins::ChatContext::Click)> onClick;

protected:
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        const bool inside = rect().contains(event->pos());
        if (inside && onClick) {
            if (event->button() == Qt::MiddleButton) {
                onClick(Qord::Plugins::ChatContext::Click::Middle);
                return;
            }
            if (event->button() == Qt::RightButton) {
                onClick(Qord::Plugins::ChatContext::Click::Right);
                return;
            }
        }
        const bool wasDown = isDown();
        QToolButton::mouseReleaseEvent(event);
        if (inside && wasDown && event->button() == Qt::LeftButton && onClick)
            onClick(Qord::Plugins::ChatContext::Click::Left);
    }
    void contextMenuEvent(QContextMenuEvent *event) override { event->accept(); }
};
} // namespace

namespace Acheron {
namespace UI {

namespace {
QIcon iconFromSvg(QByteArray svg, const QColor &color, qreal dpr)
{
    svg.replace("currentColor", color.name().toLatin1());
    svg.replace("currentcolor", color.name().toLatin1());
    QSvgRenderer renderer(svg);
    const QSize size(20, 20);
    QPixmap pixmap(size * dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    if (renderer.isValid()) {
        QPainter painter(&pixmap);
        renderer.render(&painter, QRectF(QPointF(0, 0), QSizeF(size)));
    }
    return QIcon(pixmap);
}
} // namespace

PluginChatButtons::PluginChatButtons(ContextProvider provider, QWidget *parent)
    : QWidget(parent), contextProvider(std::move(provider))
{
    layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 0, 4, 0);
    layout->setSpacing(2);
    setVisible(false);

    if (auto *host = Qord::Plugins::PluginHost::instance())
        connect(host, &Qord::Plugins::PluginHost::chatBarChanged, this, &PluginChatButtons::scheduleRebuild);
    connect(&Core::Theme::Manager::instance(), &Core::Theme::Manager::themeChanged, this,
            &PluginChatButtons::scheduleRebuild);
    rebuild();
}

void PluginChatButtons::scheduleRebuild()
{
    // Queued: the request may come from inside one of our buttons' click handlers
    if (rebuildPending)
        return;
    rebuildPending = true;
    QMetaObject::invokeMethod(this, [this] { rebuild(); }, Qt::QueuedConnection);
}

void PluginChatButtons::rebuild()
{
    rebuildPending = false;
    while (QLayoutItem *item = layout->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    auto *host = Qord::Plugins::PluginHost::instance();
    if (!host || !host->hasHook(Qord::Plugins::Hook::ChatBarButtons)) {
        setVisible(false);
        return;
    }

    const Qord::Plugins::ChatContext chat = contextProvider();
    const QColor normal = Core::Theme::Manager::instance().color(Core::Theme::Token::PlaceholderText);
    const QColor active = Core::Theme::Manager::instance().color(Core::Theme::Token::ButtonText);
    const auto buttons = host->dispatchChatBarButtons(chat);
    for (const auto &pb : buttons) {
        auto *button = new ClickButton(this);
        button->setAutoRaise(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setIconSize(QSize(20, 20));
        button->setIcon(iconFromSvg(pb.button.svg, pb.button.active ? active : normal, devicePixelRatioF()));
        button->setToolTip(pb.button.tooltip);
        button->setAccessibleName(pb.button.tooltip);
        const QString pluginId = pb.pluginId;
        const QString buttonId = pb.button.id;
        // Left, middle and right clicks all reach the plugin (SilentTyping maps each to an action)
        button->onClick = [this, pluginId, buttonId](Qord::Plugins::ChatContext::Click click) {
            Qord::Plugins::ChatContext chat = contextProvider();
            chat.click = click;
            if (auto *h = Qord::Plugins::PluginHost::instance())
                h->dispatchChatBarButtonClicked(pluginId, buttonId, chat);
        };
        layout->addWidget(button);
    }
    setVisible(!buttons.isEmpty());
}

} // namespace UI
} // namespace Acheron
