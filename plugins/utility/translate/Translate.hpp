/*
 * Qord, a native Discord client
 * Port of Equicord's Translate plugin (src/plugins/translate)
 * Original: Copyright (c) Vendicated and contributors, authors Ven, AshtonMemer, koish1
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "Plugins/Plugin.hpp"

#include <QDateTime>
#include <QHash>

#include <functional>
#include <memory>
#include <optional>

namespace Qord {
namespace Plugins {

class PluginContext;

// Translates messages with Google Translate, DeepL or Kagi: from the message
// menu (shown under the message), and your own before they are sent.
class TranslatePlugin : public Plugin
{
public:
    static constexpr int TooltipMs = 2000; // "Auto Translate Enabled"

    struct Language
    {
        QString code;
        QString name;
    };
    static const QList<Language> &googleLanguages();
    static const QList<Language> &deeplLanguages();
    static const QList<Language> &kagiLanguages();
    // Upstream's deeplLanguageToGoogleLanguage
    static QString deeplToGoogle(const QString &language);

    struct Translation
    {
        QString sourceLanguage;
        QString text;
    };
    // A failure carries what to show (a toast)
    using Done = std::function<void(std::optional<Translation> translation)>;

    [[nodiscard]] Hooks hooks() const override
    {
        return Hook::MessageContextMenu | Hook::ChatBarButtons | Hook::BeforeSend | Hook::MessageAccessories |
               Hook::MessagePopover;
    }
    void start(PluginContext &context) override;
    void stop() override;
    void onSettingChanged(const QString &key) override;

    QList<MenuItem> messageContextMenuItems(const MessageContext &message) override;
    void onMessageContextMenuAction(const QString &id, const MessageContext &message) override;
    QList<ChatBarButton> messagePopoverButtons(const MessageContext &message) override;
    void onSettingAction(const QString &key) override;
    void onMessagePopoverClicked(const QString &id, const MessageContext &message, bool rightClick) override;
    std::optional<MessageAccessory> messageAccessory(const MessageContext &message) override;
    void onMessageAccessoryAction(const QString &id, const MessageContext &message) override;
    QList<ChatBarButton> chatBarButtons(const ChatContext &chat) override;
    void onChatBarButtonClicked(const QString &id, const ChatContext &chat) override;
    void onBeforeSend(OutgoingMessage &message) override;

    // "received" or "sent": which language settings to use
    void translate(const QString &kind, const QString &text, Done done);

private:
    [[nodiscard]] const QList<Language> &languages() const;
    void resetLanguageDefaults();
    void googleTranslate(const QString &text, const QString &from, const QString &to, Done done);
    void deeplTranslate(const QString &text, const QString &from, const QString &to, Done done);
    void kagiTranslate(const QString &text, const QString &from, const QString &to, Done done);
    void fail(const QString &message, const Done &done);
    void toggleAutoTranslate();
    void openTranslateModal();

    PluginContext *context = nullptr;
    QHash<Id, Translation> translations; // by message id, until dismissed
    QDateTime tooltipUntil;
    bool quotaToastShown = false; // upstream's onlyOnce
    // Sends waiting on a translation: given up on when the plugin stops
    QHash<quint64, OutgoingMessage::DeferredDone> pendingSends;
    quint64 nextSend = 0;
    std::shared_ptr<bool> alive;
};

} // namespace Plugins
} // namespace Qord
