#include "PluginTestUtils.hpp"

#include <QTest>

namespace Qord::Plugins {
void qord_register_viewraw(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

class TestViewRaw : public QObject
{
    Q_OBJECT

    static MessageContext message()
    {
        MessageContext m;
        m.messageId = 5;
        m.content = "**hi** <@123>";
        m.rawJson = "{\n    \"id\": \"5\"\n}\n";
        return m;
    }

private slots:
    void viewAndCopy()
    {
        SinglePluginHost h(qord_register_viewraw);
        QVERIFY(h.info()->loadError.isEmpty());
        QVERIFY(h.enableWithAllPermissions());
        const auto items = h.host->dispatchMessageContextMenuItems(message());
        QCOMPARE(items.size(), 2);
        QCOMPARE(items[0].item.label, QStringLiteral("View Raw"));
        QCOMPARE(items[1].item.label, QStringLiteral("Copy Raw Content"));

        h.host->dispatchMessageContextMenuAction(h.id, items[0].item.id, message());
        QCOMPARE(h.ui.dialogTitle, QStringLiteral("Raw Message Data"));
        QCOMPARE(h.ui.dialogSections.size(), 2);
        QCOMPARE(h.ui.dialogSections[0].text, QStringLiteral("**hi** <@123>"));
        QCOMPARE(h.ui.dialogSections[1].text, message().rawJson);
        QVERIFY(h.ui.dialogSections[1].monospace);

        h.host->dispatchMessageContextMenuAction(h.id, items[1].item.id, message());
        QCOMPARE(h.clipboard.text, QStringLiteral("**hi** <@123>"));
    }

    void emptyContentShowsOnlyData()
    {
        SinglePluginHost h(qord_register_viewraw);
        QVERIFY(h.enableWithAllPermissions());
        MessageContext m = message();
        m.content.clear();
        h.host->dispatchMessageContextMenuAction(h.id, "vc-view-message-raw", m);
        QCOMPARE(h.ui.dialogSections.size(), 1);
    }

    void respectsPermissions()
    {
        SinglePluginHost h(qord_register_viewraw);
        h.host->setGranted(h.id, Permission::AddUiElements);
        QVERIFY(h.host->setEnabled(h.id, true));
        h.host->dispatchMessageContextMenuAction(h.id, "vc-copy-message-raw", message());
        QCOMPARE(h.clipboard.writes, 0);
    }
};

QTEST_GUILESS_MAIN(TestViewRaw)
#include "tst_viewraw.moc"
