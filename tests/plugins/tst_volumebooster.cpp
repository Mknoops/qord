#include "PluginTestUtils.hpp"

#include <QTest>

namespace Qord::Plugins {
void qord_register_volumebooster(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

class TestVolumeBooster : public QObject
{
    Q_OBJECT

private slots:
    void multipliesTheLimit()
    {
        SinglePluginHost h(qord_register_volumebooster);
        QVERIFY(h.info()->loadError.isEmpty());
        QVERIFY(h.enableWithAllPermissions());
        QCOMPARE(h.audio.multiplier, 2.0);
        h.host->settings(h.id)->setValue(QStringLiteral("multiplier"), QStringLiteral("4.5"));
        QCOMPARE(h.audio.multiplier, 4.5);
        QVERIFY(h.host->setEnabled(h.id, false));
        QCOMPARE(h.audio.multiplier, 1.0);
    }

    void needsPermission()
    {
        SinglePluginHost h(qord_register_volumebooster);
        h.host->setGranted(h.id, {});
        QVERIFY(h.host->setEnabled(h.id, true));
        QCOMPARE(h.audio.multiplier, 1.0);
    }
};

QTEST_GUILESS_MAIN(TestVolumeBooster)
#include "tst_volumebooster.moc"
