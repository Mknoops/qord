#include "PluginTestUtils.hpp"

#include <QTest>

namespace Qord::Plugins {
void qord_register_imagezoom(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

class TestImageZoom : public QObject
{
    Q_OBJECT

private slots:
    void lensFollowsSettings()
    {
        SinglePluginHost h(qord_register_imagezoom);
        QVERIFY(h.info()->loadError.isEmpty());
        QVERIFY(h.enableWithAllPermissions());
        QVERIFY(h.displayOptions.lens.has_value());
        QCOMPARE(h.displayOptions.lens->zoom, 2.0);
        QCOMPARE(h.displayOptions.lens->size, 100);
        QCOMPARE(h.displayOptions.lens->zoomSpeed, 0.5);
        QVERIFY(h.displayOptions.lens->invertScroll);

        // The wheel's values are saved...
        auto changed = h.displayOptions.lensChanged; // a copy, as the viewer calls it
        changed(3.5, 240);
        QCOMPARE(h.host->settings(h.id)->numberValue(QStringLiteral("zoom")), 3.5);
        QCOMPARE(h.displayOptions.lens->size, 240);
        // ...unless saving is off
        h.host->settings(h.id)->setValue(QStringLiteral("saveZoomValues"), false);
        changed = h.displayOptions.lensChanged;
        changed(9, 500);
        QCOMPARE(h.host->settings(h.id)->numberValue(QStringLiteral("zoom")), 3.5);

        QVERIFY(h.host->setEnabled(h.id, false));
        QVERIFY(!h.displayOptions.lens.has_value());
    }

    void imageMenu()
    {
        SinglePluginHost h(qord_register_imagezoom);
        QVERIFY(h.enableWithAllPermissions());
        MessageContext message;
        QVERIFY(h.host->dispatchMessageContextMenuItems(message).isEmpty());
        message.imageUrl = QStringLiteral("https://cdn.discordapp.com/attachments/1/2/a.png");
        auto items = h.host->dispatchMessageContextMenuItems(message);
        QCOMPARE(items.size(), 2);
        QCOMPARE(items[0].item.label, QStringLiteral("Square Lens"));
        QCOMPARE(items[0].item.checked, std::optional<bool>(false));
        h.host->dispatchMessageContextMenuAction(h.id, items[0].item.id, message);
        QVERIFY(h.displayOptions.lens->square);
        items = h.host->dispatchMessageContextMenuItems(message);
        QCOMPARE(items[0].item.checked, std::optional<bool>(true));
        h.host->dispatchMessageContextMenuAction(h.id, items[1].item.id, message);
        QVERIFY(h.displayOptions.lens->nearestNeighbour);
    }
};

QTEST_GUILESS_MAIN(TestImageZoom)
#include "tst_imagezoom.moc"
