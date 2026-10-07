#include "PluginTestUtils.hpp"

#include "NormalizeMessageLinks.hpp"

#include <QTest>

namespace Qord::Plugins {
void qord_register_normalizemessagelinks(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

class TestNormalizeMessageLinks : public QObject
{
    Q_OBJECT

private slots:
    void host_data()
    {
        QTest::addColumn<QString>("host");
        QTest::addColumn<QString>("expected");
        // Expected values from host.replace(/(^|\b)(canary\.|ptb\.)(discord.com)$/, "$1$3") in V8
        QTest::newRow("canary") << "canary.discord.com" << "discord.com";
        QTest::newRow("ptb") << "ptb.discord.com" << "discord.com";
        QTest::newRow("stable") << "discord.com" << "discord.com";
        QTest::newRow("discordapp") << "canary.discordapp.com" << "canary.discordapp.com";
        QTest::newRow("not at end") << "canary.discord.com.evil.net" << "canary.discord.com.evil.net";
        QTest::newRow("subdomain boundary") << "x.ptb.discord.com" << "x.discord.com";
        QTest::newRow("no boundary") << "xptb.discord.com" << "xptb.discord.com";
        QTest::newRow("unescaped dot quirk") << "ptb.discordXcom" << "discordXcom";
    }

    void host()
    {
        QFETCH(QString, host);
        QFETCH(QString, expected);
        QCOMPARE(NormalizeMessageLinks::normalizeHost(host), expected);
    }

    void link()
    {
        QCOMPARE(NormalizeMessageLinks::normalizeLink("https://canary.discord.com/channels/1/2/3"),
                 QStringLiteral("https://discord.com/channels/1/2/3"));
        QCOMPARE(NormalizeMessageLinks::normalizeLink("https://ptb.discord.com/channels/@me/2/3"),
                 QStringLiteral("https://discord.com/channels/@me/2/3"));
        QCOMPARE(NormalizeMessageLinks::normalizeLink("https://discord.com/channels/1/2/3"),
                 QStringLiteral("https://discord.com/channels/1/2/3"));
        // Only the host is considered
        QCOMPARE(NormalizeMessageLinks::normalizeLink("https://example.com/canary.discord.com"),
                 QStringLiteral("https://example.com/canary.discord.com"));
        QCOMPARE(NormalizeMessageLinks::normalizeLink("not a url"), QStringLiteral("not a url"));
    }

    void plugin()
    {
        SinglePluginHost h(qord_register_normalizemessagelinks);
        QVERIFY(h.info()->loadError.isEmpty());
        const QString canary = QStringLiteral("https://canary.discord.com/channels/1/2/3");
        QCOMPARE(h.host->dispatchTransformCopiedMessageLink(canary), canary); // disabled
        QVERIFY(h.enableWithAllPermissions());
        QCOMPARE(h.host->dispatchTransformCopiedMessageLink(canary), QStringLiteral("https://discord.com/channels/1/2/3"));
    }
};

QTEST_GUILESS_MAIN(TestNormalizeMessageLinks)
#include "tst_normalizemessagelinks.moc"
