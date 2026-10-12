/*
 * Qord, a native Discord client
 * Port of Equicord's VolumeBooster plugin (src/plugins/volumeBooster)
 * Original: Copyright (c) Vendicated and contributors, authors Nuckyz, sadan
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "Plugins/Plugin.hpp"
#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

namespace Qord {
namespace Plugins {

// User volume sliders go up to 200% times the multiplier.
class VolumeBoosterPlugin : public Plugin
{
public:
    [[nodiscard]] Hooks hooks() const override { return {}; }

    void start(PluginContext &ctx) override
    {
        context = &ctx;
        audio = ctx.audio();
        apply();
    }
    void stop() override
    {
        if (audio)
            audio->setUserVolumeMultiplier(1.0);
        audio = nullptr;
        context = nullptr;
    }
    void onSettingChanged(const QString &key) override
    {
        if (key == QLatin1String("multiplier"))
            apply();
    }

private:
    void apply()
    {
        if (!audio || !context)
            return;
        bool ok = false;
        const double multiplier = context->settings().stringValue(QStringLiteral("multiplier")).toDouble(&ok);
        audio->setUserVolumeMultiplier(ok ? multiplier : 2.0);
    }

    PluginContext *context = nullptr;
    AudioOptionsService *audio = nullptr;
};

QByteArray qord_manifest_volumebooster();

void qord_register_volumebooster(Registry &registry)
{
    registry.add({ QStringLiteral("volumebooster/plugin.json"), [] { return std::make_unique<VolumeBoosterPlugin>(); },
                   qord_manifest_volumebooster() });
}

} // namespace Plugins
} // namespace Qord
