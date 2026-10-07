/*
 * Qord, a native Discord client
 * Port of Equicord's PolishWording plugin (src/equicordplugins/polishWording)
 * Original: Copyright (c) Vendicated and contributors, authors Samwich, WKoA
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "Plugins/Plugin.hpp"

#include <QStringList>

#include <functional>

namespace Qord {
namespace Plugins {

class PluginContext;

namespace PolishWording {

struct Options
{
    bool quickDisable = false;
    QString blockedWords;
    bool fixApostrophes = true;
    bool expandContractions = false;
    bool fixCapitalization = false;
    bool fixPunctuation = false;
    int fixPunctuationFrequency = 100; // percent
};

// Math.random(): a number in [0, 1)
using Random = std::function<double()>;

// upstream textProcessing(): code blocks are preserved; then, as enabled,
// missing apostrophes are restored, sentences capitalized, periods added
// (with the given frequency) and contractions expanded.
QString process(const QString &input, const Options &options, const Random &random);

QString ensureApostrophe(const QString &text);
QString expandContractions(const QString &text);
QString capitalize(const QString &text, const QString &blockedWords);
QString addPeriods(const QString &text);

} // namespace PolishWording

class PolishWordingPlugin : public Plugin
{
public:
    [[nodiscard]] Hooks hooks() const override { return Hook::BeforeSend; }
    void start(PluginContext &context) override;
    void stop() override { context = nullptr; }
    void onSettingChanged(const QString &key) override;
    void onBeforeSend(OutgoingMessage &message) override;

private:
    PluginContext *context = nullptr;
    PolishWording::Options options;
};

} // namespace Plugins
} // namespace Qord
