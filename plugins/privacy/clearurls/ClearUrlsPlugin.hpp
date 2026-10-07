/*
 * Qord, a native Discord client
 * Port of Equicord's ClearURLs plugin (src/plugins/clearURLs)
 * Original: Copyright (c) 2022 Vendicated and contributors, authors adryd and thororen
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "ClearUrls.hpp"

#include "Plugins/Plugin.hpp"
#include "Plugins/Services.hpp"

namespace Qord {
namespace Plugins {

// Removes tracking parameters from URLs in messages you send or edit.
// The rules are downloaded once and cached; they're refreshed at start when
// older than a week. Without the network permission, a cached copy is used
// if there is one.
class ClearUrlsPlugin : public Plugin
{
public:
    static constexpr qint64 RefreshAfterSeconds = 7 * 24 * 60 * 60;

    [[nodiscard]] Hooks hooks() const override { return Hook::BeforeSend | Hook::BeforeEdit; }
    void start(PluginContext &context) override;
    void stop() override;
    void onBeforeSend(OutgoingMessage &message) override;
    void onBeforeEdit(OutgoingMessage &message) override;

    [[nodiscard]] const ClearUrls::RuleSet &ruleSet() const { return rules; }

private:
    PluginContext *context = nullptr;
    ClearUrls::RuleSet rules;
    NetworkService::Handle request = 0;
};

} // namespace Plugins
} // namespace Qord
