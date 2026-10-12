#include "PluginTestUtils.hpp"

#include <QTest>

namespace Qord::Plugins {
void qord_register_forceownercrown(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

class TestForceOwnerCrown : public QObject
{
    Q_OBJECT

private slots:
    void forcesWhileRunning()
    {
        SinglePluginHost h(qord_register_forceownercrown);
        QVERIFY(h.info()->loadError.isEmpty());
        QVERIFY(!h.displayOptions.ownerCrown);
        QVERIFY(h.enableWithAllPermissions());
        QVERIFY(h.displayOptions.ownerCrown);
        QVERIFY(h.host->setEnabled(h.id, false));
        QVERIFY(!h.displayOptions.ownerCrown);
    }

    void needsPermission()
    {
        SinglePluginHost h(qord_register_forceownercrown);
        h.host->setGranted(h.id, {});
        QVERIFY(h.host->setEnabled(h.id, true));
        QVERIFY(!h.displayOptions.ownerCrown);
    }
};

QTEST_GUILESS_MAIN(TestForceOwnerCrown)
#include "tst_forceownercrown.moc"
