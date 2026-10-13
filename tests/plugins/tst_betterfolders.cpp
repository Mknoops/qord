#include "PluginTestUtils.hpp"

#include <QTest>

namespace Qord::Plugins {
void qord_register_betterfolders(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

class TestBetterFolders : public QObject
{
    Q_OBJECT

private slots:
    void followsSettings()
    {
        SinglePluginHost h(qord_register_betterfolders);
        QVERIFY(h.info()->loadError.isEmpty());
        QVERIFY(!h.displayOptions.folders.has_value());
        QVERIFY(h.enableWithAllPermissions());
        QVERIFY(h.displayOptions.folders.has_value());
        QVERIFY(h.displayOptions.folders->sidebar);
        QVERIFY(!h.displayOptions.folders->closeOthers);
        QVERIFY(h.displayOptions.folders->folderIcon == FolderSidebar::FolderIcon::Always);

        auto *settings = h.host->settings(h.id);
        settings->setValue(QStringLiteral("closeOthers"), true);
        settings->setValue(QStringLiteral("keepIcons"), true);
        settings->setValue(QStringLiteral("showFolderIcon"), QStringLiteral("more"));
        QVERIFY(h.displayOptions.folders->closeOthers);
        QVERIFY(h.displayOptions.folders->keepIcons);
        QVERIFY(h.displayOptions.folders->folderIcon == FolderSidebar::FolderIcon::MoreThanOneFolderExpanded);

        QVERIFY(h.host->setEnabled(h.id, false));
        QVERIFY(!h.displayOptions.folders.has_value());
    }
};

QTEST_GUILESS_MAIN(TestBetterFolders)
#include "tst_betterfolders.moc"
