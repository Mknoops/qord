/*
 * Qord, a native Discord client
 * Port of Equicord's TextReplace plugin (src/plugins/textReplace)
 * Original: Copyright (c) 2023 Vendicated and contributors,
 * authors AutumnVN, TheKodeToad, Etorix, Ape
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "Plugins/JsCompat.hpp"
#include "Plugins/Plugin.hpp"

#include <QJsonArray>
#include <QList>
#include <QSet>
#include <QString>

#include <functional>
#include <optional>

namespace Qord {
namespace Plugins {
namespace TextReplace {

enum class Scope { MyMessages, OthersMessages, AllMessages };

Scope scopeFromString(const QString &s); // unknown -> MyMessages, like upstream's normalizeRule
QString scopeToString(Scope scope);

struct Rule
{
    QString id;
    QString name;
    QString find;
    QString replace;
    QString onlyIfIncludes;
    Scope scope = Scope::MyMessages;

    [[nodiscard]] bool isEmpty() const { return find.isEmpty(); }
};

QList<Rule> rulesFromJson(const QJsonArray &array);
QJsonArray rulesToJson(const QList<Rule> &rules);

// Parses "/pattern/flags" or a bare pattern into a JS RegExp the way
// upstream's stringToRegex does. Returns an error message on failure.
QString checkRegex(const QString &find);

using ErrorSink = std::function<void(const QString &)>;

// Rules with their regexes compiled once (upstream compiles per message;
// the result is the same).
struct RuleSet
{
    QList<Rule> stringRules;
    QList<Rule> regexRules;
    QList<std::optional<Js::RegExp>> compiled; // parallel to regexRules

    static RuleSet build(QList<Rule> stringRules, QList<Rule> regexRules);
};

// Applies string rules, then regex rules, in order, keeping those whose
// scope matches. Mirrors upstream applyRules() exactly, including padding
// string replacements with spaces and trimming the result. Invalid regex
// rules are reported through onRegexError and skipped.
QString applyRules(QString content, Scope scope, const RuleSet &rules, const ErrorSink &onRegexError = {});
QString applyRules(QString content, Scope scope, const QList<Rule> &stringRules, const QList<Rule> &regexRules,
                   const ErrorSink &onRegexError = {});

// Channels where upstream never rewrites outgoing messages (rule sharing and
// support channels).
const QSet<Id> &exemptChannelIds();

} // namespace TextReplace

class TextReplacePlugin : public Plugin
{
public:
    [[nodiscard]] Hooks hooks() const override { return Hook::BeforeSend | Hook::TransformIncomingContent; }
    void start(PluginContext &context) override;
    void stop() override;
    void onSettingChanged(const QString &key) override;
    void onBeforeSend(OutgoingMessage &message) override;
    void transformIncomingContent(IncomingMessage &message) override;

private:
    void reload();

    PluginContext *context = nullptr;
    TextReplace::RuleSet rules;
};

} // namespace Plugins
} // namespace Qord
