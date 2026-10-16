#include "PluginTestUtils.hpp"

#include "Translate.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QUrlQuery>

namespace Qord::Plugins {
void qord_register_translate(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

namespace {
MessageContext message(const QString &content, Id id = 30)
{
    MessageContext m;
    m.accountId = 1;
    m.channelId = 2;
    m.messageId = id;
    m.content = content;
    return m;
}

QByteArray google(const QString &translation, const QString &source)
{
    return QJsonDocument(QJsonObject { { "translation", translation }, { "sourceLanguage", source } }).toJson();
}

QString queryValue(const QUrl &url, const QString &key)
{
    return QUrlQuery(url).queryItemValue(key, QUrl::FullyDecoded);
}

void menuTranslate(SinglePluginHost &h, const MessageContext &m)
{
    h.host->dispatchMessageContextMenuAction(h.id, QStringLiteral("vc-trans"), m);
}

QByteArray header(const NetworkService::Headers &headers, const QByteArray &name)
{
    for (const auto &h : headers)
        if (h.first == name)
            return h.second;
    return {};
}

// Settings changes come back through the plugin's change callback
void set(SinglePluginHost &h, const QString &key, const QJsonValue &value)
{
    QCOMPARE(h.host->settings(h.id)->setValue(key, value), QString());
}
} // namespace

class TestTranslate : public QObject
{
    Q_OBJECT

private slots:
    void popoverAndSettingsButton()
    {
        SinglePluginHost h(qord_register_translate);
        QVERIFY(h.enableWithAllPermissions());
        MessageContext m;
        m.messageId = 3;
        m.content = QStringLiteral("hola");
        const auto buttons = h.host->dispatchMessagePopoverButtons(m);
        QCOMPARE(buttons.size(), 1);
        QCOMPARE(buttons[0].button.tooltip, QStringLiteral("Translate"));
        h.host->dispatchMessagePopoverClicked(h.id, buttons[0].button.id, m, false);
        QCOMPARE(h.network.urls.size(), 1); // asked the service
        m.content.clear();
        QVERIFY(h.host->dispatchMessagePopoverButtons(m).isEmpty());

        h.host->dispatchSettingAction(h.id, QStringLiteral("manageTranslateSettings"));
        QCOMPARE(h.ui.formTitle, QStringLiteral("Translate"));

        // The key fields follow the service
        const auto *spec = h.host->settings(h.id)->spec(QStringLiteral("deeplApiKey"));
        QVERIFY(spec);
        QCOMPARE(spec->showWhenKey, QStringLiteral("service"));
        QCOMPARE(spec->showWhenValues, (QStringList { QStringLiteral("deepl"), QStringLiteral("deepl-pro") }));
    }

    void languageTables()
    {
        QCOMPARE(TranslatePlugin::googleLanguages().size(), 134);
        QCOMPARE(TranslatePlugin::googleLanguages()[0].code, QStringLiteral("auto"));
        QCOMPARE(TranslatePlugin::deeplLanguages()[0].code, QString());
        QCOMPARE(TranslatePlugin::kagiLanguages()[0].name, QStringLiteral("Detect language"));
        QCOMPARE(TranslatePlugin::deeplToGoogle(QStringLiteral("zh-hans")), QStringLiteral("zh-CN"));
        QCOMPARE(TranslatePlugin::deeplToGoogle(QStringLiteral("en-gb")), QStringLiteral("en"));
        QCOMPARE(TranslatePlugin::deeplToGoogle(QString()), QStringLiteral("auto"));
        QCOMPARE(TranslatePlugin::deeplToGoogle(QStringLiteral("de")), QStringLiteral("de"));
    }

    void translatesAMessageUnderIt()
    {
        SinglePluginHost h(qord_register_translate);
        QVERIFY(h.info()->loadError.isEmpty());
        QVERIFY(h.enableWithAllPermissions());

        QVERIFY(h.host->dispatchMessageContextMenuItems(message(QString())).isEmpty());
        QCOMPARE(h.host->dispatchMessageContextMenuItems(message(QStringLiteral("hola"))).value(0).item.label,
                 QStringLiteral("Translate"));

        menuTranslate(h, message(QStringLiteral("hola & adiós amigo+")));
        QCOMPARE(h.network.urls.size(), 1);
        const QUrl url = h.network.urls[0];
        QCOMPARE(url.host(), QStringLiteral("translate-pa.googleapis.com"));
        QCOMPARE(queryValue(url, QStringLiteral("query.text")), QStringLiteral("hola & adiós amigo+"));
        QCOMPARE(queryValue(url, QStringLiteral("query.sourceLanguage")), QStringLiteral("auto"));
        QCOMPARE(queryValue(url, QStringLiteral("query.targetLanguage")), QStringLiteral("en"));
        QVERIFY(!h.host->dispatchMessageAccessories(message(QStringLiteral("hola"))).size());

        QVERIFY(h.network.respond(true, google(QStringLiteral("hello & goodbye **friend**"), QStringLiteral("es"))));
        QCOMPARE(h.messages.refreshed.size(), 1);
        const auto accessories = h.host->dispatchMessageAccessories(message(QStringLiteral("hola")));
        QCOMPARE(accessories.size(), 1);
        QCOMPARE(accessories[0].accessory.content, QStringLiteral("hello & goodbye **friend**"));
        QCOMPARE(accessories[0].accessory.footer, QStringLiteral("translated from Spanish"));
        QCOMPARE(accessories[0].accessory.links.value(0).label, QStringLiteral("Dismiss"));
        // Only on that message
        QVERIFY(h.host->dispatchMessageAccessories(message(QStringLiteral("x"), 31)).isEmpty());

        h.host->dispatchMessageAccessoryAction(h.id, QStringLiteral("dismiss"), message(QStringLiteral("hola")));
        QVERIFY(h.host->dispatchMessageAccessories(message(QStringLiteral("hola"))).isEmpty());
        QCOMPARE(h.messages.refreshed.size(), 2);
    }

    void forwardedContent()
    {
        SinglePluginHost h(qord_register_translate);
        QVERIFY(h.enableWithAllPermissions());
        MessageContext forwarded = message(QString());
        forwarded.snapshotContent = QStringLiteral("bonjour");
        QCOMPARE(h.host->dispatchMessageContextMenuItems(forwarded).size(), 1);
        menuTranslate(h, forwarded);
        QCOMPARE(queryValue(h.network.urls[0], QStringLiteral("query.text")), QStringLiteral("bonjour"));
    }

    void failuresToast()
    {
        SinglePluginHost h(qord_register_translate);
        QVERIFY(h.enableWithAllPermissions());
        menuTranslate(h, message(QStringLiteral("hola")));
        QVERIFY(h.network.respond(false, {}, 500));
        QCOMPARE(h.ui.toasts.size(), 1);
        QVERIFY(h.ui.toasts[0].first.startsWith(QStringLiteral("Something went wrong")));
        QVERIFY(h.host->dispatchMessageAccessories(message(QStringLiteral("hola"))).isEmpty());
    }

    void deepl()
    {
        SinglePluginHost h(qord_register_translate);
        QVERIFY(h.enableWithAllPermissions());
        set(h, QStringLiteral("service"), QStringLiteral("deepl"));
        // Switching services resets the languages like upstream
        PluginSettings *settings = h.host->settings(h.id);
        QCOMPARE(settings->stringValue(QStringLiteral("receivedInput")), QString());
        QCOMPARE(settings->stringValue(QStringLiteral("receivedOutput")), QStringLiteral("en-us"));

        set(h, QStringLiteral("deeplApiKey"), QStringLiteral("k3y:fx"));
        // Kept in the secret store, not the settings file
        QVERIFY(!h.storage.data.value(QStringLiteral("settings-translate")).contains("k3y"));
        QCOMPARE(settings->stringValue(QStringLiteral("deeplApiKey")), QStringLiteral("k3y:fx"));

        menuTranslate(h, message(QStringLiteral("hallo")));
        QCOMPARE(h.network.posts.size(), 1);
        const auto post = h.network.posts[0];
        QCOMPARE(post.url, QUrl(QStringLiteral("https://api-free.deepl.com/v2/translate")));
        QCOMPARE(header(post.headers, "Authorization"), QByteArray("DeepL-Auth-Key k3y:fx"));
        const QJsonObject body = QJsonDocument::fromJson(post.body).object();
        QCOMPARE(body.value("text").toArray().at(0).toString(), QStringLiteral("hallo"));
        QCOMPARE(body.value("target_lang").toString(), QStringLiteral("en-us"));
        QCOMPARE(body.value("source_lang").toString(), QString());
        const QJsonObject reply { { "translations", QJsonArray { QJsonObject {
                                                            { "detected_source_language", "DE" }, { "text", "hello" } } } } };
        QVERIFY(h.network.respond(true, QJsonDocument(reply).toJson()));
        QCOMPARE(h.host->dispatchMessageAccessories(message(QString()))[0].accessory.footer,
                 QStringLiteral("translated from German"));

        // Quota: one toast, then Google with the languages mapped
        set(h, QStringLiteral("service"), QStringLiteral("deepl-pro"));
        set(h, QStringLiteral("receivedOutput"), QStringLiteral("pt-br"));
        menuTranslate(h, message(QStringLiteral("hallo"), 40));
        QCOMPARE(h.network.posts.last().url.host(), QStringLiteral("api.deepl.com"));
        QVERIFY(h.network.respond(false, "quota", 456));
        QCOMPARE(h.ui.toasts.size(), 1);
        QCOMPARE(h.ui.toasts[0].first, QStringLiteral("Deepl API quota exceeded. Falling back to Google Translate"));
        QCOMPARE(queryValue(h.network.urls.last(), QStringLiteral("query.targetLanguage")), QStringLiteral("pt"));
        QCOMPARE(queryValue(h.network.urls.last(), QStringLiteral("query.sourceLanguage")), QStringLiteral("auto"));
        QVERIFY(h.network.respond(true, google(QStringLiteral("olá"), QStringLiteral("de"))));
        menuTranslate(h, message(QStringLiteral("hallo"), 41));
        QVERIFY(h.network.respond(false, "quota", 456));
        QCOMPARE(h.ui.toasts.size(), 1); // only once

        // A bad key
        QVERIFY(h.network.respond(true, google(QStringLiteral("x"), QStringLiteral("de"))));
        menuTranslate(h, message(QStringLiteral("hallo"), 42));
        QVERIFY(h.network.respond(false, {}, 403));
        QCOMPARE(h.ui.toasts.last().first, QStringLiteral("Invalid DeepL API key or version"));
    }

    void deeplWithoutKeyFallsBackToGoogle()
    {
        SinglePluginHost h(qord_register_translate);
        QVERIFY(h.enableWithAllPermissions());
        set(h, QStringLiteral("service"), QStringLiteral("deepl"));
        set(h, QStringLiteral("deeplApiKey"), QString());
        menuTranslate(h, message(QStringLiteral("hallo")));
        QCOMPARE(h.ui.toasts.value(0).first, QStringLiteral("DeepL API key is not set. Resetting to Google"));
        QCOMPARE(h.host->settings(h.id)->stringValue(QStringLiteral("service")), QStringLiteral("google"));
        QCOMPARE(h.host->settings(h.id)->stringValue(QStringLiteral("receivedOutput")), QStringLiteral("en"));
        QVERIFY(h.network.posts.isEmpty());
        QCOMPARE(queryValue(h.network.urls[0], QStringLiteral("query.targetLanguage")), QStringLiteral("en"));
    }

    void kagi()
    {
        SinglePluginHost h(qord_register_translate);
        QVERIFY(h.enableWithAllPermissions());
        set(h, QStringLiteral("service"), QStringLiteral("kagi"));
        set(h, QStringLiteral("kagiSession"), QStringLiteral("s3ss"));
        menuTranslate(h, message(QStringLiteral("ciao")));
        const auto post = h.network.posts[0];
        QCOMPARE(post.url, QUrl(QStringLiteral("https://translate.kagi.com/api/translate")));
        QCOMPARE(header(post.headers, "Cookie"), QByteArray("kagi_session=s3ss"));
        const QJsonObject body = QJsonDocument::fromJson(post.body).object();
        QCOMPARE(body.value("from").toString(), QStringLiteral("auto"));
        QCOMPARE(body.value("to").toString(), QStringLiteral("en"));
        QCOMPARE(body.value("model").toString(), QStringLiteral("standard"));
        const QJsonObject reply { { "translation", "hi" }, { "detected_language", QJsonObject { { "label", "Italian" } } } };
        QVERIFY(h.network.respond(true, QJsonDocument(reply).toJson()));
        QCOMPARE(h.host->dispatchMessageAccessories(message(QString()))[0].accessory.footer,
                 QStringLiteral("translated from Italian"));
        menuTranslate(h, message(QStringLiteral("ciao"), 31));
        QVERIFY(h.network.respond(false, {}, 401));
        QCOMPARE(h.ui.toasts.last().first, QStringLiteral("Invalid or expired Kagi session token"));
    }

    void chatBarButton()
    {
        SinglePluginHost h(qord_register_translate);
        QVERIFY(h.enableWithAllPermissions());
        ChatContext chat;
        auto button = h.host->dispatchChatBarButtons(chat).value(0).button;
        QCOMPARE(button.tooltip, QStringLiteral("Open Translate Modal"));
        QVERIFY(!button.active);

        // Click: the languages window, with the service's languages
        h.host->dispatchChatBarButtonClicked(h.id, button.id, chat);
        QCOMPARE(h.ui.formTitle, QStringLiteral("Translate"));
        QCOMPARE(h.ui.formFields.size(), 5);
        QCOMPARE(h.ui.formFields[0].label, QStringLiteral("Language incoming messages are translated from"));
        QCOMPARE(h.ui.formFields[0].choices.size(), 134);
        QCOMPARE(h.ui.formFields[1].choices.size(), 133); // no "Detect language" for outputs
        QCOMPARE(h.ui.formFields[1].value, QStringLiteral("en"));
        QVERIFY(h.ui.formFields[4].choices.isEmpty());
        h.ui.formChanged(QStringLiteral("sentOutput"), QStringLiteral("ja"));
        QCOMPARE(h.host->settings(h.id)->stringValue(QStringLiteral("sentOutput")), QStringLiteral("ja"));
        h.ui.formChanged(QStringLiteral("autoTranslate"), QStringLiteral("true"));
        QVERIFY(h.host->dispatchChatBarButtons(chat).value(0).button.active);
        QVERIFY(h.ui.confirmations.isEmpty()); // the window's switch doesn't ask
        h.ui.formChanged(QStringLiteral("autoTranslate"), QStringLiteral("false"));

        // Shift+click toggles, with the notice the first time
        chat.shift = true;
        h.ui.confirmAnswer = false; // "Got it"
        h.ui.checkboxAnswer = true;
        h.host->dispatchChatBarButtonClicked(h.id, button.id, chat);
        QVERIFY(h.host->settings(h.id)->boolValue(QStringLiteral("autoTranslate")));
        QCOMPARE(h.ui.confirmations.size(), 1);
        QCOMPARE(h.ui.confirmations[0].confirmText, QStringLiteral("Disable Auto-Translate"));
        // Right click toggles too; once dismissed, no notice
        chat.shift = false;
        chat.click = ChatContext::Click::Right;
        h.host->dispatchChatBarButtonClicked(h.id, button.id, chat);
        QVERIFY(!h.host->settings(h.id)->boolValue(QStringLiteral("autoTranslate")));
        h.host->dispatchChatBarButtonClicked(h.id, button.id, chat);
        QCOMPARE(h.ui.confirmations.size(), 1);
        QVERIFY(h.host->settings(h.id)->boolValue(QStringLiteral("autoTranslate")));
    }

    void noticeCanDisable()
    {
        SinglePluginHost h(qord_register_translate);
        QVERIFY(h.enableWithAllPermissions());
        ChatContext chat;
        chat.click = ChatContext::Click::Right;
        h.ui.confirmAnswer = true; // "Disable Auto-Translate"
        h.host->dispatchChatBarButtonClicked(h.id, QStringLiteral("vc-trans-button"), chat);
        QVERIFY(!h.host->settings(h.id)->boolValue(QStringLiteral("autoTranslate")));
        QVERIFY(!h.host->settings(h.id)->boolValue(QStringLiteral("dismissedAutoTranslateAlert")));
    }

    void autoTranslate()
    {
        SinglePluginHost h(qord_register_translate);
        QVERIFY(h.enableWithAllPermissions());
        OutgoingMessage out;
        out.content = QStringLiteral("hello");
        h.host->dispatchBeforeSend(out);
        QVERIFY(!out.deferred); // off by default

        set(h, QStringLiteral("autoTranslate"), true);
        set(h, QStringLiteral("sentOutput"), QStringLiteral("de"));
        h.host->dispatchBeforeSend(out);
        QVERIFY(out.deferred);
        QCOMPARE(h.host->dispatchChatBarButtons(ChatContext()).value(0).button.popupTooltip,
                 QStringLiteral("Auto Translate Enabled"));
        std::optional<QString> sent;
        int calls = 0;
        out.deferred(out.content, [&](std::optional<QString> content) {
            sent = content;
            ++calls;
        });
        QCOMPARE(queryValue(h.network.urls.last(), QStringLiteral("query.targetLanguage")), QStringLiteral("de"));
        QCOMPARE(calls, 0);
        QVERIFY(h.network.respond(true, google(QStringLiteral("hallo"), QStringLiteral("en"))));
        QCOMPARE(calls, 1);
        QCOMPARE(sent, std::optional<QString>(QStringLiteral("hallo")));

        // A failure sends the text as written, as upstream
        out.deferred(out.content, [&](std::optional<QString> content) {
            sent = content;
            ++calls;
        });
        QVERIFY(h.network.respond(false, {}, 500));
        QCOMPARE(calls, 2);
        QCOMPARE(sent, std::optional<QString>(out.content));

        // The tooltip goes away after two seconds
        QTRY_VERIFY_WITH_TIMEOUT(h.host->dispatchChatBarButtons(ChatContext()).value(0).button.popupTooltip.isEmpty(),
                                 TranslatePlugin::TooltipMs + 1000);

        // Edits and empty messages aren't translated
        OutgoingMessage edit;
        edit.content = QStringLiteral("x");
        edit.isEdit = true;
        h.host->dispatchBeforeEdit(edit);
        h.host->dispatchBeforeSend(edit);
        QVERIFY(!edit.deferred);
    }

    void stoppingReleasesWaitingSends()
    {
        SinglePluginHost h(qord_register_translate);
        QVERIFY(h.enableWithAllPermissions());
        set(h, QStringLiteral("autoTranslate"), true);
        OutgoingMessage out;
        out.content = QStringLiteral("hello");
        h.host->dispatchBeforeSend(out);
        int calls = 0;
        std::optional<QString> sent = QStringLiteral("unset");
        out.deferred(out.content, [&](std::optional<QString> content) {
            sent = content;
            ++calls;
        });
        QVERIFY(h.host->setEnabled(h.id, false));
        QCOMPARE(calls, 1);
        QVERIFY(!sent);
    }
};

QTEST_GUILESS_MAIN(TestTranslate)
#include "tst_translate.moc"
