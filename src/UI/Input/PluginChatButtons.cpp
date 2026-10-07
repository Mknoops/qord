#include "PluginChatButtons.hpp"

#include "Core/Theme/Manager.hpp"
#include "Plugins/PluginHost.hpp"

#include <QHBoxLayout>
#include <QPainter>
#include <QSvgRenderer>
#include <QToolButton>

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
        auto *button = new QToolButton(this);
        button->setAutoRaise(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setIconSize(QSize(20, 20));
        button->setIcon(iconFromSvg(pb.button.svg, pb.button.active ? active : normal, devicePixelRatioF()));
        button->setToolTip(pb.button.tooltip);
        button->setAccessibleName(pb.button.tooltip);
        const QString pluginId = pb.pluginId;
        const QString buttonId = pb.button.id;
        connect(button, &QToolButton::clicked, this, [this, pluginId, buttonId] {
            if (auto *h = Qord::Plugins::PluginHost::instance())
                h->dispatchChatBarButtonClicked(pluginId, buttonId, contextProvider());
        });
        layout->addWidget(button);
    }
    setVisible(!buttons.isEmpty());
}

} // namespace UI
} // namespace Acheron
