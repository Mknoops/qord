/*
 * Qord, a native Discord client
 * Port of Equicord's AlwaysTrust plugin (src/plugins/alwaysTrust)
 * Original: Copyright (c) Vendicated and contributors, authors zt, trey
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "Plugins/Plugin.hpp"
#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

namespace Qord {
namespace Plugins {

class AlwaysTrustPlugin : public Plugin
{
public:
    [[nodiscard]] Hooks hooks() const override { return Hook::LinkWarnings; }

    void start(PluginContext &ctx) override { context = &ctx; }

    // Upstream: isTrustedDomain() returns true while "domain" is on
    std::optional<bool> shouldConfirmLink(const QString &url) override
    {
        Q_UNUSED(url);
        if (context->settings().boolValue(QStringLiteral("domain")))
            return false;
        return std::nullopt;
    }

private:
    PluginContext *context = nullptr;
};

QByteArray qord_manifest_alwaystrust();

void qord_register_alwaystrust(Registry &registry)
{
    registry.add({ QStringLiteral("alwaystrust/plugin.json"), [] { return std::make_unique<AlwaysTrustPlugin>(); },
                   qord_manifest_alwaystrust() });
}

} // namespace Plugins
} // namespace Qord
