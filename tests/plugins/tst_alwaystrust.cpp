#include "PluginTestUtils.hpp"

#include <QTest>

namespace Qord::Plugins {
void qord_register_alwaystrust(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

class TestAlwaysTrust : public QObject
{
    Q_OBJECT

private slots:
    void skipsLinkWarning()
    {
        SinglePluginHost h(qord_register_alwaystrust);
        QVERIFY(h.info()->loadError.isEmpty());
        const QString url = QStringLiteral("https://example.com/");
        QVERIFY(h.host->dispatchShouldConfirmLink(url)); // disabled: Qord asks
        QVERIFY(h.enableWithAllPermissions());
        QVERIFY(!h.host->dispatchShouldConfirmLink(url));
        QVERIFY(h.host->settings(h.id)->setValue("domain", false).isEmpty());
        QVERIFY(h.host->dispatchShouldConfirmLink(url));
    }

    void needsPermission()
    {
        SinglePluginHost h(qord_register_alwaystrust);
        h.host->setGranted(h.id, {});
        QVERIFY(h.host->setEnabled(h.id, true));
        QVERIFY(h.host->dispatchShouldConfirmLink(QStringLiteral("https://example.com/")));
    }
};

QTEST_GUILESS_MAIN(TestAlwaysTrust)
#include "tst_alwaystrust.moc"
