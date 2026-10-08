#include "PluginTestUtils.hpp"

#include <QTest>

namespace Qord::Plugins {
void qord_register_unsuppressembeds(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

class TestUnsuppressEmbeds : public QObject
{
    Q_OBJECT

    static MessageContext message(quint64 flags, bool embeds)
    {
        MessageContext m;
        m.accountId = 1;
        m.channelId = 10;
        m.messageId = 99;
        m.authorId = 5;
        m.flags = flags;
        m.hasEmbeds = embeds;
        return m;
    }

private slots:
    void menuAndAction()
    {
        SinglePluginHost h(qord_register_unsuppressembeds);
        QVERIFY(h.info()->loadError.isEmpty());
        QVERIFY(h.enableWithAllPermissions());

        QVERIFY(h.host->dispatchMessageContextMenuItems(message(0, false)).isEmpty());

        auto items = h.host->dispatchMessageContextMenuItems(message(0, true));
        QCOMPARE(items.size(), 1);
        QCOMPARE(items[0].item.label, QStringLiteral("Suppress Embeds"));
        h.host->dispatchMessageContextMenuAction(h.id, items[0].item.id, message(1, true));
        QCOMPARE(h.messageActions.calls.size(), 1);
        QCOMPARE(h.messageActions.calls[0].flags, quint64(1 | 4)); // other flags kept

        items = h.host->dispatchMessageContextMenuItems(message(4, false));
        QCOMPARE(items[0].item.label, QStringLiteral("Unsuppress Embeds"));
        h.host->dispatchMessageContextMenuAction(h.id, items[0].item.id, message(4 | 32, false));
        QCOMPARE(h.messageActions.calls[1].flags, quint64(32));
        QCOMPARE(h.messageActions.calls[1].messageId, Id(99));

        // Own message without the Embed Links permission: no item
        MessageContext own = message(0, true);
        own.isOwn = true;
        own.canEmbedLinks = false;
        QVERIFY(h.host->dispatchMessageContextMenuItems(own).isEmpty());
    }

    void needsEditPermission()
    {
        SinglePluginHost h(qord_register_unsuppressembeds);
        h.host->setGranted(h.id, Permission::AddUiElements);
        QVERIFY(h.host->setEnabled(h.id, true));
        h.host->dispatchMessageContextMenuAction(h.id, "unsuppress-embeds", message(0, true));
        QVERIFY(h.messageActions.calls.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestUnsuppressEmbeds)
#include "tst_unsuppressembeds.moc"
