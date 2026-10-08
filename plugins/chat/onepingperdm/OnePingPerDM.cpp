/*
 * Qord, a native Discord client
 * Port of Equicord's OnePingPerDM plugin (src/plugins/onePingPerDM)
 * Original: Copyright (c) Vendicated and contributors, author ProffDea
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "OnePingPerDM.hpp"

#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

namespace Qord {
namespace Plugins {

bool OnePingPerDM::isPrivateChannelRead(const Config &c, const NotificationContext &n)
{
    const MessageContext &m = n.message;
    // upstream: settings.store.ignoreUsers.split(", ").filter(Boolean)
    for (const QString &id : c.ignoreUsers.split(QStringLiteral(", "), Qt::SkipEmptyParts))
        if (id == QString::number(m.authorId))
            return true;
    if ((!m.isDm && !m.isGroupDm) || (m.isDm && c.channelToAffect == QLatin1String("group_dm"))
        || (m.isGroupDm && c.channelToAffect == QLatin1String("user_dm")) || (c.allowMentions && m.mentionsMe)
        || (c.allowEveryone && m.mentionEveryone))
        return true;
    return n.isOldestUnread;
}

class OnePingPerDMPlugin : public Plugin
{
public:
    [[nodiscard]] Hooks hooks() const override { return Hook::Notifications; }
    void start(PluginContext &ctx) override { context = &ctx; }

    std::optional<bool> shouldNotify(const NotificationContext &notification) override
    {
        auto &s = context->settings();
        OnePingPerDM::Config c;
        c.channelToAffect = s.stringValue(QStringLiteral("channelToAffect"));
        c.allowMentions = s.boolValue(QStringLiteral("allowMentions"));
        c.allowEveryone = s.boolValue(QStringLiteral("allowEveryone"));
        c.ignoreUsers = s.stringValue(QStringLiteral("ignoreUsers"));
        if (OnePingPerDM::isPrivateChannelRead(c, notification))
            return std::nullopt;
        return false;
    }

private:
    PluginContext *context = nullptr;
};

QByteArray qord_manifest_onepingperdm();

void qord_register_onepingperdm(Registry &registry)
{
    registry.add({ QStringLiteral("onepingperdm/plugin.json"), [] { return std::make_unique<OnePingPerDMPlugin>(); },
                   qord_manifest_onepingperdm() });
}

} // namespace Plugins
} // namespace Qord
