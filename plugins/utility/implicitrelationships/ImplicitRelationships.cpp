/*
 * Qord, a native Discord client
 * Port of Equicord's ImplicitRelationships plugin (src/plugins/implicitRelationships)
 * Original: Copyright (c) Vendicated and contributors, author Dolfies
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "Plugins/Plugin.hpp"
#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

#include <QHash>

#include <algorithm>
#include <memory>

namespace Qord {
namespace Plugins {

namespace {
// The Friends page's "Implicit" tab: people Discord ranks you as talking to
// whom you have no relationship with
class ImplicitRelationshipsPlugin : public Plugin
{
public:
    [[nodiscard]] Hooks hooks() const override { return Hook::FriendsSections; }

    void start(PluginContext &ctx) override
    {
        context = &ctx;
        alive = std::make_shared<bool>(true);
    }

    void stop() override
    {
        alive.reset();
        affinities.clear();
        context = nullptr;
    }

    void onSettingChanged(const QString &key) override
    {
        Q_UNUSED(key);
        if (context)
            context->refreshFriends();
    }

    // fetchImplicitRelationships, when Discord would fetch relationships
    void onFriendsPageOpened(const FriendsContext &friends) override
    {
        const RelationshipDirectory *relationships = context ? context->relationships() : nullptr;
        if (!relationships)
            return;
        std::weak_ptr<bool> weak = alive;
        const Id accountId = friends.accountId;
        relationships->fetchUserAffinities(accountId, [this, weak, accountId](bool ok, const QList<UserAffinity> &list) {
            if (weak.expired() || !ok)
                return;
            affinities.insert(accountId, list);
            context->refreshFriends();
        });
    }

    QList<FriendsSection> friendsSections(const FriendsContext &friends) override
    {
        if (!context)
            return {};
        const RelationshipDirectory *relationships = context->relationships();
        // Users you have an affinity for and no relationship with
        QList<UserAffinity> implicit;
        for (const UserAffinity &a : affinities.value(friends.accountId))
            if (relationships && !relationships->hasRelationship(friends.accountId, a.userId))
                implicit.append(a);
        FriendsSection section;
        section.id = QStringLiteral("implicit");
        section.label = QStringLiteral("Implicit");
        section.countTitle = QStringLiteral("Implicit — %1");
        // Upstream uses the Online tab's empty state
        section.emptyText = QStringLiteral("No one's around to play with Wumpus.");
        section.sortByName = !context->settings().boolValue(QStringLiteral("sortByAffinity"));
        if (!section.sortByName)
            std::stable_sort(implicit.begin(), implicit.end(), [](const UserAffinity &a, const UserAffinity &b) {
                return a.communicationRank < b.communicationRank;
            });
        for (const UserAffinity &a : implicit)
            section.userIds.append(a.userId);
        return { section };
    }

private:
    PluginContext *context = nullptr;
    QHash<Id, QList<UserAffinity>> affinities; // by account, from the last fetch
    std::shared_ptr<bool> alive;
};
} // namespace

QByteArray qord_manifest_implicitrelationships();

void qord_register_implicitrelationships(Registry &registry)
{
    registry.add({ QStringLiteral("implicitrelationships/plugin.json"),
                   [] { return std::make_unique<ImplicitRelationshipsPlugin>(); },
                   qord_manifest_implicitrelationships() });
}

} // namespace Plugins
} // namespace Qord
