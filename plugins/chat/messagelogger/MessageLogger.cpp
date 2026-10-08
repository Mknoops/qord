/*
 * Qord, a native Discord client
 * Port of Equicord's MessageLogger plugin (src/plugins/messageLogger)
 * Original: Copyright (c) 2024 Vendicated and contributors,
 * authors rushii, Ven, AutumnVN, Nickyux, Kyuuhachi, sadan, justjxke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "MessageLogger.hpp"

#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"
#include "Plugins/TextDiff.hpp"

#include <QLocale>
#include <QRegularExpression>

namespace Qord {
namespace Plugins {

namespace {
constexpr quint64 EphemeralFlag = 64;
constexpr Id VenbotUserId = 1017176847865352332ull;
constexpr Id VencordSupportCategoryId = 1108135649699180705ull;
constexpr Id EquibotUserId = 1243063117852835941ull;
constexpr Id EquicordSupportChannelId = 1297590739911573585ull;

const QString ToggleHighlight = QStringLiteral("ml-toggle-style");
const QString ToggleDiff = QStringLiteral("ml-toggle-diff");
const QString RemoveHistory = QStringLiteral("ml-remove-history");
const QString ViewHistory = QStringLiteral("ml-view-history");

QSet<Id> parseIds(const QString &text)
{
    static const QRegularExpression separators(QStringLiteral("[,\\s]+"));
    QSet<Id> ids;
    for (const QString &part : text.split(separators, Qt::SkipEmptyParts)) {
        bool ok = false;
        const Id id = part.toULongLong(&ok);
        if (ok && id)
            ids.insert(id);
    }
    return ids;
}

QList<DiffPart> onlyKinds(const QList<DiffPart> &parts, DiffPart::Kind dropped)
{
    QList<DiffPart> out;
    for (const auto &p : parts)
        if (p.kind != dropped)
            out.append(p);
    return out;
}
} // namespace

namespace MessageLogger {

bool shouldIgnore(const Config &c, const MessageContext &m, bool isEdit)
{
    return (c.ignoreBots && m.authorIsBot) || (c.ignoreSelf && m.isOwn) || (c.ignoreSelfEdits && isEdit && m.isOwn) ||
           c.ignoreUsers.contains(m.authorId) || c.ignoreChannels.contains(m.channelId) ||
           (m.parentChannelId && c.ignoreChannels.contains(m.parentChannelId)) ||
           (isEdit ? !c.logEdits : !c.logDeletes) || (m.guildId && c.ignoreGuilds.contains(m.guildId)) ||
           // Upstream leaves the support bots alone in the support channels
           (m.authorId == VenbotUserId && m.parentChannelId == VencordSupportCategoryId) ||
           (m.authorId == EquibotUserId && m.channelId == EquicordSupportChannelId);
}

MessageAnnotation annotate(const Config &config, const Record &record, const QString &current,
                           bool diffDisabled, bool highlightDisabled)
{
    MessageAnnotation a;
    a.deleted = record.deleted && !highlightDisabled;
    a.deletedStyle = config.deleteStyle;
    if (!config.inlineEdits || record.history.isEmpty())
        return a;

    const bool diffs = config.showEditDiffs && !diffDisabled;
    if (diffs && config.separatedDiffs) {
        // Aggregated view: the first version with what was removed since,
        // and the current content with what was added since
        const auto &first = record.history.first();
        const QList<DiffPart> diff = createWordDiff(first.content, current);
        MessageAnnotation::Edit e = first;
        e.diff = onlyKinds(diff, DiffPart::Kind::Added);
        a.history = { e };
        a.separatedDiffs = true;
        a.currentDiff = onlyKinds(diff, DiffPart::Kind::Removed);
        return a;
    }

    for (qsizetype i = 0; i < record.history.size(); ++i) {
        MessageAnnotation::Edit e = record.history.at(i);
        const QString next = i == record.history.size() - 1 ? current : record.history.at(i + 1).content;
        if (diffs && next != e.content)
            e.diff = createWordDiff(e.content, next);
        a.history.append(e);
    }
    return a;
}

} // namespace MessageLogger

using namespace MessageLogger;

void MessageLoggerPlugin::start(PluginContext &ctx)
{
    context = &ctx;
    reload();
}

void MessageLoggerPlugin::stop()
{
    // Deleted messages we were keeping go away with the plugin
    if (MessageService *messages = context->messages()) {
        for (auto it = records.cbegin(); it != records.cend(); ++it) {
            const auto [account, channel] = where.value(it.key());
            if (it->deleted)
                messages->removeMessage(account, channel, it.key());
            else
                messages->refreshMessage(account, channel, it.key());
        }
    }
    records.clear();
    where.clear();
    order.clear();
    context = nullptr;
}

void MessageLoggerPlugin::onSettingChanged(const QString &key)
{
    Q_UNUSED(key);
    reload();
    refreshAll();
}

void MessageLoggerPlugin::reload()
{
    PluginSettings &s = context->settings();
    config.logDeletes = s.boolValue(QStringLiteral("logDeletes"));
    config.logEdits = s.boolValue(QStringLiteral("logEdits"));
    config.ignoreBots = s.boolValue(QStringLiteral("ignoreBots"));
    config.ignoreSelf = s.boolValue(QStringLiteral("ignoreSelf"));
    config.ignoreSelfEdits = s.boolValue(QStringLiteral("ignoreSelfEdits"));
    config.ignoreUsers = parseIds(s.stringValue(QStringLiteral("ignoreUsers")));
    config.ignoreChannels = parseIds(s.stringValue(QStringLiteral("ignoreChannels")));
    config.ignoreGuilds = parseIds(s.stringValue(QStringLiteral("ignoreGuilds")));
    config.inlineEdits = s.boolValue(QStringLiteral("inlineEdits"));
    config.showEditDiffs = s.boolValue(QStringLiteral("showEditDiffs"));
    // Upstream: turning diffs off also turns separated diffs off
    config.separatedDiffs = config.showEditDiffs && s.boolValue(QStringLiteral("separatedDiffs"));
    config.deleteStyle = s.stringValue(QStringLiteral("deleteStyle")) == QLatin1String("overlay")
            ? MessageAnnotation::DeletedStyle::Overlay
            : MessageAnnotation::DeletedStyle::Text;
}

void MessageLoggerPlugin::refreshAll()
{
    if (MessageService *messages = context->messages())
        for (auto it = where.cbegin(); it != where.cend(); ++it)
            messages->refreshMessage(it->first, it->second, it.key());
}

Record &MessageLoggerPlugin::recordFor(const MessageContext &m)
{
    auto it = records.find(m.messageId);
    if (it != records.end())
        return *it;
    while (int(order.size()) >= MaxRecords) {
        const Id oldest = order.front();
        order.pop_front();
        records.remove(oldest);
        where.remove(oldest);
    }
    order.push_back(m.messageId);
    where.insert(m.messageId, { m.accountId, m.channelId });
    return records[m.messageId];
}

const Record *MessageLoggerPlugin::record(Id messageId) const
{
    auto it = records.constFind(messageId);
    return it == records.constEnd() ? nullptr : &*it;
}

bool MessageLoggerPlugin::keepDeletedMessage(const MessageContext &m)
{
    if ((m.flags & EphemeralFlag) || shouldIgnore(config, m, false))
        return false;
    recordFor(m).deleted = true;
    return true;
}

void MessageLoggerPlugin::onMessageEdited(const MessageContext &before, const MessageContext &after)
{
    if ((after.flags & EphemeralFlag) || shouldIgnore(config, after, true))
        return;
    if (!after.editedTimestamp.isValid() || after.content == before.content)
        return;
    recordFor(after).history.append({ after.editedTimestamp, before.content, {} });
}

std::optional<MessageAnnotation> MessageLoggerPlugin::annotateMessage(const MessageContext &m)
{
    const Record *r = record(m.messageId);
    if (!r || (!r->deleted && r->history.isEmpty()))
        return std::nullopt;
    return annotate(config, *r, m.content, diffDisabled.contains(m.messageId), highlightDisabled.contains(m.messageId));
}

QList<MenuItem> MessageLoggerPlugin::messageContextMenuItems(const MessageContext &m)
{
    const Record *r = record(m.messageId);
    if (!r || (!r->deleted && r->history.isEmpty()))
        return {};
    QList<MenuItem> items;
    if (r->deleted)
        items.append({ ToggleHighlight, QStringLiteral("Toggle Deleted Highlight"), {} });
    if (!r->history.isEmpty())
        items.append({ ViewHistory, QStringLiteral("View Edit History"), {} });
    if (config.showEditDiffs)
        items.append({ ToggleDiff,
                       diffDisabled.contains(m.messageId) ? QStringLiteral("Enable Diff View")
                                                          : QStringLiteral("Disable Diff View"),
                       {} });
    items.append({ RemoveHistory, QStringLiteral("Remove Message History"), {} });
    return items;
}

void MessageLoggerPlugin::onMessageContextMenuAction(const QString &id, const MessageContext &m)
{
    auto it = records.find(m.messageId);
    if (it == records.end())
        return;
    MessageService *messages = context->messages();

    if (id == ToggleHighlight) {
        if (!highlightDisabled.remove(m.messageId))
            highlightDisabled.insert(m.messageId);
    } else if (id == ToggleDiff) {
        if (!diffDisabled.remove(m.messageId))
            diffDisabled.insert(m.messageId);
    } else if (id == ViewHistory) {
        // Stands in for upstream's history modal (opened from "(edited)")
        if (UiService *ui = context->ui()) {
            QList<UiService::TextSection> sections;
            const QLocale locale;
            for (const auto &edit : std::as_const(it->history))
                sections.append({ QStringLiteral("Edited %1").arg(locale.toString(edit.timestamp.toLocalTime(), QLocale::ShortFormat)),
                                  edit.content, false });
            sections.append({ QStringLiteral("Current"), m.content, false });
            ui->showTextDialog(QStringLiteral("Message Edit History"), sections);
        }
        return;
    } else if (id == RemoveHistory) {
        const bool deleted = it->deleted;
        records.erase(it);
        where.remove(m.messageId);
        if (messages) {
            if (deleted)
                messages->removeMessage(m.accountId, m.channelId, m.messageId);
            else
                messages->refreshMessage(m.accountId, m.channelId, m.messageId);
        }
        return;
    } else {
        return;
    }
    if (messages)
        messages->refreshMessage(m.accountId, m.channelId, m.messageId);
}

QList<MenuItem> MessageLoggerPlugin::channelContextMenuItems(const ChannelContext &channel)
{
    if (!channel.channelId)
        return {};
    for (auto it = where.cbegin(); it != where.cend(); ++it)
        if (it.value().second == channel.channelId && records.contains(it.key()))
            return { { QStringLiteral("vc-ml-clear-channel"), QStringLiteral("Clear Message Log"), {} } };
    return {};
}

void MessageLoggerPlugin::onChannelContextMenuAction(const QString &id, const ChannelContext &channel)
{
    if (id != QLatin1String("vc-ml-clear-channel") || !channel.channelId)
        return;
    MessageService *messages = context->messages();
    QList<Id> cleared;
    for (auto it = where.cbegin(); it != where.cend(); ++it)
        if (it.value().second == channel.channelId)
            cleared.append(it.key());
    for (Id messageId : cleared) {
        const auto [accountId, channelId] = where.take(messageId);
        const auto rec = records.find(messageId);
        if (rec == records.end())
            continue;
        const bool deleted = rec->deleted;
        records.erase(rec);
        // Like upstream's clearMessageHistory: deleted ones leave, edited ones lose their history
        if (messages) {
            if (deleted)
                messages->removeMessage(accountId, channelId, messageId);
            else
                messages->refreshMessage(accountId, channelId, messageId);
        }
    }
}

QByteArray qord_manifest_messagelogger();

void qord_register_messagelogger(Registry &registry)
{
    registry.add({ QStringLiteral("messagelogger/plugin.json"),
                   [] { return std::make_unique<MessageLoggerPlugin>(); }, qord_manifest_messagelogger() });
}

} // namespace Plugins
} // namespace Qord
