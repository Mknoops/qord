#include "PluginTestUtils.hpp"

#include <QTest>

namespace Qord::Plugins {
void qord_register_betterroledot(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

class TestBetterRoleDot : public QObject
{
    Q_OBJECT

private slots:
    void followsSettings()
    {
        SinglePluginHost h(&qord_register_betterroledot);
        QVERIFY(!h.displayOptions.roleDot);
        QVERIFY(h.enableWithAllPermissions());
        QVERIFY(h.displayOptions.roleDot);
        QVERIFY(!h.displayOptions.roleDot->bothStyles);
        QVERIFY(!h.displayOptions.roleDot->inProfile);

        auto *s = h.host->settings(h.id);
        s->setValue(QStringLiteral("bothStyles"), true);
        QVERIFY(h.displayOptions.roleDot->bothStyles);
        s->setValue(QStringLiteral("copyRoleColorInProfilePopout"), true);
        QVERIFY(h.displayOptions.roleDot->inProfile);

        QString error;
        QVERIFY(h.host->setEnabled(h.id, false, &error));
        QVERIFY(!h.displayOptions.roleDot);
    }
};

QTEST_GUILESS_MAIN(TestBetterRoleDot)
#include "tst_betterroledot.moc"
