#pragma once

#include <QHash>
#include <QString>

#include <functional>

class QWidget;

namespace Qord {
namespace Plugins {

class PluginSettings;

// Widget-side plugin API, compiled into the client only (the host library
// stays free of Qt Widgets). The Plugins settings page generates editors
// from each plugin's settings schema; a plugin can supply its own widget
// for its structured ("json") settings.
using SettingsWidgetFactory = std::function<QWidget *(PluginSettings &settings, QWidget *parent)>;

class SettingsWidgetRegistry
{
public:
    // The widget edits every "json" setting of the plugin; the page still
    // generates editors for the plugin's other settings.
    void add(const QString &pluginId, SettingsWidgetFactory factory) { factories.insert(pluginId, std::move(factory)); }
    [[nodiscard]] const SettingsWidgetFactory *find(const QString &pluginId) const
    {
        auto it = factories.constFind(pluginId);
        return it == factories.constEnd() ? nullptr : &*it;
    }

private:
    QHash<QString, SettingsWidgetFactory> factories;
};

// Defined in the generated BuiltinPluginWidgets.cpp.
void registerBuiltinPluginWidgets(SettingsWidgetRegistry &registry);

} // namespace Plugins
} // namespace Qord
