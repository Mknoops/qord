/*
 * Qord, a native Discord client
 * Port of Equicord's NoReplyMention plugin (src/plugins/noReplyMention)
 * Original: Copyright (c) 2022 Vendicated and contributors,
 * authors DustyAngel47, rae, pylix, outfoxxed
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "Plugins/Plugin.hpp"

#include <QSet>

namespace Qord {
namespace Plugins {

class PluginContext;

namespace NoReplyMention {

// Ids separated by commas and/or whitespace.
QSet<Id> parseIdList(const QString &text);

struct Config
{
    QSet<Id> users;
    QSet<Id> roles;
    bool shouldPingListed = true;
    bool inverseShiftReply = false;
};

// isExempt = shouldPingListed ? listed : !listed
// result   = inverseShiftReply ? (shift != isExempt) : (!shift && isExempt)
bool shouldMention(const Config &config, bool listed, bool shiftHeld);

// Listed if the author is in the user list or, in a guild, holds a role in
// the role list (roles come from the member cache; unknown member = no roles).
bool isListed(const Config &config, Id authorId, const std::optional<QList<Id>> &authorRoles);

} // namespace NoReplyMention

class NoReplyMentionPlugin : public Plugin
{
public:
    [[nodiscard]] Hooks hooks() const override { return Hook::ShouldMentionOnReply; }
    void start(PluginContext &context) override;
    void stop() override;
    void onSettingChanged(const QString &key) override;
    std::optional<bool> shouldMentionOnReply(const ReplyContext &reply) override;

private:
    void reload();

    PluginContext *context = nullptr;
    NoReplyMention::Config config;
};

} // namespace Plugins
} // namespace Qord
