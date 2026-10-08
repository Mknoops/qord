/*
 * Qord, a native Discord client
 * Port of Equicord's MessageLogger plugin (src/plugins/messageLogger)
 * Original: Copyright (c) 2024 Vendicated and contributors,
 * authors rushii, Ven, AutumnVN, Nickyux, Kyuuhachi, sadan, justjxke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "Plugins/Plugin.hpp"

#include <QHash>
#include <QSet>

#include <deque>

namespace Qord {
namespace Plugins {

class PluginContext;

namespace MessageLogger {

struct Config
{
    bool logDeletes = true;
    bool logEdits = true;
    bool ignoreBots = true;
    bool ignoreSelf = false;
    bool ignoreSelfEdits = false;
    QSet<Id> ignoreUsers;
    QSet<Id> ignoreChannels;
    QSet<Id> ignoreGuilds;
    bool inlineEdits = true;
    bool showEditDiffs = false;
    bool separatedDiffs = false;
    MessageAnnotation::DeletedStyle deleteStyle = MessageAnnotation::DeletedStyle::Text;
};

// upstream shouldIgnore(): settings, ephemeral messages are handled by the caller
bool shouldIgnore(const Config &config, const MessageContext &message, bool isEdit);

struct Record
{
    bool deleted = false;
    QList<MessageAnnotation::Edit> history; // content before each edit, oldest first
};

// Builds what the client renders for a logged message (inline history,
// optional diffs, deleted style).
MessageAnnotation annotate(const Config &config, const Record &record, const QString &currentContent,
                           bool diffDisabledForMessage, bool highlightDisabledForMessage);

} // namespace MessageLogger

// Keeps deleted messages on screen (in red) and shows edit history, for as
// long as the client keeps the message in memory.
class MessageLoggerPlugin : public Plugin
{
public:
    static constexpr int MaxRecords = 5000;

    [[nodiscard]] Hooks hooks() const override
    {
        return Hook::MessageHistory | Hook::MessageContextMenu | Hook::ChannelContextMenu;
    }
    void start(PluginContext &context) override;
    void stop() override;
    void onSettingChanged(const QString &key) override;

    bool keepDeletedMessage(const MessageContext &message) override;
    void onMessageEdited(const MessageContext &before, const MessageContext &after) override;
    std::optional<MessageAnnotation> annotateMessage(const MessageContext &message) override;

    QList<MenuItem> messageContextMenuItems(const MessageContext &message) override;
    void onMessageContextMenuAction(const QString &id, const MessageContext &message) override;
    // "Clear Message Log" on a channel with logged messages
    QList<MenuItem> channelContextMenuItems(const ChannelContext &channel) override;
    void onChannelContextMenuAction(const QString &id, const ChannelContext &channel) override;

    [[nodiscard]] const MessageLogger::Record *record(Id messageId) const;

private:
    void reload();
    MessageLogger::Record &recordFor(const MessageContext &message);
    void refreshAll();

    PluginContext *context = nullptr;
    MessageLogger::Config config;
    QHash<Id, MessageLogger::Record> records;
    QHash<Id, std::pair<Id, Id>> where; // message -> (account, channel), for refreshes
    std::deque<Id> order; // oldest first, to bound memory
    QSet<Id> diffDisabled;
    QSet<Id> highlightDisabled;
};

} // namespace Plugins
} // namespace Qord
