#include "PluginTestUtils.hpp"

#include <QTest>

namespace Qord::Plugins {
void qord_register_unitconverter(Registry &registry);
QString convertUnits(const QString &message, bool toImperial);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

class TestUnitConverter : public QObject
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

private slots:
    void popoverConvertsWithIcon()
    {
        SinglePluginHost h(qord_register_unitconverter);
        QVERIFY(h.enableWithAllPermissions());
        const MessageContext m = message(QStringLiteral("it's 20°C"));
        const auto buttons = h.host->dispatchMessagePopoverButtons(m);
        QCOMPARE(buttons.size(), 1);
        QCOMPARE(buttons[0].button.tooltip, QStringLiteral("Convert Units"));
        h.host->dispatchMessagePopoverClicked(h.id, buttons[0].button.id, m, false);
        const auto accessories = h.host->dispatchMessageAccessories(m);
        QCOMPARE(accessories.size(), 1);
        QVERIFY(accessories[0].accessory.iconSvg.contains("currentColor"));
    }

    void metricToImperial()
    {
        QCOMPARE(convertUnits(QStringLiteral("it's 20°C"), true), QStringLiteral("it's 68.00°F"));
        QCOMPARE(convertUnits(QStringLiteral("-5c outside"), true), QStringLiteral("23.00°F outside"));
        QCOMPARE(convertUnits(QStringLiteral("30 cm"), true), QStringLiteral("11.81in"));
        QCOMPARE(convertUnits(QStringLiteral("I'm 1.75 m"), true), QStringLiteral("I'm 5ft8.88in"));
        QCOMPARE(convertUnits(QStringLiteral("10 km away"), true), QStringLiteral("6.22mi away"));
        QCOMPARE(convertUnits(QStringLiteral("doing 50 km/h"), true), QStringLiteral("doing 31.08mph"));
        QCOMPARE(convertUnits(QStringLiteral("500g of flour"), true), QStringLiteral("17.64oz(s) of flour"));
        QCOMPARE(convertUnits(QStringLiteral("2 kg"), true), QStringLiteral("4.41lb(s)"));
        QCOMPARE(convertUnits(QStringLiteral("nothing to convert"), true), QStringLiteral("nothing to convert"));
        // Not inside words (upstream converts these)
        QCOMPARE(convertUnits(QStringLiteral("aGVsbG8g and 5 great ideas"), true),
                 QStringLiteral("aGVsbG8g and 5 great ideas"));
        QCOMPARE(convertUnits(QStringLiteral("v2m 3m"), true), QStringLiteral("v2m 9ft10.08in"));
    }

    void imperialToMetric()
    {
        QCOMPARE(convertUnits(QStringLiteral("100F today"), false), QStringLiteral("37.78°C today"));
        QCOMPARE(convertUnits(QStringLiteral("6 feet"), false), QStringLiteral("1.83m"));
        QCOMPARE(convertUnits(QStringLiteral("12 inches"), false), QStringLiteral("30.48cm"));
        QCOMPARE(convertUnits(QStringLiteral("5 ft 11 in"), false), QStringLiteral("1.80m"));
        QCOMPARE(convertUnits(QStringLiteral("5'11\""), false), QStringLiteral("1.80m"));
        QCOMPARE(convertUnits(QStringLiteral("10 lbs"), false), QStringLiteral("4.54kg"));
        QCOMPARE(convertUnits(QStringLiteral("5 lb 3 oz"), false), QStringLiteral("2.35kg"));
        QCOMPARE(convertUnits(QStringLiteral("8 oz"), false), QStringLiteral("226.80g"));
        QCOMPARE(convertUnits(QStringLiteral("60 mph"), false), QStringLiteral("96.54km/h"));
    }

    void convertsUnderTheMessage()
    {
        SinglePluginHost h(&qord_register_unitconverter);
        QVERIFY(h.enableWithAllPermissions());
        const auto items = h.host->dispatchMessageContextMenuItems(message(QStringLiteral("it's 20°C")));
        QCOMPARE(items.size(), 1);
        QCOMPARE(items[0].item.label, QStringLiteral("Convert Units"));
        h.host->dispatchMessageContextMenuAction(items[0].pluginId, items[0].item.id, message(QStringLiteral("it's 20°C")));
        QCOMPARE(h.messages.refreshed.size(), 1);
        auto accessories = h.host->dispatchMessageAccessories(message(QStringLiteral("it's 20°C")));
        QCOMPARE(accessories.size(), 1);
        QCOMPARE(accessories[0].accessory.content, QStringLiteral("it's 68.00°F"));
        QCOMPARE(accessories[0].accessory.links[0].label, QStringLiteral("Dismiss"));

        // Metric users get imperial converted
        h.host->settings(h.id)->setValue(QStringLiteral("myUnits"), QStringLiteral("metric"));
        h.host->dispatchMessageContextMenuAction(items[0].pluginId, items[0].item.id, message(QStringLiteral("100F")));
        QCOMPARE(h.host->dispatchMessageAccessories(message(QStringLiteral("100F")))[0].accessory.content,
                 QStringLiteral("37.78°C"));

        h.host->dispatchMessageAccessoryAction(items[0].pluginId, QStringLiteral("dismiss"), message(QStringLiteral("x")));
        QVERIFY(h.host->dispatchMessageAccessories(message(QStringLiteral("x"))).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestUnitConverter)
#include "tst_unitconverter.moc"
