/*
 * Qord, a native Discord client
 * Port of Equicord's BetterFolders plugin (src/plugins/betterFolders)
 * Original: Copyright (c) Vendicated and contributors, authors Juby210, AutumnVN, Nuckyz
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "Plugins/Plugin.hpp"
#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

namespace Qord {
namespace Plugins {

// Open server folders on a second rail beside the server list, and rules for
// when folders open and close.
class BetterFoldersPlugin : public Plugin
{
public:
    [[nodiscard]] Hooks hooks() const override { return {}; }

    void start(PluginContext &ctx) override
    {
        context = &ctx;
        options = ctx.displayOptions();
        apply();
    }
    void stop() override
    {
        if (options)
            options->setFolderSidebar(std::nullopt);
        options = nullptr;
        context = nullptr;
    }
    void onSettingChanged(const QString &key) override
    {
        Q_UNUSED(key);
        apply();
    }

private:
    void apply()
    {
        if (!options || !context)
            return;
        const PluginSettings &settings = context->settings();
        FolderSidebar folders;
        folders.sidebar = settings.boolValue(QStringLiteral("sidebar"));
        folders.closeAllFolders = settings.boolValue(QStringLiteral("closeAllFolders"));
        folders.closeAllHomeButton = settings.boolValue(QStringLiteral("closeAllHomeButton"));
        folders.closeOthers = settings.boolValue(QStringLiteral("closeOthers"));
        folders.closeServerFolder = settings.boolValue(QStringLiteral("closeServerFolder"));
        folders.forceOpen = settings.boolValue(QStringLiteral("forceOpen"));
        folders.keepIcons = settings.boolValue(QStringLiteral("keepIcons"));
        const QString icon = settings.stringValue(QStringLiteral("showFolderIcon"));
        folders.folderIcon = icon == QLatin1String("never")   ? FolderSidebar::FolderIcon::Never
                             : icon == QLatin1String("more") ? FolderSidebar::FolderIcon::MoreThanOneFolderExpanded
                                                             : FolderSidebar::FolderIcon::Always;
        options->setFolderSidebar(folders);
    }

    PluginContext *context = nullptr;
    DisplayOptionsService *options = nullptr;
};

QByteArray qord_manifest_betterfolders();

void qord_register_betterfolders(Registry &registry)
{
    registry.add({ QStringLiteral("betterfolders/plugin.json"), [] { return std::make_unique<BetterFoldersPlugin>(); },
                   qord_manifest_betterfolders() });
}

} // namespace Plugins
} // namespace Qord
