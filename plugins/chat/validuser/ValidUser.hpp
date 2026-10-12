/*
 * Qord, a native Discord client
 * Port of Equicord's ValidUser plugin (src/plugins/validUser)
 * Original: Copyright (c) Vendicated and contributors, authors Ven, Dolfies
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "Plugins/Plugin.hpp"

#include <QSet>

#include <deque>
#include <functional>
#include <memory>

namespace Qord {
namespace Plugins {

class PluginContext;

// Looks up the users behind "@unknown-user" mentions when they are hovered.
class ValidUserPlugin : public Plugin
{
public:
    static constexpr int MaxQueued = 5; // Vencord's Queue(5): the oldest waiting lookup is dropped
    static constexpr int SpacingMs = 300;

    [[nodiscard]] Hooks hooks() const override { return Hook::UnknownMentions; }
    void start(PluginContext &context) override;
    void stop() override;
    void onUserMentionHovered(const MentionContext &mention) override;

private:
    void fetch(const MentionContext &mention);
    void runNext();
    PluginContext *context = nullptr;
    QSet<Id> fetching;
    std::deque<std::pair<Id, std::function<void()>>> queue; // user id, task
    bool running = false;
    std::shared_ptr<bool> alive;
};

} // namespace Plugins
} // namespace Qord
