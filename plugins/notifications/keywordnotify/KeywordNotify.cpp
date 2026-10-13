/*
 * Qord, a native Discord client
 * Port of Equicord's KeywordNotify plugin (src/equicordplugins/keywordNotify)
 * Original: Copyright (c) Vendicated and contributors, authors camila314, x3rt, benjas333
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "KeywordNotify.hpp"

#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"
#include "Plugins/Settings.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

namespace Qord {
namespace Plugins {

namespace {
const QString TabId = QStringLiteral("keywords");

QStringList strings(const QJsonValue &value)
{
    QStringList out;
    for (const QJsonValue &v : value.toArray())
        out.append(v.toString());
    return out;
}

bool listed(const QStringList &ids, const MessageContext &m)
{
    for (const QString &raw : ids) {
        const QString id = raw.trimmed();
        if (id.isEmpty())
            continue;
        const Id value = id.toULongLong();
        if (value && (value == m.channelId || value == m.authorId || (m.guildId && value == m.guildId)))
            return true;
    }
    return false;
}

bool regexMatches(const QString &text, const QRegularExpression &re)
{
    return !text.isEmpty() && re.match(text).hasMatch();
}
} // namespace

QList<KeywordNotifyPlugin::Entry> KeywordNotifyPlugin::entries(const PluginSettings &settings)
{
    QList<Entry> out;
    for (const QJsonValue &v : settings.value(QStringLiteral("keywordEntries")).toArray()) {
        const QJsonObject o = v.toObject();
        Entry e;
        e.regex = o.value(QStringLiteral("regex")).toString();
        e.ignoreCase = o.value(QStringLiteral("ignoreCase")).toBool();
        e.ignoreBots = o.value(QStringLiteral("ignoreBots")).toBool(true);
        e.whitelist = strings(o.value(QStringLiteral("whitelist")));
        e.blacklist = strings(o.value(QStringLiteral("blacklist")));
        e.whitelistFirst = o.value(QStringLiteral("listPriority")).toString() == QLatin1String("Whitelist");
        out.append(e);
    }
    return out;
}

void KeywordNotifyPlugin::storeEntries(PluginSettings &settings, const QList<Entry> &list)
{
    QJsonArray array;
    for (const Entry &e : list)
        array.append(QJsonObject{ { QStringLiteral("regex"), e.regex },
                                  { QStringLiteral("ignoreCase"), e.ignoreCase },
                                  { QStringLiteral("ignoreBots"), e.ignoreBots },
                                  { QStringLiteral("whitelist"), QJsonArray::fromStringList(e.whitelist) },
                                  { QStringLiteral("blacklist"), QJsonArray::fromStringList(e.blacklist) },
                                  { QStringLiteral("listPriority"), e.whitelistFirst ? QStringLiteral("Whitelist")
                                                                                     : QStringLiteral("BlackList") } });
    settings.setValue(QStringLiteral("keywordEntries"), array);
}

bool KeywordNotifyPlugin::matches(const QList<Entry> &list, const MessageContext &m)
{
    QJsonArray embeds;
    bool embedsRead = false;
    for (const Entry &e : list) {
        if (e.regex.isEmpty())
            continue;
        const bool inWhitelist = listed(e.whitelist, m);
        const bool inBlacklist = listed(e.blacklist, m);
        if (inWhitelist && inBlacklist) {
            if (!e.whitelistFirst)
                continue;
        } else {
            if (!e.whitelist.isEmpty() && !inWhitelist)
                continue;
            if (inBlacklist)
                continue;
        }
        // Bots are ignored unless the bot itself is whitelisted
        if (m.authorIsBot && e.ignoreBots
            && !std::any_of(e.whitelist.begin(), e.whitelist.end(),
                            [&](const QString &id) { return id.trimmed().toULongLong() == m.authorId; }))
            continue;
        const QRegularExpression re(e.regex, e.ignoreCase ? QRegularExpression::CaseInsensitiveOption
                                                          : QRegularExpression::NoPatternOption);
        if (!re.isValid())
            continue; // upstream's safeMatchesRegex: an invalid pattern matches nothing
        if (regexMatches(m.content, re))
            return true;
        if (!embedsRead) {
            embeds = QJsonDocument::fromJson(m.rawJson.toUtf8()).object().value(QStringLiteral("embeds")).toArray();
            embedsRead = true;
        }
        for (const QJsonValue &v : embeds) {
            const QJsonObject embed = v.toObject();
            if (regexMatches(embed.value(QStringLiteral("description")).toString(), re)
                || regexMatches(embed.value(QStringLiteral("title")).toString(), re))
                return true;
            for (const QJsonValue &f : embed.value(QStringLiteral("fields")).toArray())
                if (regexMatches(f.toObject().value(QStringLiteral("value")).toString(), re)
                    || regexMatches(f.toObject().value(QStringLiteral("name")).toString(), re))
                    return true;
        }
    }
    return false;
}

void KeywordNotifyPlugin::start(PluginContext &ctx)
{
    context = &ctx;
    cached = entries(ctx.settings());
}

void KeywordNotifyPlugin::stop()
{
    context = nullptr;
    cached.clear();
}

void KeywordNotifyPlugin::onSettingChanged(const QString &key)
{
    if (!context)
        return;
    if (key == QLatin1String("keywordEntries"))
        cached = entries(context->settings());
    else if (key == QLatin1String("amountToKeep")) {
        storeLog(loadLog()); // trims to the new amount
    }
}

bool KeywordNotifyPlugin::highlightMessage(const MessageContext &message)
{
    if (!context || message.isOwn || !matches(cached, message))
        return false;
    log(message);
    return true;
}

QJsonObject KeywordNotifyPlugin::loadLog() const
{
    return context->settings().value(QStringLiteral("log")).toObject();
}

void KeywordNotifyPlugin::storeLog(const QJsonObject &log)
{
    const int keep = qMax(0, int(context->settings().intValue(QStringLiteral("amountToKeep"))));
    QJsonObject trimmed;
    for (auto it = log.begin(); it != log.end(); ++it) {
        QJsonArray list = it->toArray();
        while (list.size() > keep)
            list.removeLast();
        trimmed.insert(it.key(), list);
    }
    context->settings().setValue(QStringLiteral("log"), trimmed);
    context->refreshInbox();
}

// Newest first, once each, at most "amount to keep" per account
void KeywordNotifyPlugin::log(const MessageContext &message)
{
    QJsonObject all = loadLog();
    const QString account = QString::number(message.accountId);
    QJsonArray list = all.value(account).toArray();
    const QString id = QString::number(message.messageId);
    int at = 0;
    for (int i = 0; i < list.size(); ++i) {
        const QJsonObject o = list[i].toObject();
        if (o.value(QStringLiteral("messageId")).toString() == id)
            return;
        if (o.value(QStringLiteral("messageId")).toString().toULongLong() > message.messageId)
            at = i + 1;
    }
    list.insert(at, QJsonObject{ { QStringLiteral("channelId"), QString::number(message.channelId) },
                                 { QStringLiteral("messageId"), id },
                                 { QStringLiteral("json"), message.rawJson } });
    all.insert(account, list);
    storeLog(all);
}

QList<InboxTab> KeywordNotifyPlugin::inboxTabs(Id accountId)
{
    if (!context)
        return {};
    InboxTab tab;
    tab.id = TabId;
    tab.label = QStringLiteral("Keywords");
    tab.emptyText = QStringLiteral("No messages matched your keywords yet.");
    for (const QJsonValue &v : loadLog().value(QString::number(accountId)).toArray()) {
        const QJsonObject o = v.toObject();
        tab.messages.append({ o.value(QStringLiteral("channelId")).toString().toULongLong(),
                              o.value(QStringLiteral("messageId")).toString().toULongLong(),
                              o.value(QStringLiteral("json")).toString() });
    }
    return { tab };
}

void KeywordNotifyPlugin::onInboxMessageRemoved(const QString &tabId, Id accountId, Id messageId)
{
    if (!context || tabId != TabId)
        return;
    QJsonObject all = loadLog();
    const QString account = QString::number(accountId);
    QJsonArray list = all.value(account).toArray();
    for (int i = 0; i < list.size(); ++i)
        if (list[i].toObject().value(QStringLiteral("messageId")).toString().toULongLong() == messageId) {
            list.removeAt(i);
            break;
        }
    all.insert(account, list);
    storeLog(all);
}

void KeywordNotifyPlugin::onInboxCleared(const QString &tabId, Id accountId)
{
    if (!context || tabId != TabId)
        return;
    QJsonObject all = loadLog();
    all.remove(QString::number(accountId));
    storeLog(all);
}

QByteArray qord_manifest_keywordnotify();

void qord_register_keywordnotify(Registry &registry)
{
    registry.add({ QStringLiteral("keywordnotify/plugin.json"), [] { return std::make_unique<KeywordNotifyPlugin>(); },
                   qord_manifest_keywordnotify() });
}

} // namespace Plugins
} // namespace Qord
