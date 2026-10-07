/*
 * Qord, a native Discord client
 * Port of Equicord's NoBlockedMessages plugin (src/plugins/noBlockedMessages)
 * Original: Copyright (c) Vendicated and contributors,
 * authors rushii, Samu, jamesbt365, Elvyra, Etorix
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "Plugins/Plugin.hpp"

#include <QSet>

#include <functional>

namespace Qord {
namespace Plugins {

class PluginContext;

namespace NoBlockedMessages {

struct Config
{
    bool alsoHideIgnoredUsers = true;
    bool disableNotifications = false;
    bool allowAutoModMessages = true;
    bool hideBlockedUserReplies = false;
    bool defaultHideUsers = true;
    QSet<Id> overrideUsers;
};

struct Relationship
{
    bool blocked = false;
    bool ignored = false;
};
using RelationshipOf = std::function<Relationship(Id userId)>;

struct Verdict
{
    bool visible = true;
    bool suppressed = false; // author (or relevant user) is blocked/ignored
};

// upstream shouldKeepMessage(): [visible, suppressed]
Verdict evaluate(const Config &config, const MessageContext &message, const RelationshipOf &relationshipOf);

QSet<Id> parseOverrideUsers(const QString &text);

} // namespace NoBlockedMessages

class NoBlockedMessagesPlugin : public Plugin
{
public:
    [[nodiscard]] Hooks hooks() const override { return Hook::MessageFilter; }
    void start(PluginContext &context) override;
    void stop() override { context = nullptr; }
    void onSettingChanged(const QString &key) override;
    bool shouldHideMessage(const MessageContext &message) override;
    bool shouldSilenceMessage(const MessageContext &message) override;

private:
    NoBlockedMessages::Verdict verdict(const MessageContext &message) const;

    PluginContext *context = nullptr;
    NoBlockedMessages::Config config;
};

} // namespace Plugins
} // namespace Qord
