/*
 * Qord, a native Discord client
 * Port of Equicord's DontRoundMyTimestamps plugin (src/plugins/dontRoundMyTimestamps)
 * Original: Copyright (c) Vendicated and contributors, author Lexi
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "Plugins/Plugin.hpp"
#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

namespace Qord {
namespace Plugins {

class DontRoundMyTimestampsPlugin : public Plugin
{
public:
    [[nodiscard]] Hooks hooks() const override { return {}; }

    // moment.relativeTimeRounding(Math.floor) / (Math.round)
    void start(PluginContext &context) override
    {
        options = context.displayOptions();
        if (options)
            options->setRelativeTimeRoundsDown(true);
    }
    void stop() override
    {
        if (options)
            options->setRelativeTimeRoundsDown(false);
    }

private:
    DisplayOptionsService *options = nullptr;
};

QByteArray qord_manifest_dontroundmytimestamps();

void qord_register_dontroundmytimestamps(Registry &registry)
{
    registry.add({ QStringLiteral("dontroundmytimestamps/plugin.json"),
                   [] { return std::make_unique<DontRoundMyTimestampsPlugin>(); },
                   qord_manifest_dontroundmytimestamps() });
}

} // namespace Plugins
} // namespace Qord
