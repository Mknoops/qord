/*
 * Qord, a native Discord client
 * Port of Equicord's WriteUpperCase plugin (src/equicordplugins/writeUpperCase)
 * Original: Copyright (c) Vendicated and contributors, authors Samwich, KrystalSkull
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "Plugins/Plugin.hpp"

#include <QStringList>

namespace Qord {
namespace Plugins {

class PluginContext;

namespace WriteUpperCase {

// "http, https, ok" -> {"http", "https", "ok"}; split on ", " like upstream,
// empty entries ignored.
QStringList parseBlockedWords(const QString &setting);

// Capitalizes the first character of every sentence, where sentences are
// separated by whitespace following . ! or ? (optionally followed by
// closing quotes/brackets). Sentences starting with a blocked word are left
// alone.
QString capitalize(const QString &content, const QStringList &blockedWords);

} // namespace WriteUpperCase

class WriteUpperCasePlugin : public Plugin
{
public:
    [[nodiscard]] Hooks hooks() const override { return Hook::BeforeSend; }
    void start(PluginContext &context) override;
    void stop() override { context = nullptr; }
    void onSettingChanged(const QString &key) override;
    void onBeforeSend(OutgoingMessage &message) override;

private:
    PluginContext *context = nullptr;
    QStringList blocked;
};

} // namespace Plugins
} // namespace Qord
