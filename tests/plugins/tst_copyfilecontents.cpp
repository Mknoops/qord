#include "PluginTestUtils.hpp"

#include "CopyFileContents.hpp"

#include <QTest>

namespace Qord::Plugins {
void qord_register_copyfilecontents(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

namespace {
AttachmentContext file(qint64 bytesLeft = 0)
{
    AttachmentContext a;
    a.accountId = 1;
    a.channelId = 2;
    a.messageId = 3;
    a.attachmentId = 4;
    a.filename = QStringLiteral("build.log");
    a.text = QStringLiteral("[01] ok\n[02] ok\n");
    a.size = a.text.size() + bytesLeft;
    a.bytesLeft = bytesLeft;
    return a;
}

QString tooltip(PluginHost &host, const AttachmentContext &a)
{
    const auto buttons = host.dispatchAttachmentButtons(a);
    return buttons.size() == 1 ? buttons[0].button.tooltip : QString();
}
} // namespace

class TestCopyFileContents : public QObject
{
    Q_OBJECT

private slots:
    void copiesWithToastThenResets()
    {
        SinglePluginHost h(qord_register_copyfilecontents);
        QVERIFY(h.info()->loadError.isEmpty());
        QVERIFY(h.enableWithAllPermissions());

        QCOMPARE(tooltip(*h.host, file()), QStringLiteral("Copy File Contents"));
        h.host->dispatchAttachmentButtonClicked(h.id, QStringLiteral("cfc-copy"), file());
        QCOMPARE(h.clipboard.text, QStringLiteral("[01] ok\n[02] ok\n"));
        QCOMPARE(h.ui.toasts.size(), 1);
        QCOMPARE(h.ui.toasts[0].first, QStringLiteral("Copied to clipboard!"));
        QVERIFY(h.ui.toasts[0].second == UiService::ToastType::Success);
        QCOMPARE(tooltip(*h.host, file()), QStringLiteral("Copied!"));
        QCOMPARE(h.ui.attachmentRefreshes, QList<Id> { 3 });

        // A second click while "Copied!" shows does nothing
        h.host->dispatchAttachmentButtonClicked(h.id, QStringLiteral("cfc-copy"), file());
        QCOMPARE(h.clipboard.writes, 1);

        // After two seconds the copy button is back
        QTRY_COMPARE_WITH_TIMEOUT(tooltip(*h.host, file()), QStringLiteral("Copy File Contents"),
                                  CopyFileContentsPlugin::CopiedMs + 1000);
        QCOMPARE(h.ui.attachmentRefreshes, (QList<Id> { 3, 3 }));
    }

    void partlyLoadedFilesAreTooLarge()
    {
        SinglePluginHost h(qord_register_copyfilecontents);
        QVERIFY(h.enableWithAllPermissions());
        QCOMPARE(tooltip(*h.host, file(100)), QStringLiteral("File too large to copy"));
        h.host->dispatchAttachmentButtonClicked(h.id, QStringLiteral("cfc-copy"), file(100));
        QCOMPARE(h.clipboard.writes, 0);
        QVERIFY(h.ui.toasts.isEmpty());
    }

    void stoppingCancelsTheReset()
    {
        SinglePluginHost h(qord_register_copyfilecontents);
        QVERIFY(h.enableWithAllPermissions());
        h.host->dispatchAttachmentButtonClicked(h.id, QStringLiteral("cfc-copy"), file());
        QVERIFY(h.host->setEnabled(h.id, false));
        QTest::qWait(CopyFileContentsPlugin::CopiedMs + 200); // the timer fires into a stopped plugin
        QCOMPARE(h.ui.attachmentRefreshes, QList<Id> { 3 });
        QVERIFY(h.host->dispatchAttachmentButtons(file()).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestCopyFileContents)
#include "tst_copyfilecontents.moc"
