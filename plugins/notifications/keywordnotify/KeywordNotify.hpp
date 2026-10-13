/*
 * Qord, a native Discord client
 * Port of Equicord's KeywordNotify plugin (src/equicordplugins/keywordNotify)
 * Original: Copyright (c) Vendicated and contributors, authors camila314, x3rt, benjas333
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "Plugins/Plugin.hpp"

#include <QJsonObject>
#include <QStringList>

#include <memory>

namespace Qord {
namespace Plugins {

class PluginContext;
class PluginSettings;

// Messages matching your keywords count as mentioning you, and are kept in
// a "Keywords" inbox tab
class KeywordNotifyPlugin : public Plugin
{
public:
    // One keyword entry, as upstream stores it
    struct Entry
    {
        QString regex;
        bool ignoreCase = false;
        bool ignoreBots = true;
        QStringList whitelist; // user, channel or server ids
        QStringList blacklist;
        bool whitelistFirst = false; // "List priority": who wins when an id is in both
    };
    static QList<Entry> entries(const PluginSettings &settings);
    static void storeEntries(PluginSettings &settings, const QList<Entry> &entries);
    // Upstream's applyKeywordEntries
    static bool matches(const QList<Entry> &entries, const MessageContext &message);

    [[nodiscard]] Hooks hooks() const override { return Hook::Highlights | Hook::InboxTabs; }
    void start(PluginContext &context) override;
    void stop() override;
    void onSettingChanged(const QString &key) override;

    bool highlightMessage(const MessageContext &message) override;
    QList<InboxTab> inboxTabs(Id accountId) override;
    void onInboxMessageRemoved(const QString &tabId, Id accountId, Id messageId) override;
    void onInboxCleared(const QString &tabId, Id accountId) override;

private:
    void log(const MessageContext &message);
    QJsonObject loadLog() const;
    void storeLog(const QJsonObject &log);

    PluginContext *context = nullptr;
    QList<Entry> cached;
};

} // namespace Plugins
} // namespace Qord
