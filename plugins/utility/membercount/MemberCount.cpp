/*
 * Qord, a native Discord client
 * Port of Equicord's MemberCount plugin (src/plugins/memberCount)
 * Original: Copyright (c) Vendicated and contributors, authors Ven, Commandtechno, Apexo
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "Plugins/Plugin.hpp"
#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

#include <QLocale>

namespace Qord {
namespace Plugins {

class MemberCountPlugin : public Plugin
{
public:
    [[nodiscard]] Hooks hooks() const override { return Hook::MemberCounts; }
    void start(PluginContext &ctx) override { context = &ctx; }
    void stop() override { context = nullptr; }

    QList<CountBadge> memberCountBadges(const MemberCountContext &counts) override
    {
        if (!context)
            return {};
        const bool isTooltip = counts.place == MemberCountContext::Place::ServerTooltip;
        if (!context->settings().boolValue(isTooltip ? QStringLiteral("toolTip") : QStringLiteral("memberList")))
            return {};
        // if (totalCount == null) return null;
        if (counts.total < 0)
            return {};
        const bool includeVoice = context->settings().boolValue(QStringLiteral("voiceActivity")) && !isTooltip;

        // new Intl.NumberFormat().format(): grouped in the user's locale
        const QLocale locale;
        const QString online = counts.online >= 0 ? locale.toString(counts.online) : QStringLiteral("?");
        const QString total = locale.toString(counts.total);
        QList<CountBadge> badges {
            { CountBadge::Icon::Online, online, QStringLiteral("%1 online in this channel").arg(online) },
            { CountBadge::Icon::Total, total, QStringLiteral("%1 total server members").arg(total) },
        };
        if (includeVoice && counts.inVoice > 0) {
            const QString voice = locale.toString(counts.inVoice);
            badges.append({ CountBadge::Icon::Voice, voice, QStringLiteral("%1 members in voice").arg(voice) });
        }
        return badges;
    }

private:
    PluginContext *context = nullptr;
};

QByteArray qord_manifest_membercount();

void qord_register_membercount(Registry &registry)
{
    registry.add({ QStringLiteral("membercount/plugin.json"), [] { return std::make_unique<MemberCountPlugin>(); },
                   qord_manifest_membercount() });
}

} // namespace Plugins
} // namespace Qord
