#pragma once

#include "Plugin.hpp"

#include <QByteArray>
#include <QList>
#include <QString>

#include <functional>
#include <memory>

namespace Qord {
namespace Plugins {

// A plugin compiled into the client: its manifest and how to construct it.
// Construction only happens on enable.
struct PluginDescriptor
{
    // Where the manifest came from (a file path, or "<id>/plugin.json" for
    // embedded manifests); used in messages and read when `manifest` is empty.
    QString manifestPath;
    std::function<std::unique_ptr<Plugin>()> create;
    // Manifest contents, embedded at build time for built-in plugins.
    QByteArray manifest;
};

class Registry
{
public:
    void add(PluginDescriptor descriptor) { descriptors.append(std::move(descriptor)); }
    [[nodiscard]] const QList<PluginDescriptor> &all() const { return descriptors; }

private:
    QList<PluginDescriptor> descriptors;
};

// Defined in the generated BuiltinPlugins.cpp (see plugins/CMakeLists.txt).
void registerBuiltinPlugins(Registry &registry);

} // namespace Plugins
} // namespace Qord
