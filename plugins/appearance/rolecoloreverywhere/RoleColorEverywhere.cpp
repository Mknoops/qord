/*
 * Qord, a native Discord client
 * Port of Equicord's RoleColorEverywhere plugin (src/plugins/roleColorEverywhere)
 * Original: Copyright (c) Vendicated and contributors, authors KingFish, lewisakura, AutumnVN, Kyuuhachi, jamesbt365
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "Plugins/Plugin.hpp"
#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

namespace Qord {
namespace Plugins {

// Members' top role color in mentions, the member list's role headers,
// voice users and (optionally) message text. Qord has no reactors list or
// poll voter list, so those settings have nothing to color yet.
class RoleColorEverywherePlugin : public Plugin
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
            options->setRoleColors({});
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
        RoleColors colors;
        colors.mentions = settings.boolValue(QStringLiteral("chatMentions"));
        colors.memberListHeaders = settings.boolValue(QStringLiteral("memberList"));
        colors.voiceUsers = settings.boolValue(QStringLiteral("voiceUsers"));
        colors.messages = settings.boolValue(QStringLiteral("colorChatMessages"));
        colors.messageSaturation = int(settings.intValue(QStringLiteral("messageSaturation")));
        options->setRoleColors(colors);
    }

    PluginContext *context = nullptr;
    DisplayOptionsService *options = nullptr;
};

QByteArray qord_manifest_rolecoloreverywhere();

void qord_register_rolecoloreverywhere(Registry &registry)
{
    registry.add({ QStringLiteral("rolecoloreverywhere/plugin.json"),
                   [] { return std::make_unique<RoleColorEverywherePlugin>(); }, qord_manifest_rolecoloreverywhere() });
}

} // namespace Plugins
} // namespace Qord
