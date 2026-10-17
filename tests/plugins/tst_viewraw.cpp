#include "PluginTestUtils.hpp"

#include <QTest>

namespace Qord::Plugins {
void qord_register_viewraw(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

class TestViewRaw : public QObject
{
    Q_OBJECT

    static MessageContext message()
    {
        MessageContext m;
        m.messageId = 5;
        m.content = "**hi** <@123>";
        m.rawJson = "{\n    \"id\": \"5\"\n}\n";
        return m;
    }

private slots:
    void popoverViewsAndCopies()
    {
        SinglePluginHost h(qord_register_viewraw);
        QVERIFY(h.info()->loadError.isEmpty());
        QVERIFY(h.enableWithAllPermissions());
        const auto buttons = h.host->dispatchMessagePopoverButtons(message());
        QCOMPARE(buttons.size(), 1);
        QCOMPARE(buttons[0].button.tooltip, QStringLiteral("View Raw (Left Click) / Copy Raw (Right Click)"));
        QVERIFY(buttons[0].button.svg.contains("currentColor"));

        h.host->dispatchMessagePopoverClicked(h.id, buttons[0].button.id, message(), false);
        QCOMPARE(h.ui.dialogTitle, QStringLiteral("Raw Message Data"));
        QCOMPARE(h.ui.dialogSections.size(), 2);
        QCOMPARE(h.ui.dialogSections[0].text, QStringLiteral("**hi** <@123>"));
        QCOMPARE(h.ui.dialogSections[1].text, message().rawJson);
        QVERIFY(h.ui.dialogSections[1].monospace);
        // upstream's copyWithToast messages for the modal's copy buttons
        QCOMPARE(h.ui.dialogSections[0].copiedToast, QStringLiteral("Content copied to clipboard!"));
        QCOMPARE(h.ui.dialogSections[1].copiedToast, QStringLiteral("Message data copied to clipboard!"));

        h.host->dispatchMessagePopoverClicked(h.id, buttons[0].button.id, message(), true);
        QCOMPARE(h.clipboard.text, QStringLiteral("**hi** <@123>"));
        QCOMPARE(h.ui.toasts.size(), 1);
        QCOMPARE(h.ui.toasts[0].first, QStringLiteral("Copied to clipboard!"));
        QCOMPARE(h.ui.toasts[0].second, UiService::ToastType::Success);

        // Click method Right swaps them
        h.host->settings(h.id)->setValue(QStringLiteral("clickMethod"), QStringLiteral("Right"));
        QCOMPARE(h.host->dispatchMessagePopoverButtons(message())[0].button.tooltip,
                 QStringLiteral("Copy Raw (Left Click) / View Raw (Right Click)"));
        h.host->dispatchMessagePopoverClicked(h.id, buttons[0].button.id, message(), false);
        QCOMPARE(h.ui.toasts.size(), 2);
    }

    void messageMenuOnlyWhenEnabled()
    {
        SinglePluginHost h(qord_register_viewraw);
        QVERIFY(h.enableWithAllPermissions());
        QVERIFY(h.host->dispatchMessageContextMenuItems(message()).isEmpty());
        h.host->settings(h.id)->setValue(QStringLiteral("messageContextMenu"), true);
        const auto items = h.host->dispatchMessageContextMenuItems(message());
        QCOMPARE(items.size(), 1);
        QCOMPARE(items[0].item.label, QStringLiteral("View Raw"));
        h.host->dispatchMessageContextMenuAction(h.id, items[0].item.id, message());
        QCOMPARE(h.ui.dialogTitle, QStringLiteral("Raw Message Data"));
    }

    void otherObjects()
    {
        SinglePluginHost h(qord_register_viewraw);
        QVERIFY(h.enableWithAllPermissions());
        h.guilds.raw.insert({ int(GuildDirectory::RawKind::Guild), 7 }, QStringLiteral("{\"id\": \"7\"}"));
        h.guilds.raw.insert({ int(GuildDirectory::RawKind::Role), 9 }, QStringLiteral("{\"id\": \"9\"}"));
        ChannelContext server;
        server.accountId = 1;
        server.guildId = 7;
        const auto items = h.host->dispatchChannelContextMenuItems(server);
        QCOMPARE(items.size(), 1);
        h.host->dispatchChannelContextMenuAction(h.id, items[0].item.id, server);
        QCOMPARE(h.ui.dialogTitle, QStringLiteral("Raw Guild Data"));
        QCOMPARE(h.ui.dialogSections[0].title, QStringLiteral("Guild Data"));
        QCOMPARE(h.ui.dialogSections[0].copiedToast, QStringLiteral("Guild data copied to clipboard!"));

        RoleContext role;
        role.accountId = 1;
        role.guildId = 7;
        role.roleId = 9;
        h.host->dispatchRoleContextMenuAction(h.id, h.host->dispatchRoleContextMenuItems(role)[0].item.id, role);
        QCOMPARE(h.ui.dialogTitle, QStringLiteral("Raw Role Data"));

        // A user the client doesn't have says so
        UserContext user;
        user.accountId = 1;
        user.userId = 99;
        h.host->dispatchUserContextMenuAction(h.id, h.host->dispatchUserContextMenuItems(user)[0].item.id, user);
        QCOMPARE(h.ui.toasts.last().second, UiService::ToastType::Failure);

        // From a profile's More button: the profile
        user.profileJson = QStringLiteral("{}");
        h.host->dispatchUserContextMenuAction(h.id, h.host->dispatchUserContextMenuItems(user)[0].item.id, user);
        QCOMPARE(h.ui.dialogTitle, QStringLiteral("Raw Profile Data"));
    }

    void emptyContentShowsOnlyData()
    {
        SinglePluginHost h(qord_register_viewraw);
        QVERIFY(h.enableWithAllPermissions());
        MessageContext m = message();
        m.content.clear();
        h.host->dispatchMessagePopoverClicked(h.id, "vc-view-raw", m, false);
        QCOMPARE(h.ui.dialogSections.size(), 1);
    }

    void respectsPermissions()
    {
        SinglePluginHost h(qord_register_viewraw);
        h.host->setGranted(h.id, Permission::AddUiElements);
        QVERIFY(h.host->setEnabled(h.id, true));
        h.host->dispatchMessagePopoverClicked(h.id, "vc-view-raw", message(), true);
        QCOMPARE(h.clipboard.writes, 0);
    }
};

QTEST_GUILESS_MAIN(TestViewRaw)
#include "tst_viewraw.moc"
