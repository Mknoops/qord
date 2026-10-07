#include "PluginTestUtils.hpp"

#include <QTest>

namespace Qord::Plugins {
void qord_register_copyuserurls(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

class TestCopyUserUrls : public QObject
{
    Q_OBJECT

private slots:
    void copiesFromUserMenu()
    {
        SinglePluginHost h(qord_register_copyuserurls);
        QVERIFY(h.info()->loadError.isEmpty());
        UserContext user;
        user.userId = 123456789012345678ull;
        QVERIFY(h.host->dispatchUserContextMenuItems(user).isEmpty()); // disabled
        QVERIFY(h.enableWithAllPermissions());
        const auto items = h.host->dispatchUserContextMenuItems(user);
        QCOMPARE(items.size(), 1);
        QCOMPARE(items[0].pluginId, h.id);
        QCOMPARE(items[0].item.label, QStringLiteral("Copy User URL"));
        h.host->dispatchUserContextMenuAction(h.id, items[0].item.id, user);
        QCOMPARE(h.clipboard.text, QStringLiteral("<https://discord.com/users/123456789012345678>"));
    }

    void noUserNoItem()
    {
        SinglePluginHost h(qord_register_copyuserurls);
        QVERIFY(h.enableWithAllPermissions());
        QVERIFY(h.host->dispatchUserContextMenuItems(UserContext()).isEmpty());
    }

    void clipboardNeedsPermission()
    {
        SinglePluginHost h(qord_register_copyuserurls);
        h.host->setGranted(h.id, Permission::AddUiElements);
        QVERIFY(h.host->setEnabled(h.id, true));
        UserContext user;
        user.userId = 5;
        const auto items = h.host->dispatchUserContextMenuItems(user);
        QCOMPARE(items.size(), 1);
        h.host->dispatchUserContextMenuAction(h.id, items[0].item.id, user);
        QCOMPARE(h.clipboard.writes, 0);
    }
};

QTEST_GUILESS_MAIN(TestCopyUserUrls)
#include "tst_copyuserurls.moc"
