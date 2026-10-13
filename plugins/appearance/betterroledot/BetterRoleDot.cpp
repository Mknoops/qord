/*
 * Qord, a native Discord client
 * Port of Equicord's BetterRoleDot plugin (src/plugins/betterRoleDot)
 * Original: Copyright (c) Vendicated and contributors, authors Ven, AutumnVN
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "Plugins/Plugin.hpp"
#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

namespace Qord {
namespace Plugins {

namespace {
// Role dots copy their color; names can keep theirs next to the dot
class BetterRoleDotPlugin : public Plugin
{
public:
    [[nodiscard]] Hooks hooks() const override { return {}; }

    void start(PluginContext &ctx) override
    {
        context = &ctx;
        apply();
    }

    void stop() override
    {
        if (context && context->displayOptions())
            context->displayOptions()->setRoleDot(std::nullopt);
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
        DisplayOptionsService *display = context ? context->displayOptions() : nullptr;
        if (!display)
            return;
        RoleDot dot;
        dot.bothStyles = context->settings().boolValue(QStringLiteral("bothStyles"));
        // Upstream's profile patch only applies without bothStyles, whose
        // own patch already turns profile roles into dots
        dot.inProfile = context->settings().boolValue(QStringLiteral("copyRoleColorInProfilePopout"));
        display->setRoleDot(dot);
    }

    PluginContext *context = nullptr;
};
} // namespace

QByteArray qord_manifest_betterroledot();

void qord_register_betterroledot(Registry &registry)
{
    registry.add({ QStringLiteral("betterroledot/plugin.json"), [] { return std::make_unique<BetterRoleDotPlugin>(); },
                   qord_manifest_betterroledot() });
}

} // namespace Plugins
} // namespace Qord
