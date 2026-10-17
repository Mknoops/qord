/*
 * Qord, a native Discord client
 * Port of Equicord's MessageClickActions plugin (src/plugins/messageClickActions)
 * Original: Copyright (c) Vendicated and contributors, authors Ven, keircn, ZcraftElite, omaw
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "MessageClickActions.hpp"

#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

#include <QRegularExpression>
#include <QUrl>

namespace Qord {
namespace Plugins {

namespace {
constexpr int MaxAdditionalReactEmojis = 8; // MAX_ADDITIONAL_REACT_EMOJIS (ReactEmojiSetting.tsx)
constexpr int AdditionalReactionDelayMs = 300; // ADDITIONAL_REACTION_DELAY_MS
constexpr quint64 EphemeralFlag = 1u << 6;

// Message types Discord lets you reply to (MessageTypeSets.REPLYABLE is only
// available at runtime; these are the types its docs list as replyable)
bool replyableType(int type)
{
    switch (type) {
    case 0: // DEFAULT
    case 7: // USER_JOIN
    case 8: // GUILD_BOOST
    case 9:
    case 10:
    case 11:
    case 12: // CHANNEL_FOLLOW_ADD
    case 19: // REPLY
    case 20: // CHAT_INPUT_COMMAND
    case 23: // CONTEXT_MENU_COMMAND
        return true;
    default:
        return false;
    }
}

bool canReply(const MessageContext &m)
{
    return replyableType(m.type) && !(m.flags & EphemeralFlag);
}

QString messageLink(const MessageContext &m)
{
    const QString guild = m.guildId ? QString::number(m.guildId) : QStringLiteral("@me");
    return QStringLiteral("https://discord.com/channels/%1/%2/%3").arg(guild).arg(m.channelId).arg(m.messageId);
}
} // namespace

QString MessageClickActions::normalizeEmoji(const QString &emoji)
{
    const QString trimmed = emoji.trimmed();
    if (trimmed.isEmpty())
        return {};
    static const QRegularExpression custom(QStringLiteral("^(?:<(?:(a):)?|:)?([\\w-]+?)(?:~\\d+)?:([0-9]+)>?$"));
    const auto m = custom.match(trimmed);
    if (m.hasMatch())
        return m.captured(2) + QLatin1Char(':') + m.captured(3);
    return trimmed;
}

QStringList MessageClickActions::configuredReactions(const QString &reactEmoji, bool addAdditional,
                                                     const QString &additional)
{
    QStringList configured;
    const QString base = normalizeEmoji(reactEmoji);
    if (!base.isEmpty())
        configured.append(base);
    if (addAdditional) {
        static const QRegularExpression split(QStringLiteral("[\\n,]"));
        int taken = 0;
        for (const QString &part : additional.split(split)) {
            if (taken >= MaxAdditionalReactEmojis)
                break;
            const QString e = normalizeEmoji(part);
            if (e.isEmpty())
                continue;
            ++taken;
            if (!configured.contains(e))
                configured.append(e);
        }
    }
    return configured;
}

QString MessageClickActions::quote(const QString &content)
{
    QStringList lines = content.split(QLatin1Char('\n'));
    for (QString &line : lines)
        line.prepend(QStringLiteral("> "));
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

MessageClickActionsPlugin::MessageClickActionsPlugin() = default;
MessageClickActionsPlugin::~MessageClickActionsPlugin() = default;

void MessageClickActionsPlugin::stop()
{
    clearClickTimeouts();
    reactionDelay.reset();
    resetClickState();
    context = nullptr;
}

int MessageClickActionsPlugin::setting(const char *key) const
{
    return int(context->settings().intValue(QString::fromLatin1(key)));
}

QString MessageClickActionsPlugin::text(const char *key) const
{
    return context->settings().stringValue(QString::fromLatin1(key));
}

MessageClickActionsPlugin::Modifier MessageClickActionsPlugin::modifierFrom(const QString &value)
{
    if (value == QLatin1String("SHIFT"))
        return Modifier::Shift;
    if (value == QLatin1String("CTRL"))
        return Modifier::Ctrl;
    if (value == QLatin1String("ALT"))
        return Modifier::Alt;
    if (value == QLatin1String("BACKSPACE"))
        return Modifier::Backspace;
    if (value == QLatin1String("DELETE"))
        return Modifier::Delete;
    return Modifier::None;
}

// isModifierPressed(): NONE means nothing is held
bool MessageClickActionsPlugin::isModifierPressed(Modifier modifier, const MessageClickContext &c)
{
    switch (modifier) {
    case Modifier::None:
        return !c.shift && !c.ctrl && !c.alt && !c.backspace && !c.deleteKey;
    case Modifier::Shift:
        return c.shift;
    case Modifier::Ctrl:
        return c.ctrl;
    case Modifier::Alt:
        return c.alt;
    case Modifier::Backspace:
        return c.backspace;
    case Modifier::Delete:
        return c.deleteKey;
    }
    return false;
}

void MessageClickActionsPlugin::resetClickState()
{
    mouseDownCount = 0;
    doubleClickDetected = false;
    secondMouseDownTime = 0;
}

void MessageClickActionsPlugin::clearClickTimeouts()
{
    singleClickTimeout.reset();
    doubleClickTimeout.reset();
}

void MessageClickActionsPlugin::onChatMouseDown(qint64 now)
{
    if (!context)
        return;
    if (mouseDownCount >= 1 && now - lastMouseDownTime < setting("clickTimeout")) {
        singleClickTimeout.reset();
        doubleClickDetected = true;
        secondMouseDownTime = now;
    }
    ++mouseDownCount;
    lastMouseDownTime = now;
}

void MessageClickActionsPlugin::onMessageClicked(const MessageClickContext &click)
{
    if (!context)
        return;
    auto &s = context->settings();
    const MessageContext &msg = click.message;
    const bool isMe = msg.isOwn;
    if ((s.boolValue(QStringLiteral("disableInDms")) && msg.isDm) ||
        (s.boolValue(QStringLiteral("disableInSystemDms")) && click.isSystemDm))
        return;

    const QString singleClickAction = text(isMe ? "singleClickAction" : "singleClickOthersAction");
    const QString doubleClickAction = text(isMe ? "doubleClickAction" : "doubleClickOthersAction");
    const QString tripleClickAction = text("tripleClickAction");
    const Modifier singleClickModifier = modifierFrom(text(isMe ? "singleClickModifier" : "singleClickOthersModifier"));
    const Modifier doubleClickModifier = modifierFrom(text("doubleClickModifier"));
    const Modifier tripleClickModifier = modifierFrom(text("tripleClickModifier"));

    const bool isSingleClick = click.detail == 1 && click.primaryButton;
    const bool isDoubleClick = click.detail == 2;
    const bool isTripleClick = click.detail == 3;

    // Held too long: the user is selecting text
    if (click.timeMs - lastMouseDownTime > setting("selectionHoldTimeout")) {
        resetClickState();
        return;
    }
    singleClickTimeout.reset();

    const bool defer = s.boolValue(QStringLiteral("deferDoubleClickForTriple"));
    if (isTripleClick) {
        if (!defer) {
            resetClickState();
            return;
        }
        doubleClickTimeout.reset();
        if (isModifierPressed(tripleClickModifier, click) && tripleClickAction != QLatin1String("NONE"))
            executeAction(tripleClickAction, click, tripleClickModifier);
        doubleClickFired = false;
        resetClickState();
        return;
    }

    const bool canDoubleClick =
            (isModifierPressed(doubleClickModifier, click) || doubleClickModifier == Modifier::None) &&
            doubleClickAction != QLatin1String("NONE");
    const bool canTripleClick =
            defer && isModifierPressed(tripleClickModifier, click) && tripleClickAction != QLatin1String("NONE");
    const bool shouldDeferDoubleClick = canDoubleClick && canTripleClick && doubleClickModifier == tripleClickModifier;

    if (isDoubleClick) {
        doubleClickFired = true;
        const bool isQuickDoubleClick =
                !doubleClickDetected || (click.timeMs - secondMouseDownTime < setting("doubleClickHoldThreshold"));
        const auto executeDoubleClick = [this, click, canDoubleClick, isQuickDoubleClick, doubleClickAction,
                                         doubleClickModifier]() {
            if (!click.canSend || click.message.deleted)
                return;
            if (canDoubleClick && isQuickDoubleClick)
                executeAction(doubleClickAction, click, doubleClickModifier);
        };
        if (shouldDeferDoubleClick) {
            doubleClickTimeout = std::make_unique<QTimer>();
            doubleClickTimeout->setSingleShot(true);
            QObject::connect(doubleClickTimeout.get(), &QTimer::timeout, executeDoubleClick);
            doubleClickTimeout->start(setting("clickTimeout"));
        } else {
            executeDoubleClick();
        }
        resetClickState();
        return;
    }

    if (isSingleClick) {
        doubleClickFired = false;
        const auto executeSingleClick = [this, click, singleClickModifier, singleClickAction]() {
            if (!doubleClickFired && !doubleClickDetected && isModifierPressed(singleClickModifier, click) &&
                singleClickAction != QLatin1String("NONE"))
                executeAction(singleClickAction, click, singleClickModifier);
            resetClickState();
        };
        const bool canDoubleClickWithCurrentModifier =
                doubleClickAction != QLatin1String("NONE") &&
                (doubleClickModifier == Modifier::None || isModifierPressed(doubleClickModifier, click));
        if (canDoubleClickWithCurrentModifier && singleClickModifier == Modifier::None) {
            singleClickTimeout = std::make_unique<QTimer>();
            singleClickTimeout->setSingleShot(true);
            QObject::connect(singleClickTimeout.get(), &QTimer::timeout, executeSingleClick);
            singleClickTimeout->start(setting("clickTimeout"));
        } else {
            executeSingleClick();
        }
    }
}

void MessageClickActionsPlugin::executeAction(const QString &action, const MessageClickContext &click, Modifier trigger)
{
    if (!context)
        return;
    const MessageContext &msg = click.message;
    UiService *ui = context->ui();
    MessageActionService *messages = context->messageActions();
    ClipboardService *clipboard = context->clipboard();

    // Copies with a toast saying so, as upstream's copyWithToast
    const auto copy = [clipboard, ui](const QString &text, const QString &toast) {
        if (!clipboard)
            return;
        clipboard->setText(text);
        if (ui)
            ui->showToast(toast, UiService::ToastType::Success);
    };
    const auto warn = [ui](const QString &text) {
        if (ui)
            ui->showToast(text, UiService::ToastType::Failure);
    };

    if (action == QLatin1String("DELETE")) {
        if (!msg.isOwn && !click.canManageMessages)
            return;
        // A message MessageLogger kept after deletion is only taken off the
        // screen (upstream's mlDeleted)
        if (messages)
            messages->deleteMessage(msg.accountId, msg.channelId, msg.messageId);
    } else if (action == QLatin1String("COPY_LINK")) {
        copy(messageLink(msg), QStringLiteral("Link copied!"));
    } else if (action == QLatin1String("COPY_ID")) {
        copy(QString::number(msg.messageId), QStringLiteral("Message ID copied!"));
    } else if (action == QLatin1String("COPY_CONTENT")) {
        copy(msg.content, QStringLiteral("Message content copied!"));
    } else if (action == QLatin1String("COPY_USER_ID")) {
        copy(QString::number(msg.authorId), QStringLiteral("User ID copied!"));
    } else if (action == QLatin1String("EDIT")) {
        if (msg.isOwn && ui)
            ui->startEditing(msg);
    } else if (action == QLatin1String("REPLY")) {
        if (!canReply(msg) || !click.canSend || !ui)
            return;
        ui->startReply(msg, click.shift && trigger != Modifier::Shift, std::nullopt);
    } else if (action == QLatin1String("EDIT_REPLY")) {
        if (!ui)
            return;
        if (msg.isOwn)
            ui->startEditing(msg);
        else if (canReply(msg) && click.canSend)
            ui->startReply(msg, false, std::nullopt);
    } else if (action == QLatin1String("QUOTE")) {
        if (!canReply(msg)) {
            warn(QStringLiteral("Cannot quote this message type"));
            return;
        }
        if (!ui)
            return;
        QString content = msg.content;
        if (context->settings().boolValue(QStringLiteral("useSelectionForQuote"))) {
            const QString selection = click.selectedText.trimmed();
            if (!selection.isEmpty() && msg.content.contains(selection))
                content = selection;
        }
        if (content.isEmpty())
            return;
        ui->insertIntoMessageBox(MessageClickActions::quote(content));
        if (context->settings().boolValue(QStringLiteral("quoteWithReply")))
            ui->startReply(msg, false, false);
    } else if (action == QLatin1String("PIN")) {
        if (!click.canManageMessages) {
            warn(QStringLiteral("Cannot pin: Missing permissions"));
            return;
        }
        if (!messages)
            return;
        messages->setPinned(msg.accountId, msg.channelId, msg.messageId, !click.pinned);
    } else if (action == QLatin1String("REACT")) {
        if (msg.guildId && !click.canReact) {
            warn(QStringLiteral("Cannot react: Missing permissions"));
            return;
        }
        if (!messages)
            return;
        const QStringList emojis = MessageClickActions::configuredReactions(
                text("reactEmoji"), context->settings().boolValue(QStringLiteral("addAdditionalReacts")),
                text("additionalReactEmojis"));
        if (emojis.isEmpty())
            return;
        // Toggle the first one, then add the others a little apart (Discord rate limits reactions)
        messages->setReaction(msg.accountId, msg.channelId, msg.messageId, emojis.first(),
                              !click.myReactions.contains(emojis.first()));
        pendingReactions = emojis.mid(1);
        pendingReactionMessage = msg;
        if (pendingReactions.isEmpty())
            return;
        reactionDelay = std::make_unique<QTimer>();
        reactionDelay->setInterval(AdditionalReactionDelayMs);
        QObject::connect(reactionDelay.get(), &QTimer::timeout, [this]() {
            if (!context || pendingReactions.isEmpty() || !context->messageActions()) {
                reactionDelay->stop();
                return;
            }
            const QString emoji = pendingReactions.takeFirst();
            context->messageActions()->setReaction(pendingReactionMessage.accountId, pendingReactionMessage.channelId,
                                                   pendingReactionMessage.messageId, emoji, true);
            if (pendingReactions.isEmpty())
                reactionDelay->stop();
        });
        reactionDelay->start();
    } else if (action == QLatin1String("OPEN_TAB")) {
        if (ui)
            ui->openUrl(QUrl(messageLink(msg)));
    } else if (action == QLatin1String("OPEN_THREAD")) {
        if (ui)
            ui->openThread(msg);
    }
}

QByteArray qord_manifest_messageclickactions();

void qord_register_messageclickactions(Registry &registry)
{
    registry.add({ QStringLiteral("messageclickactions/plugin.json"),
                   [] { return std::make_unique<MessageClickActionsPlugin>(); }, qord_manifest_messageclickactions() });
}

} // namespace Plugins
} // namespace Qord
