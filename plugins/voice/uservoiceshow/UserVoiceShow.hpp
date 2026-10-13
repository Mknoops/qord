/*
 * Qord, a native Discord client
 * Port of Equicord's UserVoiceShow plugin (src/plugins/userVoiceShow)
 * Original: Copyright (c) Vendicated and contributors, authors Nuckyz, LordElias, omaw
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "Plugins/Plugin.hpp"

#include <memory>

class QTimer;

namespace Qord {
namespace Plugins {

class PluginContext;

// A speaker after the names of people in voice; click it to open their
// channel, double click to join it.
class UserVoiceShowPlugin : public Plugin
{
public:
    static constexpr int ClickDelayMs = 250; // a single click waits for a second one
    static constexpr int MaxTooltipUsers = 13;

    enum class Icon { Speaker, LockedSpeaker, Muted, Deaf };
    static QByteArray iconSvg(Icon icon, int size);

    UserVoiceShowPlugin();
    ~UserVoiceShowPlugin() override;

    [[nodiscard]] Hooks hooks() const override { return Hook::UserDecorations; }
    void start(PluginContext &context) override;
    void stop() override;

    QList<NameDecoration> nameDecorations(const DecorationContext &user) override;
    void onNameDecorationClicked(const QString &clickId, const DecorationContext &user, bool doubleClick) override;

private:
    struct Shown
    {
        UserVoiceState state;
        LinkedChannel channel;
        bool isPrivate = false;
        bool locked = false;
    };
    // The user's voice channel, if the indicator shows for it
    [[nodiscard]] std::optional<Shown> shownFor(Id accountId, Id userId) const;
    [[nodiscard]] QString tooltip(Id accountId, const Shown &shown) const;

    PluginContext *context = nullptr;
    std::unique_ptr<QTimer> clickTimer;
    Id pendingAccount = 0;
    Id pendingChannel = 0;
};

} // namespace Plugins
} // namespace Qord
