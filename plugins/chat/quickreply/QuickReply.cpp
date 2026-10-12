/*
 * Qord, a native Discord client
 * Port of Equicord's QuickReply plugin (src/plugins/quickReply)
 * Original: Copyright (c) Vendicated and contributors, authors fawn, Ven, pylix
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "QuickReply.hpp"

#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

#include <QSet>

namespace Qord {
namespace Plugins {

namespace {
constexpr quint64 Ephemeral = quint64(1) << 6;
}

bool QuickReplyPlugin::isReplyable(int type)
{
    static const QSet<int> types = { 0,  7,  8,  9,  10, 11, 19, 20, 22, 23, 24, 25, 26, 27,
                                     28, 29, 30, 31, 32, 40, 41, 42, 44, 46 };
    return types.contains(type);
}

bool QuickReplyPlugin::onShortcut(const ShortcutContext &shortcut)
{
    UiService *ui = context ? context->ui() : nullptr;
    if (!ui)
        return false;
    if (shortcut.guildId && !shortcut.canSend)
        return true; // the key is still Discord's no-op
    const bool up = shortcut.key == ShortcutContext::Key::Up;
    const bool reply = !shortcut.shift;

    const bool skipBlocked = context->settings().boolValue(QStringLiteral("ignoreBlockedAndIgnored"));
    const RelationshipDirectory *relationships = context->relationships();
    QList<ChatMessageInfo> messages;
    for (const ChatMessageInfo &m : shortcut.messages) {
        if (m.deleted)
            continue;
        if (!reply && m.authorId != shortcut.accountId)
            continue; // only your own messages can be edited
        if (!isReplyable(m.type) || (m.flags & Ephemeral))
            continue;
        if (skipBlocked && relationships &&
            (relationships->isBlocked(shortcut.accountId, m.authorId) ||
             relationships->isIgnored(shortcut.accountId, m.authorId)))
            continue;
        messages.append(m);
    }

    // From the current reply or edit, or the newest message
    const Id current = reply ? shortcut.replyingTo : shortcut.editing;
    const ChatMessageInfo *next = nullptr;
    int index = -1;
    for (int i = 0; i < messages.size(); ++i)
        if (messages[i].id == current)
            index = i;
    if (!current || index < 0) {
        if (!messages.isEmpty())
            next = &messages.last();
    } else {
        const int i = up ? index - 1 : index + 1;
        if (i >= 0 && i < messages.size())
            next = &messages[i];
    }

    if (!next) {
        if (reply)
            ui->cancelReply();
        else
            ui->cancelEditing();
        return true;
    }
    MessageContext message;
    message.accountId = shortcut.accountId;
    message.guildId = shortcut.guildId;
    message.channelId = shortcut.channelId;
    message.messageId = next->id;
    message.authorId = next->authorId;
    if (reply) {
        const QString mention = context->settings().stringValue(QStringLiteral("shouldMention"));
        std::optional<bool> ping; // NoReplyMention's choice, or Discord's
        if (mention == QLatin1String("enabled"))
            ping = true;
        else if (mention == QLatin1String("disabled"))
            ping = false;
        ui->startReply(message, false, ping);
        ui->focusMessageBox();
    } else {
        ui->startEditing(message);
    }
    ui->revealMessage(next->id);
    return true;
}

QByteArray qord_manifest_quickreply();

void qord_register_quickreply(Registry &registry)
{
    registry.add({ QStringLiteral("quickreply/plugin.json"), [] { return std::make_unique<QuickReplyPlugin>(); },
                   qord_manifest_quickreply() });
}

} // namespace Plugins
} // namespace Qord
