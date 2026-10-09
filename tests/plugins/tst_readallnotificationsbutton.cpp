#include "PluginTestUtils.hpp"

#include <QTest>

namespace Qord::Plugins {
void qord_register_readallnotificationsbutton(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

class TestReadAllNotificationsButton : public QObject
{
    Q_OBJECT

private slots:
    void buttonMarksEverythingRead()
    {
        SinglePluginHost h(qord_register_readallnotificationsbutton);
        QVERIFY(h.info()->loadError.isEmpty());
        ServerListContext list;
        list.accountId = 7;
        QVERIFY(h.host->dispatchServerListButtons(list).isEmpty()); // disabled
        QVERIFY(h.enableWithAllPermissions());
        const auto buttons = h.host->dispatchServerListButtons(list);
        QCOMPARE(buttons.size(), 1);
        QCOMPARE(buttons[0].pluginId, h.id);
        QCOMPARE(buttons[0].button.label, QStringLiteral("Read All"));
        h.host->dispatchServerListButtonClicked(h.id, buttons[0].button.id, list);
        QCOMPARE(h.readStates.markedAccounts, QList<Id> { 7 });
        h.host->dispatchServerListButtonClicked(h.id, QStringLiteral("other"), list);
        QCOMPARE(h.readStates.markedAccounts.size(), 1);
    }

    void noAccountNoButton()
    {
        SinglePluginHost h(qord_register_readallnotificationsbutton);
        QVERIFY(h.enableWithAllPermissions());
        QVERIFY(h.host->dispatchServerListButtons(ServerListContext()).isEmpty());
    }

    void needsPermissions()
    {
        SinglePluginHost h(qord_register_readallnotificationsbutton);
        ServerListContext list;
        list.accountId = 7;
        // Without add_ui_elements there is no button
        h.host->setGranted(h.id, Permission::MarkRead);
        QVERIFY(h.host->setEnabled(h.id, true));
        QVERIFY(h.host->dispatchServerListButtons(list).isEmpty());
        // Without mark_read the button does nothing
        h.host->setGranted(h.id, Permission::AddUiElements);
        const auto buttons = h.host->dispatchServerListButtons(list);
        QCOMPARE(buttons.size(), 1);
        h.host->dispatchServerListButtonClicked(h.id, buttons[0].button.id, list);
        QVERIFY(h.readStates.markedAccounts.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestReadAllNotificationsButton)
#include "tst_readallnotificationsbutton.moc"
