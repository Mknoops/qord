/*
 * Qord, a native Discord client
 * Port of Equicord's Translate plugin (src/plugins/translate)
 * Original: Copyright (c) Vendicated and contributors, authors Ven, AshtonMemer, koish1
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "Translate.hpp"

#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include <utility>

namespace Qord {
namespace Plugins {

namespace {
const QString MenuId = QStringLiteral("vc-trans");
const QString ButtonId = QStringLiteral("vc-trans-button");
const QString DismissId = QStringLiteral("dismiss");
const QStringList LanguageKeys = { QStringLiteral("receivedInput"), QStringLiteral("receivedOutput"),
                                   QStringLiteral("sentInput"), QStringLiteral("sentOutput") };
const QString AutoTranslateDescription =
        QStringLiteral("Automatically translate your messages before sending. You can also Shift+click or "
                       "right-click the translate button to toggle this");
const QString GenericError = QStringLiteral(
        "Something went wrong. If this issue persists, please check the logs or ask for help in the support server.");

// Upstream's TranslateIcon
const QByteArray Icon = QByteArrayLiteral(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 96 960 960\"><path fill=\"currentColor\" "
        "d=\"m475 976 181-480h82l186 480h-87l-41-126H604l-47 126h-82Zm151-196h142l-70-194h-2l-70 194Zm-466 76-55-55 "
        "204-204q-38-44-67.5-88.5T190 416h87q17 33 37.5 62.5T361 539q45-47 75-97.5T487 336H40v-80h280v-80h80v80h280v80H"
        "567q-22 69-58.5 135.5T419 598l98 99-30 81-127-122-200 200Z\"/></svg>");

// What can be translated: the text, a forwarded message's text, or an AutoMod alert's
QString messageContent(const MessageContext &message)
{
    if (!message.content.isEmpty())
        return message.content;
    if (!message.snapshotContent.isEmpty())
        return message.snapshotContent;
    const QJsonObject json = QJsonDocument::fromJson(message.rawJson.toUtf8()).object();
    for (const auto &embed : json.value(QLatin1String("embeds")).toArray()) {
        const QJsonObject o = embed.toObject();
        if (o.value(QLatin1String("type")).toString() == QLatin1String("auto_moderation_message"))
            return o.value(QLatin1String("description")).toString();
    }
    return {};
}

QString languageName(const QList<TranslatePlugin::Language> &list, const QString &code)
{
    for (const auto &language : list)
        if (language.code.compare(code, Qt::CaseInsensitive) == 0)
            return language.name;
    return code;
}
} // namespace

QString TranslatePlugin::deeplToGoogle(const QString &language)
{
    if (language.isEmpty())
        return QStringLiteral("auto");
    if (language == QLatin1String("nb"))
        return QStringLiteral("no");
    if (language == QLatin1String("zh-hans"))
        return QStringLiteral("zh-CN");
    if (language == QLatin1String("zh-hant"))
        return QStringLiteral("zh-TW");
    if (language == QLatin1String("en-us") || language == QLatin1String("en-gb"))
        return QStringLiteral("en");
    if (language == QLatin1String("pt-br") || language == QLatin1String("pt-pt"))
        return QStringLiteral("pt");
    return language;
}

void TranslatePlugin::start(PluginContext &ctx)
{
    context = &ctx;
    alive = std::make_shared<bool>(true);
}

void TranslatePlugin::stop()
{
    if (alive)
        *alive = false;
    // The replies won't come any more: let waiting sends go back to the box
    const auto pending = std::exchange(pendingSends, {});
    for (const auto &done : pending)
        done(std::nullopt);
    translations.clear(); // the client re-renders messages when the plugin stops
    context = nullptr;
}

void TranslatePlugin::onSettingChanged(const QString &key)
{
    if (key == QLatin1String("service"))
        resetLanguageDefaults();
    else if (key == QLatin1String("autoTranslate") && context)
        context->refreshChatBar();
}

const QList<TranslatePlugin::Language> &TranslatePlugin::languages() const
{
    const QString service = context ? context->settings().stringValue(QStringLiteral("service")) : QString();
    if (service == QLatin1String("google"))
        return googleLanguages();
    if (service == QLatin1String("kagi"))
        return kagiLanguages();
    return deeplLanguages();
}

void TranslatePlugin::resetLanguageDefaults()
{
    if (!context)
        return;
    PluginSettings &settings = context->settings();
    const QString service = settings.stringValue(QStringLiteral("service"));
    const bool google = service == QLatin1String("google") || service == QLatin1String("kagi");
    settings.setValue(QStringLiteral("receivedInput"), google ? QStringLiteral("auto") : QString());
    settings.setValue(QStringLiteral("receivedOutput"), google ? QStringLiteral("en") : QStringLiteral("en-us"));
    settings.setValue(QStringLiteral("sentInput"), google ? QStringLiteral("auto") : QString());
    settings.setValue(QStringLiteral("sentOutput"), google ? QStringLiteral("en") : QStringLiteral("en-us"));
}

// --- Translating -------------------------------------------------------------

void TranslatePlugin::translate(const QString &kind, const QString &text, Done done)
{
    if (!context || !context->network()) {
        done(std::nullopt);
        return;
    }
    PluginSettings &settings = context->settings();
    const QString from = settings.stringValue(kind + QStringLiteral("Input"));
    const QString to = settings.stringValue(kind + QStringLiteral("Output"));
    const QString service = settings.stringValue(QStringLiteral("service"));
    if (service == QLatin1String("google"))
        googleTranslate(text, from, to, std::move(done));
    else if (service == QLatin1String("kagi"))
        kagiTranslate(text, from, to, std::move(done));
    else
        deeplTranslate(text, from, to, std::move(done));
}

void TranslatePlugin::fail(const QString &message, const Done &done)
{
    if (context && context->ui())
        context->ui()->showToast(message, UiService::ToastType::Failure);
    done(std::nullopt);
}

void TranslatePlugin::googleTranslate(const QString &text, const QString &from, const QString &to, Done done)
{
    // Every value percent-encoded (URLSearchParams would use + for spaces)
    const QList<QPair<QString, QString>> params = {
        { QStringLiteral("params.client"), QStringLiteral("gtx") },
        { QStringLiteral("dataTypes"), QStringLiteral("TRANSLATION") },
        { QStringLiteral("key"), QStringLiteral("AIzaSyDLEeFI5OtFBwYBIoK_jj5m32rZK5CkCXA") }, // upstream's public key
        { QStringLiteral("query.sourceLanguage"), from },
        { QStringLiteral("query.targetLanguage"), to },
        { QStringLiteral("query.text"), text },
    };
    QStringList parts;
    for (const auto &param : params)
        parts.append(param.first + QLatin1Char('=') + QString::fromLatin1(QUrl::toPercentEncoding(param.second)));
    QUrl url(QStringLiteral("https://translate-pa.googleapis.com/v1/translate"));
    url.setQuery(parts.join(QLatin1Char('&')), QUrl::StrictMode);

    std::weak_ptr<bool> weak = alive;
    context->network()->get(url, [this, weak, done = std::move(done)](const NetworkService::Reply &reply) {
        if (auto a = weak.lock(); !a || !*a)
            return;
        const QJsonObject json = QJsonDocument::fromJson(reply.body).object();
        if (!reply.ok || !json.value(QLatin1String("translation")).isString()) {
            if (context)
                context->logInfo(QStringLiteral("Google Translate failed: %1 %2").arg(reply.status).arg(reply.error));
            fail(GenericError, done);
            return;
        }
        const QString source = json.value(QLatin1String("sourceLanguage")).toString();
        done(Translation { languageName(googleLanguages(), source), json.value(QLatin1String("translation")).toString() });
    });
}

void TranslatePlugin::deeplTranslate(const QString &text, const QString &from, const QString &to, Done done)
{
    PluginSettings &settings = context->settings();
    const QString key = settings.stringValue(QStringLiteral("deeplApiKey"));
    if (key.isEmpty()) {
        if (context->ui())
            context->ui()->showToast(QStringLiteral("DeepL API key is not set. Resetting to Google"),
                                     UiService::ToastType::Failure);
        settings.setValue(QStringLiteral("service"), QStringLiteral("google")); // resets the languages
        googleTranslate(text, deeplToGoogle(from), deeplToGoogle(to), std::move(done));
        return;
    }

    const bool pro = settings.stringValue(QStringLiteral("service")) == QLatin1String("deepl-pro");
    const QUrl url(pro ? QStringLiteral("https://api.deepl.com/v2/translate")
                       : QStringLiteral("https://api-free.deepl.com/v2/translate"));
    QJsonObject payload;
    payload.insert(QStringLiteral("text"), QJsonArray { text });
    payload.insert(QStringLiteral("target_lang"), to);
    payload.insert(QStringLiteral("source_lang"), from.section(QLatin1Char('-'), 0, 0));
    const NetworkService::Headers headers = { { "Content-Type", "application/json" },
                                              { "Authorization", "DeepL-Auth-Key " + key.toUtf8() } };

    std::weak_ptr<bool> weak = alive;
    context->network()->post(
            url, headers, QJsonDocument(payload).toJson(QJsonDocument::Compact),
            [this, weak, text, from, to, done = std::move(done)](const NetworkService::Reply &reply) {
                if (auto a = weak.lock(); !a || !*a)
                    return;
                switch (reply.status) {
                case 200:
                    break;
                case 0:
                    fail(QStringLiteral("Failed to connect to DeepL API: ") + reply.error, done);
                    return;
                case 403:
                    fail(QStringLiteral("Invalid DeepL API key or version"), done);
                    return;
                case 456:
                    if (!quotaToastShown && context && context->ui()) {
                        quotaToastShown = true;
                        context->ui()->showToast(QStringLiteral("Deepl API quota exceeded. Falling back to Google Translate"),
                                                 UiService::ToastType::Failure);
                    }
                    googleTranslate(text, deeplToGoogle(from), deeplToGoogle(to), done);
                    return;
                default:
                    if (context)
                        context->logInfo(QStringLiteral("DeepL failed: %1").arg(reply.status));
                    fail(GenericError, done);
                    return;
                }
                const QJsonObject first = QJsonDocument::fromJson(reply.body)
                                                  .object()
                                                  .value(QLatin1String("translations"))
                                                  .toArray()
                                                  .at(0)
                                                  .toObject();
                if (!first.value(QLatin1String("text")).isString()) {
                    fail(GenericError, done);
                    return;
                }
                const QString source = first.value(QLatin1String("detected_source_language")).toString();
                done(Translation { languageName(deeplLanguages(), source), first.value(QLatin1String("text")).toString() });
            });
}

void TranslatePlugin::kagiTranslate(const QString &text, const QString &from, const QString &to, Done done)
{
    const QString session = context->settings().stringValue(QStringLiteral("kagiSession"));
    QJsonObject payload;
    payload.insert(QStringLiteral("text"), text);
    payload.insert(QStringLiteral("from"), from);
    payload.insert(QStringLiteral("to"), to);
    payload.insert(QStringLiteral("model"), QStringLiteral("standard"));
    const NetworkService::Headers headers = { { "Content-Type", "application/json" },
                                              { "Cookie", "kagi_session=" + session.toUtf8() } };

    std::weak_ptr<bool> weak = alive;
    context->network()->post(QUrl(QStringLiteral("https://translate.kagi.com/api/translate")), headers,
                             QJsonDocument(payload).toJson(QJsonDocument::Compact),
                             [this, weak, done = std::move(done)](const NetworkService::Reply &reply) {
                                 if (auto a = weak.lock(); !a || !*a)
                                     return;
                                 if (reply.status == 401) {
                                     fail(QStringLiteral("Invalid or expired Kagi session token"), done);
                                     return;
                                 }
                                 const QJsonObject json = QJsonDocument::fromJson(reply.body).object();
                                 if (reply.status != 200 || !json.value(QLatin1String("translation")).isString()) {
                                     if (context)
                                         context->logInfo(QStringLiteral("Kagi failed: %1").arg(reply.status));
                                     fail(GenericError, done);
                                     return;
                                 }
                                 done(Translation { json.value(QLatin1String("detected_language"))
                                                            .toObject()
                                                            .value(QLatin1String("label"))
                                                            .toString(),
                                                    json.value(QLatin1String("translation")).toString() });
                             });
}

// --- Messages ----------------------------------------------------------------

QList<MenuItem> TranslatePlugin::messageContextMenuItems(const MessageContext &message)
{
    if (messageContent(message).isEmpty())
        return {};
    return { { MenuId, QStringLiteral("Translate"), {} } };
}

void TranslatePlugin::onMessageContextMenuAction(const QString &id, const MessageContext &message)
{
    if (id != MenuId)
        return;
    const QString content = messageContent(message);
    if (content.isEmpty())
        return;
    translate(QStringLiteral("received"), content, [this, message](std::optional<Translation> translation) {
        if (!translation || !context)
            return;
        translations.insert(message.messageId, *translation);
        if (MessageService *messages = context->messages())
            messages->refreshMessage(message.accountId, message.channelId, message.messageId);
    });
}

// Upstream's button on the hover bar, for messages with something to translate
QList<ChatBarButton> TranslatePlugin::messagePopoverButtons(const MessageContext &message)
{
    if (messageContent(message).isEmpty())
        return {};
    return { { MenuId, QStringLiteral("Translate"), Icon } };
}

void TranslatePlugin::onMessagePopoverClicked(const QString &id, const MessageContext &message, bool rightClick)
{
    if (!rightClick)
        onMessageContextMenuAction(id, message);
}

std::optional<MessageAccessory> TranslatePlugin::messageAccessory(const MessageContext &message)
{
    const auto it = translations.constFind(message.messageId);
    if (it == translations.constEnd())
        return std::nullopt;
    MessageAccessory accessory;
    accessory.iconSvg = Icon;
    accessory.content = it->text;
    accessory.footer = QStringLiteral("translated from ") + it->sourceLanguage;
    accessory.links = { { DismissId, QStringLiteral("Dismiss"), {} } };
    return accessory;
}

void TranslatePlugin::onMessageAccessoryAction(const QString &id, const MessageContext &message)
{
    if (id != DismissId || !translations.remove(message.messageId) || !context)
        return;
    if (MessageService *messages = context->messages())
        messages->refreshMessage(message.accountId, message.channelId, message.messageId);
}

// --- Chat bar and sending ----------------------------------------------------

QList<ChatBarButton> TranslatePlugin::chatBarButtons(const ChatContext &chat)
{
    Q_UNUSED(chat);
    if (!context)
        return {};
    ChatBarButton button;
    button.id = ButtonId;
    button.tooltip = QStringLiteral("Open Translate Modal");
    button.svg = Icon;
    button.active = context->settings().boolValue(QStringLiteral("autoTranslate"));
    if (tooltipUntil.isValid() && QDateTime::currentDateTimeUtc() < tooltipUntil &&
        context->settings().boolValue(QStringLiteral("showAutoTranslateTooltip")))
        button.popupTooltip = QStringLiteral("Auto Translate Enabled");
    return { button };
}

void TranslatePlugin::onChatBarButtonClicked(const QString &id, const ChatContext &chat)
{
    if (id != ButtonId)
        return;
    if (chat.click == ChatContext::Click::Right || (chat.click == ChatContext::Click::Left && chat.shift))
        toggleAutoTranslate();
    else if (chat.click == ChatContext::Click::Left)
        openTranslateModal();
}

// The settings page's "Customize translation languages & Auto-Translate"
void TranslatePlugin::onSettingAction(const QString &key)
{
    if (key == QLatin1String("manageTranslateSettings") && context)
        openTranslateModal();
}

void TranslatePlugin::toggleAutoTranslate()
{
    PluginSettings &settings = context->settings();
    const bool on = !settings.boolValue(QStringLiteral("autoTranslate"));
    settings.setValue(QStringLiteral("autoTranslate"), on);
    UiService *ui = context->ui();
    if (!on || settings.boolValue(QStringLiteral("dismissedAutoTranslateAlert")) || !ui)
        return;
    UiService::Confirmation confirmation;
    confirmation.title = QStringLiteral("Qord Auto-Translate Enabled");
    confirmation.text = QStringLiteral(
            "You just enabled Auto Translate! Any message will automatically be translated before being sent.");
    confirmation.confirmText = QStringLiteral("Disable Auto-Translate");
    confirmation.cancelText = QStringLiteral("Got it");
    confirmation.checkboxText = QStringLiteral("Don't show again");
    const bool disable = ui->confirm(confirmation);
    if (!context)
        return;
    context->settings().setValue(QStringLiteral("dismissedAutoTranslateAlert"), confirmation.checkboxChecked);
    if (disable)
        context->settings().setValue(QStringLiteral("autoTranslate"), false);
}

void TranslatePlugin::openTranslateModal()
{
    UiService *ui = context->ui();
    if (!ui)
        return;
    PluginSettings &settings = context->settings();
    QList<UiService::FormField> fields;
    for (const QString &key : LanguageKeys) {
        UiService::FormField field;
        field.key = key;
        field.label = settings.spec(key) ? settings.spec(key)->description : key;
        // Only the input languages can be detected
        const bool includeAuto = key.endsWith(QLatin1String("Input"));
        const QList<Language> &list = languages();
        for (qsizetype i = includeAuto ? 0 : 1; i < list.size(); ++i)
            field.choices.append({ list[i].code, list[i].name });
        field.value = settings.stringValue(key);
        fields.append(field);
    }
    UiService::FormField autoTranslate;
    autoTranslate.key = QStringLiteral("autoTranslate");
    autoTranslate.label = QStringLiteral("Auto Translate");
    autoTranslate.description = AutoTranslateDescription;
    autoTranslate.value = settings.boolValue(autoTranslate.key) ? QStringLiteral("true") : QStringLiteral("false");
    fields.append(autoTranslate);

    std::weak_ptr<bool> weak = alive;
    ui->showForm(QStringLiteral("Translate"), fields, [this, weak](const QString &key, const QString &value) {
        if (auto a = weak.lock(); !a || !*a || !context)
            return;
        if (key == QLatin1String("autoTranslate"))
            context->settings().setValue(key, value == QLatin1String("true"));
        else if (LanguageKeys.contains(key))
            context->settings().setValue(key, value);
    });
}

void TranslatePlugin::onBeforeSend(OutgoingMessage &message)
{
    if (!context || message.isEdit || message.content.isEmpty() ||
        !context->settings().boolValue(QStringLiteral("autoTranslate")))
        return;

    // "Auto Translate Enabled" over the button for two seconds
    tooltipUntil = QDateTime::currentDateTimeUtc().addMSecs(TooltipMs);
    context->refreshChatBar();
    std::weak_ptr<bool> weak = alive;
    QTimer::singleShot(TooltipMs + 50, [this, weak] {
        if (auto a = weak.lock(); a && *a && context)
            context->refreshChatBar();
    });

    message.deferred = [this, weak](const QString &content, OutgoingMessage::DeferredDone done) {
        if (auto a = weak.lock(); !a || !*a) {
            done(std::nullopt);
            return;
        }
        const quint64 send = ++nextSend;
        pendingSends.insert(send, done);
        translate(QStringLiteral("sent"), content, [this, weak, send, content](std::optional<Translation> translation) {
            if (auto a = weak.lock(); !a || !*a)
                return;
            // As upstream, a failed translation is sent as it was written
            if (auto finish = pendingSends.take(send))
                finish(translation ? translation->text : content);
        });
    };
}

QByteArray qord_manifest_translate();

void qord_register_translate(Registry &registry)
{
    registry.add({ QStringLiteral("translate/plugin.json"), [] { return std::make_unique<TranslatePlugin>(); },
                   qord_manifest_translate() });
}

} // namespace Plugins
} // namespace Qord
