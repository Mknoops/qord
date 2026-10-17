#include "PluginTestUtils.hpp"

#include <QTest>

namespace Qord::Plugins {
void qord_register_decodebase64(Registry &registry);
QStringList decodeBase64Strings(const QString &content);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

class TestDecodeBase64 : public QObject
{
    Q_OBJECT

    static MessageContext message(const QString &content)
    {
        MessageContext m;
        m.accountId = 1;
        m.channelId = 2;
        m.messageId = 3;
        m.content = content;
        return m;
    }

    static void act(SinglePluginHost &h, const QString &itemId, const QString &content)
    {
        const auto items = h.host->dispatchMessageContextMenuItems(message(content));
        h.host->dispatchMessageContextMenuAction(items[0].pluginId, itemId, message(content));
    }

private slots:
    void popoverDecodesAndCopies()
    {
        SinglePluginHost h(&qord_register_decodebase64);
        QVERIFY(h.enableWithAllPermissions());
        MessageContext m;
        m.content = QStringLiteral("aGVsbG8gd29ybGQ=");
        const auto buttons = h.host->dispatchMessagePopoverButtons(m);
        QCOMPARE(buttons.size(), 1);
        QCOMPARE(buttons[0].button.tooltip, QStringLiteral("Decode Base64 (Left Click) / Copy Decoded (Right Click)"));
        h.host->dispatchMessagePopoverClicked(h.id, buttons[0].button.id, m, false);
        QCOMPARE(h.ui.dialogTitle, QStringLiteral("Decoded Base64 Content"));
        h.host->dispatchMessagePopoverClicked(h.id, buttons[0].button.id, m, true);
        QCOMPARE(h.clipboard.text, QStringLiteral("hello world"));
        // Click method Right swaps them
        h.host->settings(h.id)->setValue(QStringLiteral("clickMethod"), QStringLiteral("Right"));
        h.clipboard.text.clear();
        h.host->dispatchMessagePopoverClicked(h.id, buttons[0].button.id, m, false);
        QCOMPARE(h.clipboard.text, QStringLiteral("hello world"));
    }

    void findsAndDecodes()
    {
        // "hello world", with and without padding, inside other text
        QCOMPARE(decodeBase64Strings(QStringLiteral("try aGVsbG8gd29ybGQ= now")), QStringList{ QStringLiteral("hello world") });
        QCOMPARE(decodeBase64Strings(QStringLiteral("aGVsbG8gd29ybGQ")), QStringList{ QStringLiteral("hello world") });
        // UTF-8 text decodes as text
        QCOMPARE(decodeBase64Strings(QStringLiteral("w6ljaGVj")), QStringList{ QStringLiteral("échec") });
        // Ordinary words aren't valid Base64 text
        QVERIFY(decodeBase64Strings(QStringLiteral("hello there test")).isEmpty());
        // Two of them
        QCOMPARE(decodeBase64Strings(QStringLiteral("Zmlyc3Q= and c2Vjb25k")),
                 (QStringList{ QStringLiteral("first"), QStringLiteral("second") }));
    }

    void menuOrderFollowsClickMethod()
    {
        SinglePluginHost h(&qord_register_decodebase64);
        QVERIFY(h.enableWithAllPermissions());
        auto items = h.host->dispatchMessageContextMenuItems(message(QStringLiteral("x")));
        QCOMPARE(items.size(), 2);
        QCOMPARE(items[0].item.label, QStringLiteral("Decode Base64"));
        h.host->settings(h.id)->setValue(QStringLiteral("clickMethod"), QStringLiteral("Right"));
        items = h.host->dispatchMessageContextMenuItems(message(QStringLiteral("x")));
        QCOMPARE(items[0].item.label, QStringLiteral("Copy Decoded"));
        QVERIFY(h.host->dispatchMessageContextMenuItems(message({})).isEmpty());
    }

    void decodeShowsAWindow()
    {
        SinglePluginHost h(&qord_register_decodebase64);
        QVERIFY(h.enableWithAllPermissions());
        act(h, QStringLiteral("decode"), QStringLiteral("Zmlyc3Q= and c2Vjb25k"));
        QCOMPARE(h.ui.dialogTitle, QStringLiteral("Decoded Base64 Content"));
        QCOMPARE(h.ui.dialogSections.size(), 2);
        QCOMPARE(h.ui.dialogSections[1].title, QStringLiteral("Decoded Content 2"));
        QCOMPARE(h.ui.dialogSections[1].text, QStringLiteral("second"));
        QVERIFY(h.ui.dialogSections[1].monospace);
    }

    void copyAndNothingFound()
    {
        SinglePluginHost h(&qord_register_decodebase64);
        QVERIFY(h.enableWithAllPermissions());
        act(h, QStringLiteral("copy"), QStringLiteral("Zmlyc3Q= and c2Vjb25k"));
        QCOMPARE(h.clipboard.text, QStringLiteral("first\nsecond"));
        act(h, QStringLiteral("copy"), QStringLiteral("nothing here"));
        QCOMPARE(h.ui.toasts.last().first, QStringLiteral("No Base64 content found in this message."));
        QCOMPARE(h.clipboard.writes, 1);
    }
};

QTEST_GUILESS_MAIN(TestDecodeBase64)
#include "tst_decodebase64.moc"
