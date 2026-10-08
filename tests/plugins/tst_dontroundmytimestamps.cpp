#include "PluginTestUtils.hpp"

#include <QTest>

namespace Qord::Plugins {
void qord_register_dontroundmytimestamps(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

class TestDontRoundMyTimestamps : public QObject
{
    Q_OBJECT

private slots:
    void switchesRounding()
    {
        SinglePluginHost h(qord_register_dontroundmytimestamps);
        QVERIFY(h.info()->loadError.isEmpty());
        QVERIFY(!h.displayOptions.roundsDown);
        QVERIFY(h.enableWithAllPermissions());
        QVERIFY(h.displayOptions.roundsDown);
        QVERIFY(h.host->setEnabled(h.id, false));
        QVERIFY(!h.displayOptions.roundsDown);
    }

    void needsPermission()
    {
        SinglePluginHost h(qord_register_dontroundmytimestamps);
        h.host->setGranted(h.id, {});
        QVERIFY(h.host->setEnabled(h.id, true));
        QVERIFY(!h.displayOptions.roundsDown);
    }
};

QTEST_GUILESS_MAIN(TestDontRoundMyTimestamps)
#include "tst_dontroundmytimestamps.moc"
