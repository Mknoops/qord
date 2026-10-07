/*
 * Qord, a native Discord client
 * Port of Equicord's Ingtoninator plugin (src/equicordplugins/ingtoninator)
 * Original: Copyright (c) Vendicated and contributors, authors zyqunix, BioTomateDE
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "Plugins/Plugin.hpp"

#include <functional>

namespace Qord {
namespace Plugins {

class PluginContext;

namespace Ingtoninator {

// Math.random(): a number in [0, 1)
using Random = std::function<double()>;

// The suffix that completes `upperWord` to end in "INGTON".
QString ington(const QString &upperWord);

// Appends "ington" (matching the word's case) to one random word, skipping
// links, "i" and words ending in a vowel or y. Unchanged if no word fits.
QString apply(const QString &message, const Random &random);

} // namespace Ingtoninator

class IngtoninatorPlugin : public Plugin
{
public:
    [[nodiscard]] Hooks hooks() const override { return Hook::BeforeSend | Hook::ChatBarButtons; }
    void start(PluginContext &context) override { this->context = &context; }
    void stop() override { context = nullptr; }
    void onSettingChanged(const QString &key) override;
    void onBeforeSend(OutgoingMessage &message) override;
    QList<ChatBarButton> chatBarButtons(const ChatContext &chat) override;
    void onChatBarButtonClicked(const QString &id, const ChatContext &chat) override;

private:
    PluginContext *context = nullptr;
};

} // namespace Plugins
} // namespace Qord
