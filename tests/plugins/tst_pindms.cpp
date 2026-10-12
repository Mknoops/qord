#include "PluginTestUtils.hpp"

#include "PinDMs.hpp"

#include <QTest>

namespace Qord::Plugins {
void qord_register_pindms(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

namespace {
constexpr Id Account = 1;

ChannelContext dm(Id channelId, int type = 1)
{
    ChannelContext c;
    c.accountId = Account;
    c.channelId = channelId;
    c.type = type;
    return c;
}

QStringList labels(const QList<MenuItem> &items)
{
    QStringList out;
    for (const auto &item : items)
        out.append(item.label.isEmpty() ? QStringLiteral("-") : item.label);
    return out;
}

QList<MenuItem> pinMenu(SinglePluginHost &h, Id channelId)
{
    const auto items = h.host->dispatchChannelContextMenuItems(dm(channelId));
    return items.isEmpty() ? QList<MenuItem>() : items[0].item.children;
}

void click(SinglePluginHost &h, Id channelId, const QString &label)
{
    for (const auto &item : pinMenu(h, channelId))
        if (item.label == label)
            h.host->dispatchChannelContextMenuAction(h.id, item.id, dm(channelId));
}

// New Category from a DM, confirmed with a name and color
void addCategory(SinglePluginHost &h, Id channelId, const QString &name, quint32 color)
{
    click(h, channelId, QStringLiteral("Add Category"));
    QVERIFY(h.ui.formDone);
    h.ui.formDone({ { QStringLiteral("name"), name }, { QStringLiteral("color"), QString::number(color) } });
    h.ui.formDone = nullptr;
}
} // namespace

class TestPinDMs : public QObject
{
    Q_OBJECT

private slots:
    void pinsIntoCategories()
    {
        SinglePluginHost h(qord_register_pindms);
        QVERIFY(h.info()->loadError.isEmpty());
        QVERIFY(h.enableWithAllPermissions());

        // Only DMs and group DMs
        QVERIFY(h.host->dispatchChannelContextMenuItems(ChannelContext()).isEmpty());
        ChannelContext guildChannel = dm(9);
        guildChannel.guildId = 5;
        guildChannel.type = 0;
        QVERIFY(h.host->dispatchChannelContextMenuItems(guildChannel).isEmpty());
        QCOMPARE(labels(pinMenu(h, 10)), QStringList { "Add Category" });
        QVERIFY(!h.host->dispatchDmPinLayout(Account).has_value());

        click(h, 10, QStringLiteral("Add Category"));
        QCOMPARE(h.ui.formTitle, QStringLiteral("New Category"));
        QCOMPARE(h.ui.formConfirmText, QStringLiteral("Create"));
        QCOMPARE(h.ui.formFields[0].value, QStringLiteral("Pin Category 1"));
        QCOMPARE(h.ui.formFields[1].value, QString::number(PinDMsPlugin::DefaultColor));
        QCOMPARE(h.ui.formFields[1].colors.size(), 20);
        h.ui.formDone({ { "name", "Friends" }, { "color", QString::number(0x3498db) } });

        auto layout = h.host->dispatchDmPinLayout(Account);
        QVERIFY(layout.has_value());
        QCOMPARE(layout->first, h.id);
        QCOMPARE(layout->second.categories.size(), 1);
        QCOMPARE(layout->second.categories[0].name, QStringLiteral("Friends"));
        QCOMPARE(layout->second.categories[0].color, quint32(0x3498db));
        QCOMPARE(layout->second.categories[0].channelIds, QList<Id> { 10 });
        QVERIFY(layout->second.sortByLastMessage);

        // Pin another DM into it; a pinned DM offers Unpin
        QCOMPARE(labels(pinMenu(h, 11)), (QStringList { "Add Category", "-", "Friends" }));
        click(h, 11, QStringLiteral("Friends"));
        QCOMPARE(h.host->dispatchDmPinLayout(Account)->second.categories[0].channelIds, (QList<Id> { 10, 11 }));
        QCOMPARE(labels(pinMenu(h, 11)), QStringList { "Unpin DM" });

        // Custom order: move up and down
        h.host->settings(h.id)->setValue(QStringLiteral("pinOrder"), QStringLiteral("custom"));
        QVERIFY(!h.host->dispatchDmPinLayout(Account)->second.sortByLastMessage);
        QCOMPARE(labels(pinMenu(h, 11)), (QStringList { "Unpin DM", "Move Up" }));
        click(h, 11, QStringLiteral("Move Up"));
        QCOMPARE(h.host->dispatchDmPinLayout(Account)->second.categories[0].channelIds, (QList<Id> { 11, 10 }));
        click(h, 11, QStringLiteral("Unpin DM"));
        QCOMPARE(h.host->dispatchDmPinLayout(Account)->second.categories[0].channelIds, QList<Id> { 10 });

        // Other accounts have their own categories
        QVERIFY(!h.host->dispatchDmPinLayout(2).has_value());
    }

    void headers()
    {
        SinglePluginHost h(qord_register_pindms);
        QVERIFY(h.enableWithAllPermissions());
        addCategory(h, 10, QStringLiteral("A"), 1);
        addCategory(h, 11, QStringLiteral("B"), 2);
        auto ids = [&]() {
            QStringList out;
            const auto layout = h.host->dispatchDmPinLayout(Account);
            for (const auto &c : layout->second.categories)
                out.append(c.name);
            return out;
        };
        QCOMPARE(ids(), (QStringList { "A", "B" }));
        const QString a = h.host->dispatchDmPinLayout(Account)->second.categories[0].id;
        const QString b = h.host->dispatchDmPinLayout(Account)->second.categories[1].id;

        // Click collapses
        h.host->dispatchDmPinHeaderClicked(h.id, Account, a);
        QVERIFY(h.host->dispatchDmPinLayout(Account)->second.categories[0].collapsed);
        h.host->dispatchDmPinHeaderClicked(h.id, Account, a);
        QVERIFY(!h.host->dispatchDmPinLayout(Account)->second.categories[0].collapsed);

        QCOMPARE(labels(h.host->dispatchDmPinHeaderMenu(h.id, Account, a)),
                 (QStringList { "Edit Category", "Move Down", "-", "Delete Category" }));
        QCOMPARE(labels(h.host->dispatchDmPinHeaderMenu(h.id, Account, b)),
                 (QStringList { "Edit Category", "Move Up", "-", "Delete Category" }));
        QVERIFY(h.host->dispatchDmPinHeaderMenu(h.id, Account, QString::fromLatin1(DmSectionHeaderId)).isEmpty());
        h.host->dispatchDmPinHeaderMenuAction(h.id, Account, b, QStringLiteral("vc-pindms-move-category-up"));
        QCOMPARE(ids(), (QStringList { "B", "A" }));

        h.host->dispatchDmPinHeaderMenuAction(h.id, Account, a, QStringLiteral("vc-pindms-edit-category"));
        QCOMPARE(h.ui.formTitle, QStringLiteral("Edit Category"));
        QCOMPARE(h.ui.formConfirmText, QStringLiteral("Save"));
        QCOMPARE(h.ui.formFields[0].value, QStringLiteral("A"));
        h.ui.formDone({ { "name", "Renamed" }, { "color", "5" } });
        QCOMPARE(ids(), (QStringList { "B", "Renamed" }));
        QCOMPARE(h.host->dispatchDmPinLayout(Account)->second.categories[1].channelIds, QList<Id> { 10 });

        h.host->dispatchDmPinHeaderMenuAction(h.id, Account, b, QStringLiteral("vc-pindms-delete-category"));
        QCOMPARE(ids(), QStringList { "Renamed" });
    }

    void dmSection()
    {
        SinglePluginHost h(qord_register_pindms);
        QVERIFY(h.enableWithAllPermissions());
        addCategory(h, 10, QStringLiteral("A"), 1);
        const QString section = QString::fromLatin1(DmSectionHeaderId);
        h.host->dispatchDmPinHeaderClicked(h.id, Account, section);
        QVERIFY(!h.host->dispatchDmPinLayout(Account)->second.dmSectionCollapsed); // not collapsible yet
        h.host->settings(h.id)->setValue(QStringLiteral("canCollapseDmSection"), true);
        QVERIFY(h.host->dispatchDmPinLayout(Account)->second.dmSectionCollapsible);
        h.host->dispatchDmPinHeaderClicked(h.id, Account, section);
        QVERIFY(h.host->dispatchDmPinLayout(Account)->second.dmSectionCollapsed);
    }
};

QTEST_GUILESS_MAIN(TestPinDMs)
#include "tst_pindms.moc"
