#include "PluginTestUtils.hpp"

#include "ReverseImageSearch.hpp"

#include <QTest>

namespace Qord::Plugins {
void qord_register_reverseimagesearch(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

static const QString Image = QStringLiteral("https://cdn.discordapp.com/attachments/1/2/a b(1)!*'~.png?ex=1&is=2");

class TestReverseImageSearch : public QObject
{
    Q_OBJECT

private slots:
    void encodesLikeEncodeURIComponent()
    {
        // Expected value from engine + encodeURIComponent(src) in V8
        QCOMPARE(ReverseImageSearch::searchUrl(ReverseImageSearch::engines().first(), Image).toString(QUrl::FullyEncoded),
                 QStringLiteral("https://lens.google.com/uploadbyurl?url=https%3A%2F%2Fcdn.discordapp.com%2Fattachments%2F1%2F2%2Fa%20b(1)!*'~.png%3Fex%3D1%26is%3D2"));
        QCOMPARE(ReverseImageSearch::engines().size(), 7);
    }

    void submenuOnlyOnImages()
    {
        SinglePluginHost h(qord_register_reverseimagesearch);
        QVERIFY(h.info()->loadError.isEmpty());
        QVERIFY(h.enableWithAllPermissions());
        MessageContext m;
        QVERIFY(h.host->dispatchMessageContextMenuItems(m).isEmpty());
        m.imageUrl = Image;
        const auto items = h.host->dispatchMessageContextMenuItems(m);
        QCOMPARE(items.size(), 1);
        QCOMPARE(items[0].item.label, QStringLiteral("Search Image"));
        QCOMPARE(items[0].item.children.size(), 8);
        QCOMPARE(items[0].item.children.last().label, QStringLiteral("All"));

        h.host->dispatchMessageContextMenuAction(h.id, "search-image-Google", m);
        QCOMPARE(h.ui.opened.size(), 1);
        QVERIFY(h.ui.opened[0].toString().startsWith("https://lens.google.com/uploadbyurl?url="));
        h.host->dispatchMessageContextMenuAction(h.id, "search-image-all", m);
        QCOMPARE(h.ui.opened.size(), 8);
    }

    void openingLinksNeedsPermission()
    {
        SinglePluginHost h(qord_register_reverseimagesearch);
        h.host->setGranted(h.id, Permission::AddUiElements);
        QVERIFY(h.host->setEnabled(h.id, true));
        MessageContext m;
        m.imageUrl = Image;
        QCOMPARE(h.host->dispatchMessageContextMenuItems(m).size(), 1);
        h.host->dispatchMessageContextMenuAction(h.id, "search-image-all", m);
        QVERIFY(h.ui.opened.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestReverseImageSearch)
#include "tst_reverseimagesearch.moc"
