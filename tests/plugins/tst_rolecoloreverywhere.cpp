#include "PluginTestUtils.hpp"

#include <QTest>

namespace Qord::Plugins {
void qord_register_rolecoloreverywhere(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

class TestRoleColorEverywhere : public QObject
{
    Q_OBJECT

private slots:
    void appliesSettings()
    {
        SinglePluginHost h(qord_register_rolecoloreverywhere);
        QVERIFY(h.info()->loadError.isEmpty());
        QVERIFY(h.enableWithAllPermissions());
        RoleColors colors = h.displayOptions.roleColors;
        QVERIFY(colors.mentions && colors.memberListHeaders && colors.voiceUsers);
        QVERIFY(!colors.messages);
        QCOMPARE(colors.messageSaturation, 30);

        h.host->settings(h.id)->setValue(QStringLiteral("colorChatMessages"), true);
        h.host->settings(h.id)->setValue(QStringLiteral("messageSaturation"), 60);
        h.host->settings(h.id)->setValue(QStringLiteral("voiceUsers"), false);
        colors = h.displayOptions.roleColors;
        QVERIFY(colors.messages);
        QCOMPARE(colors.messageSaturation, 60);
        QVERIFY(!colors.voiceUsers);

        QVERIFY(h.host->setEnabled(h.id, false));
        colors = h.displayOptions.roleColors;
        QVERIFY(!colors.mentions && !colors.memberListHeaders && !colors.voiceUsers && !colors.messages);
    }

    void needsPermission()
    {
        SinglePluginHost h(qord_register_rolecoloreverywhere);
        h.host->setGranted(h.id, {});
        QVERIFY(h.host->setEnabled(h.id, true));
        QCOMPARE(h.displayOptions.roleColorUpdates, 0);
    }
};

QTEST_GUILESS_MAIN(TestRoleColorEverywhere)
#include "tst_rolecoloreverywhere.moc"
