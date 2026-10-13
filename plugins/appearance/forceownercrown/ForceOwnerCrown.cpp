/*
 * Qord, a native Discord client
 * Port of Equicord's ForceOwnerCrown plugin (src/plugins/forceOwnerCrown)
 * Original: Copyright (c) Vendicated and contributors, authors D3SOX, Nickyux
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "Plugins/Plugin.hpp"
#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

namespace Qord {
namespace Plugins {

// Upstream makes the member list's isOwner check the server's owner id
// instead of Discord's own rule, which leaves the crown out in large servers.
class ForceOwnerCrownPlugin : public Plugin
{
public:
    [[nodiscard]] Hooks hooks() const override { return {}; }

    void start(PluginContext &context) override
    {
        options = context.displayOptions();
        if (options)
            options->setForceOwnerCrown(true);
    }
    void stop() override
    {
        if (options)
            options->setForceOwnerCrown(false);
        options = nullptr;
    }

private:
    DisplayOptionsService *options = nullptr;
};

QByteArray qord_manifest_forceownercrown();

void qord_register_forceownercrown(Registry &registry)
{
    registry.add({ QStringLiteral("forceownercrown/plugin.json"), [] { return std::make_unique<ForceOwnerCrownPlugin>(); },
                   qord_manifest_forceownercrown() });
}

} // namespace Plugins
} // namespace Qord
