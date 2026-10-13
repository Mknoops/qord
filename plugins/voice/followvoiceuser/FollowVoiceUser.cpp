/*
 * Qord, a native Discord client
 * Port of Equicord's FollowVoiceUser plugin (src/equicordplugins/followVoiceUser)
 * Original: Copyright (c) Vendicated and contributors, author TheArmagan
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "Plugins/Plugin.hpp"
#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"
#include "Plugins/Services.hpp"

namespace Qord {
namespace Plugins {

namespace {

// "Follow User" on a friend: join whichever voice channel they move to
class FollowVoiceUserPlugin : public Plugin
{
public:
    [[nodiscard]] Hooks hooks() const override { return Hook::VoiceStates | Hook::UserContextMenu; }

    void start(PluginContext &ctx) override { context = &ctx; }
    void stop() override
    {
        context = nullptr;
        followed.reset();
    }

    QList<MenuItem> userContextMenuItems(const UserContext &user) override
    {
        if (!context || user.userId == user.accountId || !isFriend(user.accountId, user.userId))
            return {};
        MenuItem item;
        item.id = QStringLiteral("fvu-follow-user");
        item.label = QStringLiteral("Follow User");
        item.checked = isFollowing(user.accountId, user.userId);
        return { item };
    }

    void onUserContextMenuAction(const QString &id, const UserContext &user) override
    {
        if (id != QLatin1String("fvu-follow-user"))
            return;
        if (isFollowing(user.accountId, user.userId)) {
            followed.reset();
            return;
        }
        // One person at a time, as upstream
        followed = Followed{ user.accountId, user.userId, 0 };
    }

    void onVoiceStateChanged(const VoiceStateChange &change) override
    {
        if (!context || !followed || change.accountId != followed->accountId || change.userId != followed->userId)
            return;
        if (!isFriend(followed->accountId, followed->userId))
            return;
        // Without "only when in voice" this also pulls you in from outside voice
        const VoiceStateDirectory *voice = context->voiceStates();
        if (context->settings().boolValue(QStringLiteral("onlyWhenInVoice"))
            && !(voice && voice->voiceState(change.accountId, change.accountId)))
            return;
        UiService *ui = context->ui();
        if (!ui)
            return;
        if (change.channelId && change.channelId != followed->lastChannelId) {
            followed->lastChannelId = change.channelId;
            ui->joinVoiceChannel(change.accountId, change.channelId);
        } else if (!change.channelId && context->settings().boolValue(QStringLiteral("leaveWhenUserLeaves"))) {
            ui->leaveVoice(change.accountId);
        }
    }

private:
    struct Followed
    {
        Id accountId = 0;
        Id userId = 0;
        Id lastChannelId = 0; // the channel last joined after them
    };

    bool isFollowing(Id accountId, Id userId) const
    {
        return followed && followed->accountId == accountId && followed->userId == userId;
    }

    bool isFriend(Id accountId, Id userId) const
    {
        const RelationshipDirectory *relationships = context ? context->relationships() : nullptr;
        return relationships
                && relationships->relationshipIds(accountId, RelationshipDirectory::Kind::Friend).contains(userId);
    }

    PluginContext *context = nullptr;
    std::optional<Followed> followed;
};
} // namespace

QByteArray qord_manifest_followvoiceuser();

void qord_register_followvoiceuser(Registry &registry)
{
    registry.add({ QStringLiteral("followvoiceuser/plugin.json"),
                   [] { return std::make_unique<FollowVoiceUserPlugin>(); }, qord_manifest_followvoiceuser() });
}

} // namespace Plugins
} // namespace Qord
