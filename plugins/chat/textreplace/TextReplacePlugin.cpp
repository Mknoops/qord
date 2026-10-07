/*
 * Qord, a native Discord client
 * Port of Equicord's TextReplace plugin (src/plugins/textReplace)
 * Original: Copyright (c) 2023 Vendicated and contributors,
 * authors AutumnVN, TheKodeToad, Etorix, Ape
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "TextReplace.hpp"

#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

#include <QSet>

namespace Qord {
namespace Plugins {

using namespace TextReplace;

void TextReplacePlugin::start(PluginContext &ctx)
{
    context = &ctx;
    reload();
}

void TextReplacePlugin::stop()
{
    rules = {};
    context = nullptr;
}

void TextReplacePlugin::onSettingChanged(const QString &key)
{
    Q_UNUSED(key);
    reload();
}

void TextReplacePlugin::reload()
{
    PluginSettings &s = context->settings();
    rules = RuleSet::build(rulesFromJson(s.value(QStringLiteral("stringRules")).toArray()),
                           rulesFromJson(s.value(QStringLiteral("regexRules")).toArray()));
    // Upstream logs on every message; once per change is enough to be useful
    for (qsizetype i = 0; i < rules.regexRules.size(); ++i)
        if (!rules.regexRules[i].find.isEmpty() && !rules.compiled[i])
            context->logWarning(QStringLiteral("Invalid regex: %1").arg(rules.regexRules[i].find));
}

void TextReplacePlugin::onBeforeSend(OutgoingMessage &message)
{
    // Replacing text in channels used for sharing/requesting rules may be messy
    if (exemptChannelIds().contains(message.channelId))
        return;
    message.content = applyRules(message.content, Scope::MyMessages, rules);
}

void TextReplacePlugin::transformIncomingContent(IncomingMessage &message)
{
    if (message.content.isEmpty() || message.isOwn || !message.authorId)
        return;
    message.content = applyRules(message.content, Scope::OthersMessages, rules);
}

QByteArray qord_manifest_textreplace();

void qord_register_textreplace(Registry &registry)
{
    registry.add({ QStringLiteral("textreplace/plugin.json"),
                   [] { return std::make_unique<TextReplacePlugin>(); }, qord_manifest_textreplace() });
}

} // namespace Plugins
} // namespace Qord
