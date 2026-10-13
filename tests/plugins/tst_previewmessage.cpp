#include "PluginTestUtils.hpp"

#include <QTest>

namespace Qord::Plugins {
void qord_register_previewmessage(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

class TestPreviewMessage : public QObject
{
    Q_OBJECT

    static ChatContext chat(const QString &draft, bool files = false)
    {
        ChatContext c;
        c.accountId = 1;
        c.channelId = 2;
        c.draft = draft;
        c.hasAttachments = files;
        return c;
    }

private slots:
    void buttonOnlyWithSomethingToPreview()
    {
        SinglePluginHost h(&qord_register_previewmessage);
        QVERIFY(h.enableWithAllPermissions());
        QVERIFY(h.host->dispatchChatBarButtons(chat({})).isEmpty());
        const auto withText = h.host->dispatchChatBarButtons(chat(QStringLiteral("hi")));
        QCOMPARE(withText.size(), 1);
        QCOMPARE(withText[0].button.tooltip, QStringLiteral("Preview Message"));
        QVERIFY(!withText[0].button.svg.isEmpty());
        QCOMPARE(h.host->dispatchChatBarButtons(chat({}, true)).size(), 1);
        // No channel open: nothing
        ChatContext none = chat(QStringLiteral("hi"));
        none.channelId = 0;
        QVERIFY(h.host->dispatchChatBarButtons(none).isEmpty());
    }

    void showsTheDraftLocally()
    {
        SinglePluginHost h(&qord_register_previewmessage);
        QVERIFY(h.enableWithAllPermissions());
        const auto buttons = h.host->dispatchChatBarButtons(chat(QStringLiteral("**bold** plan")));
        h.host->dispatchChatBarButtonClicked(buttons[0].pluginId, buttons[0].button.id, chat(QStringLiteral("**bold** plan")));
        QCOMPARE(h.ui.localMessages.size(), 1);
        QCOMPARE(h.ui.localMessages[0].accountId, Id(1));
        QCOMPARE(h.ui.localMessages[0].channelId, Id(2));
        QCOMPARE(h.ui.localMessages[0].content, QStringLiteral("**bold** plan"));
        // Files alone: nothing to show yet
        h.host->dispatchChatBarButtonClicked(buttons[0].pluginId, buttons[0].button.id, chat({}, true));
        QCOMPARE(h.ui.localMessages.size(), 1);
    }
};

QTEST_GUILESS_MAIN(TestPreviewMessage)
#include "tst_previewmessage.moc"
