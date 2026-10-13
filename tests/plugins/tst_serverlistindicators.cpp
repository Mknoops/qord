#include "PluginTestUtils.hpp"

#include <QSignalSpy>
#include <QTest>

namespace Qord::Plugins {
void qord_register_serverlistindicators(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

class TestServerListIndicators : public QObject
{
    Q_OBJECT

    static QList<ServerListButton> buttons(SinglePluginHost &h, int servers = 12)
    {
        ServerListContext ctx;
        ctx.accountId = 1;
        ctx.serverCount = servers;
        QList<ServerListButton> out;
        for (const auto &entry : h.host->dispatchServerListButtons(ctx))
            out.append(entry.button);
        return out;
    }

    // Three friends: one online, one idle, one offline; a non-friend online
    static void start(SinglePluginHost &h)
    {
        QVERIFY(h.enableWithAllPermissions());
        h.relationships.friends = { 10, 11, 12 };
        h.presences.statuses = { { 10, QStringLiteral("online") },
                                 { 11, QStringLiteral("idle") },
                                 { 20, QStringLiteral("online") } };
    }

private slots:
    void onlineFriendsByDefault()
    {
        SinglePluginHost h(&qord_register_serverlistindicators);
        start(h);
        const auto b = buttons(h);
        QCOMPARE(b.size(), 1);
        QCOMPARE(b[0].label, QStringLiteral("2"));
        QCOMPARE(b[0].tooltip, QStringLiteral("2 Friends"));
        QVERIFY(!b[0].svg.isEmpty());
    }

    void modesAndCompact()
    {
        SinglePluginHost h(&qord_register_serverlistindicators);
        start(h);
        auto *s = h.host->settings(h.id);
        s->setValue(QStringLiteral("mode"), QStringLiteral("1"));
        auto b = buttons(h);
        QCOMPARE(b.size(), 1);
        QCOMPARE(b[0].label, QStringLiteral("12"));
        QCOMPARE(b[0].tooltip, QStringLiteral("12 Servers"));

        s->setValue(QStringLiteral("mode"), QStringLiteral("3"));
        s->setValue(QStringLiteral("useCompact"), true);
        b = buttons(h, 4);
        QCOMPARE(b.size(), 2);
        QCOMPARE(b[0].label, QStringLiteral("2 Friends"));
        QCOMPARE(b[1].label, QStringLiteral("4 Servers"));
        QVERIFY(b[0].svg.isEmpty());
        QCOMPARE(b[1].tooltip, QStringLiteral("2 Friends, 4 Servers"));
    }

    void settingChangesAskForARefresh()
    {
        SinglePluginHost h(&qord_register_serverlistindicators);
        start(h);
        QSignalSpy spy(h.host.get(), &PluginHost::serverListChanged);
        h.host->settings(h.id)->setValue(QStringLiteral("useCompact"), true);
        QCOMPARE(spy.size(), 1);
    }
};

QTEST_GUILESS_MAIN(TestServerListIndicators)
#include "tst_serverlistindicators.moc"
