#include "ShowConnections.hpp"
#include "PluginTestUtils.hpp"

#include <QTest>

namespace Qord::Plugins {
void qord_register_showconnections(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

class TestShowConnections : public QObject
{
    Q_OBJECT

private slots:
    void appliesSettings()
    {
        SinglePluginHost h(&qord_register_showconnections);
        QVERIFY(!h.displayOptions.connections);
        QVERIFY(h.enableWithAllPermissions());
        QVERIFY(h.displayOptions.connections);
        QCOMPARE(h.displayOptions.connections->iconSize, 32);
        QCOMPARE(h.displayOptions.connections->spacing, 6); // Cozy
        QVERIFY(h.displayOptions.githubOrgs);

        auto *s = h.host->settings(h.id);
        s->setValue(QStringLiteral("iconSpacing"), QStringLiteral("0"));
        QCOMPARE(h.displayOptions.connections->spacing, 4);
        s->setValue(QStringLiteral("iconSpacing"), QStringLiteral("2"));
        QCOMPARE(h.displayOptions.connections->spacing, 8);
        s->setValue(QStringLiteral("iconSize"), 20);
        QCOMPARE(h.displayOptions.connections->iconSize, 20);
        s->setValue(QStringLiteral("showGithubOrgs"), false);
        QVERIFY(!h.displayOptions.githubOrgs);

        QString error;
        QVERIFY(h.host->setEnabled(h.id, false, &error));
        QVERIFY(!h.displayOptions.connections);
    }

    void parsesOrgs()
    {
        QCOMPARE(ShowConnectionsPlugin::parseOrgs(R"([{"login":"Equicord","id":1},{"login":"Vendicated"},{"id":3}])"),
                 (QStringList{ QStringLiteral("Equicord"), QStringLiteral("Vendicated") }));
        QVERIFY(ShowConnectionsPlugin::parseOrgs(R"({"message":"Not Found"})").isEmpty());
        QVERIFY(ShowConnectionsPlugin::parseOrgs("garbage").isEmpty());
    }

    void fetchesOrgsOncePerName()
    {
        SinglePluginHost h(&qord_register_showconnections);
        QVERIFY(h.enableWithAllPermissions());
        QStringList first, second, third;
        h.displayOptions.githubOrgs(QStringLiteral("Octo Cat"), [&](const QStringList &l) { first = l; });
        h.displayOptions.githubOrgs(QStringLiteral("octo cat"), [&](const QStringList &l) { second = l; });
        QCOMPARE(h.network.urls.size(), 1); // the same name, asked once
        QCOMPARE(h.network.urls.first(), QUrl(QStringLiteral("https://api.github.com/users/Octo%20Cat/orgs")));
        QVERIFY(h.network.respond(true, R"([{"login":"Equicord"}])"));
        QCOMPARE(first, QStringList{ QStringLiteral("Equicord") });
        QCOMPARE(second, first);
        // Remembered
        h.displayOptions.githubOrgs(QStringLiteral("Octo Cat"), [&](const QStringList &l) { third = l; });
        QCOMPARE(third, first);
        QCOMPARE(h.network.urls.size(), 1);
    }

    void failedFetchShowsNoneAndIsRetried()
    {
        SinglePluginHost h(&qord_register_showconnections);
        QVERIFY(h.enableWithAllPermissions());
        bool called = false;
        QStringList got{ QStringLiteral("x") };
        h.displayOptions.githubOrgs(QStringLiteral("ghost"), [&](const QStringList &l) {
            called = true;
            got = l;
        });
        QVERIFY(h.network.respond(false, {}, 404));
        QVERIFY(called);
        QVERIFY(got.isEmpty());
        h.displayOptions.githubOrgs(QStringLiteral("ghost"), [](const QStringList &) {});
        QCOMPARE(h.network.urls.size(), 2);
    }

    void stoppedPluginAnswersNothing()
    {
        SinglePluginHost h(&qord_register_showconnections);
        QVERIFY(h.enableWithAllPermissions());
        auto lookup = h.displayOptions.githubOrgs;
        bool called = false;
        lookup(QStringLiteral("someone"), [&](const QStringList &) { called = true; });
        QString error;
        QVERIFY(h.host->setEnabled(h.id, false, &error));
        h.network.respond(true, R"([{"login":"Equicord"}])");
        lookup(QStringLiteral("someone"), [&](const QStringList &) { called = true; });
        QVERIFY(!called);
    }

    void withoutNetworkNoOrgs()
    {
        SinglePluginHost h(&qord_register_showconnections);
        h.host->setGranted(h.id, Permission::ModifyDisplayedMessages);
        QString error;
        QVERIFY(h.host->setEnabled(h.id, true, &error));
        QVERIFY(h.displayOptions.connections);
        QVERIFY(!h.displayOptions.githubOrgs);
    }
};

QTEST_GUILESS_MAIN(TestShowConnections)
#include "tst_showconnections.moc"
