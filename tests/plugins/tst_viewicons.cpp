#include "PluginTestUtils.hpp"
#include "ViewIcons.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

namespace Qord::Plugins {
void qord_register_viewicons(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

class TestViewIcons : public QObject
{
    Q_OBJECT

    static QStringList labels(const QList<PluginHost::PluginMenuItem> &items)
    {
        QStringList out;
        for (const auto &i : items)
            out.append(i.item.label);
        return out;
    }

private slots:
    // Upstream's openImage(), run in Node (generate-viewicons-golden.mjs)
    void urlsMatchUpstream()
    {
        QFile file(QStringLiteral(QORD_TEST_DATA_DIR "/viewicons-golden.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonArray cases = QJsonDocument::fromJson(file.readAll()).array();
        QCOMPARE(cases.size(), 108);
        for (const auto &v : cases) {
            const QJsonObject c = v.toObject();
            QCOMPARE(ViewIcons::imageUrl(c[u"url"].toString(), c[u"format"].toString(), c[u"size"].toString()),
                     c[u"expected"].toString());
        }
    }

    void userMenu()
    {
        SinglePluginHost h(qord_register_viewicons);
        QVERIFY(h.info()->loadError.isEmpty());
        QVERIFY(h.enableWithAllPermissions());
        UserContext user;
        user.userId = 2;
        user.guildId = 1;
        user.avatarUrl = QStringLiteral("https://cdn.discordapp.com/avatars/2/abc.png?size=128");
        auto items = h.host->dispatchUserContextMenuItems(user);
        QCOMPARE(labels(items), QStringList { QStringLiteral("View Avatar") });

        user.memberAvatarUrl = QStringLiteral("https://cdn.discordapp.com/guilds/1/users/2/avatars/def.png?size=128");
        items = h.host->dispatchUserContextMenuItems(user);
        QCOMPARE(labels(items), (QStringList { QStringLiteral("View Avatar"), QStringLiteral("View Server Avatar") }));

        h.host->dispatchUserContextMenuAction(h.id, items[0].item.id, user);
        h.host->dispatchUserContextMenuAction(h.id, items[1].item.id, user);
        QCOMPARE(h.ui.images.size(), 2);
        QCOMPARE(h.ui.images[0].toString(), QStringLiteral("https://cdn.discordapp.com/avatars/2/abc.webp?size=1024"));
        QCOMPARE(h.ui.images[1].toString(),
                 QStringLiteral("https://cdn.discordapp.com/guilds/1/users/2/avatars/def.webp?size=1024"));
    }

    void settingsApply()
    {
        SinglePluginHost h(qord_register_viewicons);
        QVERIFY(h.enableWithAllPermissions());
        h.host->settings(h.id)->setValue(QStringLiteral("format"), QStringLiteral("png"));
        h.host->settings(h.id)->setValue(QStringLiteral("imgSize"), QStringLiteral("4096"));
        UserContext user;
        user.userId = 2;
        user.avatarUrl = QStringLiteral("https://cdn.discordapp.com/avatars/2/abc.png?size=128");
        h.host->dispatchUserContextMenuAction(h.id, QStringLiteral("view-avatar"), user);
        QCOMPARE(h.ui.images.value(0).toString(), QStringLiteral("https://cdn.discordapp.com/avatars/2/abc.png?size=4096"));
    }

    void serverAndGroupMenus()
    {
        SinglePluginHost h(qord_register_viewicons);
        QVERIFY(h.enableWithAllPermissions());
        ChannelContext guild;
        guild.guildId = 1;
        QVERIFY(h.host->dispatchChannelContextMenuItems(guild).isEmpty());
        guild.iconUrl = QStringLiteral("https://cdn.discordapp.com/icons/1/i.png?size=64");
        guild.bannerUrl = QStringLiteral("https://cdn.discordapp.com/banners/1/b.png?size=480");
        auto items = h.host->dispatchChannelContextMenuItems(guild);
        QCOMPARE(labels(items), (QStringList { QStringLiteral("View Icon"), QStringLiteral("View Banner") }));
        h.host->dispatchChannelContextMenuAction(h.id, items[1].item.id, guild);
        QCOMPARE(h.ui.images.value(0).toString(), QStringLiteral("https://cdn.discordapp.com/banners/1/b.webp?size=1024"));

        // A text channel's menu gets nothing
        ChannelContext text = guild;
        text.channelId = 5;
        text.type = 0;
        QVERIFY(h.host->dispatchChannelContextMenuItems(text).isEmpty());

        ChannelContext group;
        group.channelId = 9;
        group.type = 3;
        group.iconUrl = QStringLiteral("https://cdn.discordapp.com/channel-icons/9/g.png?size=64");
        items = h.host->dispatchChannelContextMenuItems(group);
        QCOMPARE(labels(items), QStringList { QStringLiteral("View Icon") });
        h.host->dispatchChannelContextMenuAction(h.id, items[0].item.id, group);
        QCOMPARE(h.ui.images.value(1).toString(),
                 QStringLiteral("https://cdn.discordapp.com/channel-icons/9/g.webp?size=1024"));
    }

    void profileClicks()
    {
        SinglePluginHost h(qord_register_viewicons);
        ProfileImageContext image;
        image.url = QStringLiteral("https://cdn.discordapp.com/avatars/2/a_x.gif?size=256");
        QVERIFY(!h.host->dispatchProfileImageClicked(image)); // disabled
        QVERIFY(h.enableWithAllPermissions());
        QVERIFY(h.host->dispatchProfileImageClicked(image));
        // Animated GIF avatars keep their extension (upstream's regex leaves .gif alone)
        QCOMPARE(h.ui.images.value(0).toString(), QStringLiteral("https://cdn.discordapp.com/avatars/2/a_x.gif?size=1024"));
        QVERIFY(!h.host->dispatchProfileImageClicked(ProfileImageContext()));
    }

    void needsPermission()
    {
        SinglePluginHost h(qord_register_viewicons);
        h.host->setGranted(h.id, {});
        QVERIFY(h.host->setEnabled(h.id, true));
        UserContext user;
        user.userId = 2;
        user.avatarUrl = QStringLiteral("https://cdn.discordapp.com/avatars/2/abc.png");
        QVERIFY(h.host->dispatchUserContextMenuItems(user).isEmpty());
        ProfileImageContext image;
        image.url = user.avatarUrl;
        QVERIFY(!h.host->dispatchProfileImageClicked(image));
    }
};

QTEST_GUILESS_MAIN(TestViewIcons)
#include "tst_viewicons.moc"
