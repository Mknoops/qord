#pragma once

#include "Plugins/PluginHost.hpp"

#include <QMenu>

#include <functional>

namespace Acheron {
namespace UI {

// Qord: plugin menu items in a menu: submenus, separators (an item with
// neither id nor label) and checkbox items; `act` runs with the plugin and
// item id when one is chosen
inline void addPluginMenuItems(QMenu *menu, const QList<Qord::Plugins::PluginHost::PluginMenuItem> &items,
                               const std::function<void(const QString &pluginId, const QString &itemId)> &act)
{
    std::function<void(QMenu *, const QString &, const Qord::Plugins::MenuItem &)> add =
            [&](QMenu *target, const QString &pluginId, const Qord::Plugins::MenuItem &item) {
                if (!item.children.isEmpty()) {
                    QMenu *sub = target->addMenu(item.label);
                    for (const auto &child : item.children)
                        add(sub, pluginId, child);
                    return;
                }
                if (item.id.isEmpty() && item.label.isEmpty()) {
                    target->addSeparator();
                    return;
                }
                QAction *action = target->addAction(item.label);
                if (item.checked) {
                    action->setCheckable(true);
                    action->setChecked(*item.checked);
                }
                QObject::connect(action, &QAction::triggered, menu,
                                 [act, pluginId, itemId = item.id]() { act(pluginId, itemId); });
            };
    for (const auto &entry : items)
        add(menu, entry.pluginId, entry.item);
}

} // namespace UI
} // namespace Acheron
