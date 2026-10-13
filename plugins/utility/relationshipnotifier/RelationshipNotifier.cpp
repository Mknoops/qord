/*
 * Qord, a native Discord client
 * Port of Equicord's RelationshipNotifier plugin (src/plugins/relationshipNotifier)
 * Original: Copyright (c) Vendicated and contributors, author nick
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "Plugins/Plugin.hpp"
#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"
#include "Plugins/Services.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

#include <memory>

namespace Qord {
namespace Plugins {

namespace {

QJsonArray toJson(const QList<MembershipEntry> &entries)
{
    QJsonArray out;
    for (const MembershipEntry &e : entries)
        out.append(QJsonObject{ { QStringLiteral("id"), QString::number(e.id) },
                                { QStringLiteral("name"), e.name },
                                { QStringLiteral("icon"), e.iconUrl } });
    return out;
}

QList<MembershipEntry> fromJson(const QJsonValue &value)
{
    QList<MembershipEntry> out;
    for (const QJsonValue &v : value.toArray()) {
        const QJsonObject o = v.toObject();
        out.append({ o.value(QStringLiteral("id")).toString().toULongLong(), o.value(QStringLiteral("name")).toString(),
                     o.value(QStringLiteral("icon")).toString() });
    }
    return out;
}

bool contains(const QList<MembershipEntry> &entries, Id id)
{
    return std::any_of(entries.begin(), entries.end(), [id](const MembershipEntry &e) { return e.id == id; });
}

// Tells you when a friend, a friend request, a server or a group DM goes
// away without you, live and while Qord was closed
class RelationshipNotifierPlugin : public Plugin
{
public:
    [[nodiscard]] Hooks hooks() const override { return Hook::Memberships; }

    void start(PluginContext &ctx) override
    {
        context = &ctx;
        checked.clear();
        alive = std::make_shared<bool>(true);
    }
    void stop() override
    {
        context = nullptr;
        alive.reset();
    }

    void onMembershipsChanged(const Memberships &now) override
    {
        if (!context)
            return;
        auto &settings = context->settings();
        const QString account = QString::number(now.accountId);
        QJsonObject known = settings.value(QStringLiteral("known")).toObject();
        // Once per account each run: what went away while Qord was closed
        if (!checked.contains(now.accountId)) {
            checked.insert(now.accountId);
            if (known.contains(account) && settings.boolValue(QStringLiteral("offlineRemovals")))
                notifyOffline(known.value(account).toObject(), now);
        }
        known.insert(account, QJsonObject{ { QStringLiteral("friends"), toJson(now.friends) },
                                           { QStringLiteral("requests"), toJson(now.incomingRequests) },
                                           { QStringLiteral("servers"), toJson(now.servers) },
                                           { QStringLiteral("groups"), toJson(now.groups) } });
        settings.setValue(QStringLiteral("known"), known);
    }

    void onRemoved(const Removal &removal) override
    {
        if (!context || removal.byYou)
            return;
        auto &settings = context->settings();
        const MembershipEntry &e = removal.entry;
        switch (removal.kind) {
        case Removal::Kind::Friend:
            if (settings.boolValue(QStringLiteral("friends")))
                notify(QStringLiteral("%1 removed you as a friend.").arg(e.name), e.iconUrl, removal.accountId, e.id);
            break;
        case Removal::Kind::FriendRequest:
            if (settings.boolValue(QStringLiteral("friendRequestCancels")))
                notify(QStringLiteral("A friend request from %1 has been removed.").arg(e.name), e.iconUrl,
                       removal.accountId, e.id);
            break;
        case Removal::Kind::Server:
            if (settings.boolValue(QStringLiteral("servers")))
                notify(QStringLiteral("You were removed from the server %1.").arg(e.name), e.iconUrl);
            break;
        case Removal::Kind::Group:
            if (settings.boolValue(QStringLiteral("groups")))
                notify(QStringLiteral("You were removed from the group %1.").arg(e.name), e.iconUrl);
            break;
        }
    }

private:
    void notifyOffline(const QJsonObject &old, const Memberships &now)
    {
        auto &settings = context->settings();
        if (settings.boolValue(QStringLiteral("groups")))
            for (const MembershipEntry &e : fromJson(old.value(QStringLiteral("groups"))))
                if (!contains(now.groups, e.id))
                    notify(QStringLiteral("You are no longer in the group %1.").arg(e.name), e.iconUrl);
        if (settings.boolValue(QStringLiteral("servers")))
            for (const MembershipEntry &e : fromJson(old.value(QStringLiteral("servers"))))
                if (!contains(now.servers, e.id))
                    notify(QStringLiteral("You are no longer in the server %1.").arg(e.name), e.iconUrl);
        if (settings.boolValue(QStringLiteral("friends")))
            for (const MembershipEntry &e : fromJson(old.value(QStringLiteral("friends"))))
                if (!contains(now.friends, e.id))
                    notify(QStringLiteral("You are no longer friends with %1.").arg(e.name), e.iconUrl, now.accountId,
                           e.id);
        if (settings.boolValue(QStringLiteral("friendRequestCancels"))) {
            // Accepted, blocked or answered with your own request: not revoked
            const RelationshipDirectory *relationships = context->relationships();
            for (const MembershipEntry &e : fromJson(old.value(QStringLiteral("requests"))))
                if (!contains(now.incomingRequests, e.id)
                    && !(relationships && relationships->hasRelationship(now.accountId, e.id)))
                    notify(QStringLiteral("Friend request from %1 has been revoked.").arg(e.name), e.iconUrl,
                           now.accountId, e.id);
        }
    }

    // userId set: clicking opens their profile
    void notify(const QString &text, const QString &iconUrl, Id accountId = 0, Id userId = 0)
    {
        UiService *ui = context ? context->ui() : nullptr;
        if (!ui)
            return;
        PluginNotification n;
        n.title = QStringLiteral("Relationship Notifier");
        n.body = text;
        n.iconUrl = iconUrl;
        // No notice bar in Qord: a notice is a notification that stays
        n.permanent = context->settings().boolValue(QStringLiteral("notices"));
        std::function<void()> clicked;
        if (userId) {
            std::weak_ptr<bool> weak = alive;
            clicked = [this, weak, accountId, userId]() {
                if (UiService *ui = !weak.expired() && context ? context->ui() : nullptr)
                    ui->openUserProfile(accountId, 0, userId);
            };
        }
        ui->showNotification(n, clicked);
    }

    PluginContext *context = nullptr;
    QSet<Id> checked; // accounts compared with what was known this run
    std::shared_ptr<bool> alive;
};

} // namespace

QByteArray qord_manifest_relationshipnotifier();

void qord_register_relationshipnotifier(Registry &registry)
{
    registry.add({ QStringLiteral("relationshipnotifier/plugin.json"),
                   [] { return std::make_unique<RelationshipNotifierPlugin>(); }, qord_manifest_relationshipnotifier() });
}

} // namespace Plugins
} // namespace Qord
