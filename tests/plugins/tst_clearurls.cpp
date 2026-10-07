#include "PluginTestUtils.hpp"

#include "ClearUrls.hpp"
#include "ClearUrlsPlugin.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>

namespace Qord::Plugins {
void qord_register_clearurls(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

static QByteArray readData(const QString &name)
{
    QFile f(QStringLiteral(QORD_TEST_DATA_DIR "/") + name);
    if (!f.open(QIODevice::ReadOnly))
        qFatal("missing test data %s", qPrintable(name));
    return f.readAll();
}

// Order matters: "zeta" comes first in the file even though it sorts last
static QByteArray miniRules()
{
    return readData(QStringLiteral("clearurls-mini.json"));
}

class TestClearUrls : public QObject
{
    Q_OBJECT

private slots:
    void golden_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        const QJsonArray cases = QJsonDocument::fromJson(readData("clearurls-golden.json")).array();
        QVERIFY(cases.size() > 30);
        for (const auto &c : cases) {
            const QJsonArray pair = c.toArray();
            QTest::newRow(qPrintable(pair[0].toString().left(70))) << pair[0].toString() << pair[1].toString();
        }
    }

    // Matches upstream's output (V8 + WHATWG URL) on the real rules file
    void golden()
    {
        static ClearUrls::RuleSet rules = [] {
            ClearUrls::RuleSet r;
            QString error;
            if (!r.load(readData("clearurls-data.min.json"), &error))
                qFatal("cannot load rules: %s", qPrintable(error));
            return r;
        }();
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(rules.cleanMessage(input), expected);
    }

    void realRulesLoad()
    {
        ClearUrls::RuleSet rules;
        QVERIFY(rules.load(readData("clearurls-data.min.json")));
        QVERIFY(rules.providerCount() > 150);
        QCOMPARE(rules.skippedPatterns(), 0);
    }

    void providerOrderAndRawRules()
    {
        ClearUrls::RuleSet rules;
        QVERIFY(rules.load(miniRules()));
        // zeta (first in file) cuts /ref=..., deletes tok -> "?x=1" remains;
        // then alpha cuts "?x=1". Alphabetical order would give a different result.
        QCOMPARE(rules.cleanUrl("https://zeta.test/p/ref=abc?tok=1&x=1"), QStringLiteral("https://zeta.test/p"));
    }

    void exceptionsAndInvalidPatterns()
    {
        ClearUrls::RuleSet rules;
        QVERIFY(rules.load(miniRules()));
        QCOMPARE(rules.providerCount(), 4); // "broken" dropped
        QCOMPARE(rules.skippedPatterns(), 2);
        QCOMPARE(rules.cleanUrl("https://keep.test/?drop=1&a=2"), QStringLiteral("https://keep.test/?drop=1&a=2"));
        QCOMPARE(rules.cleanUrl("https://other.test/?drop=1&a=2"), QStringLiteral("https://other.test/?a=2"));
        QCOMPARE(rules.cleanUrl("https://partly.test/?gone=1&a=2"), QStringLiteral("https://partly.test/?a=2"));
        // unanchored, case-insensitive rule match on the param name, as upstream
        QCOMPARE(rules.cleanUrl("https://other.test/?xDROPx=1&a=2"), QStringLiteral("https://other.test/?a=2"));
    }

    void rejectsNonRulesData()
    {
        ClearUrls::RuleSet rules;
        QVERIFY(rules.load(miniRules()));
        QString error;
        QVERIFY(!rules.load("{}", &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!rules.load("garbage"));
        QCOMPARE(rules.providerCount(), 4); // failed loads keep the old rules
    }

    void emptyRulesLeaveMessagesAlone()
    {
        ClearUrls::RuleSet rules;
        const QString msg = QStringLiteral("see https://example.com/?utm_source=x");
        QCOMPARE(rules.cleanMessage(msg), msg);
    }

    void queryHelpers()
    {
        const auto params = ClearUrls::parseQuery(QStringLiteral("a=1&&b&c=x%20y+z&=e"));
        QCOMPARE(params.size(), 4);
        QCOMPARE(params[1].name, QStringLiteral("b"));
        QCOMPARE(params[2].value, QStringLiteral("x y z"));
        QCOMPARE(params[3].name, QString());
        QCOMPARE(ClearUrls::serializeQuery(params), QStringLiteral("a=1&b=&c=x+y+z&=e"));
    }

    // ------------------------------------------------------------- plugin
    void pluginFetchesWhenNoCache()
    {
        SinglePluginHost h(qord_register_clearurls);
        QCOMPARE(h.id, QStringLiteral("clearurls"));
        QVERIFY(h.info()->loadError.isEmpty());
        QVERIFY(h.enableWithAllPermissions());
        QCOMPARE(h.network.urls.size(), 1);
        QCOMPARE(h.network.urls[0].toString(), QString::fromLatin1(ClearUrls::RulesUrl));

        OutgoingMessage m;
        m.content = QStringLiteral("https://example.com/?utm_source=x&a=1");
        h.host->dispatchBeforeSend(m);
        QCOMPARE(m.content, QStringLiteral("https://example.com/?utm_source=x&a=1")); // no rules yet

        QVERIFY(h.network.respond(true, readData("clearurls-data.min.json")));
        h.host->dispatchBeforeSend(m);
        QCOMPARE(m.content, QStringLiteral("https://example.com/?a=1"));

        OutgoingMessage e;
        e.isEdit = true;
        e.content = QStringLiteral("https://example.com/?fbclid=1");
        h.host->dispatchBeforeEdit(e);
        QCOMPARE(e.content, QStringLiteral("https://example.com/"));

        // Cached for next time
        QVERIFY(QFile::exists(QDir(h.dataDir.path()).filePath("clearurls/data.min.json")));
    }

    void pluginUsesFreshCacheWithoutNetwork()
    {
        SinglePluginHost h(qord_register_clearurls);
        QDir().mkpath(QDir(h.dataDir.path()).filePath("clearurls"));
        QFile cache(QDir(h.dataDir.path()).filePath("clearurls/data.min.json"));
        QVERIFY(cache.open(QIODevice::WriteOnly));
        cache.write(readData("clearurls-data.min.json"));
        cache.close();

        // Only the send permission: no network service at all
        h.host->setGranted(h.id, Permission::ModifyOutgoingMessages);
        QVERIFY(h.host->setEnabled(h.id, true));
        QVERIFY(h.network.urls.isEmpty());
        OutgoingMessage m;
        m.content = QStringLiteral("https://example.com/?gclid=1");
        h.host->dispatchBeforeSend(m);
        QCOMPARE(m.content, QStringLiteral("https://example.com/"));
    }

    void pluginRefreshesStaleCacheAndSurvivesFailure()
    {
        SinglePluginHost h(qord_register_clearurls);
        QDir().mkpath(QDir(h.dataDir.path()).filePath("clearurls"));
        const QString path = QDir(h.dataDir.path()).filePath("clearurls/data.min.json");
        QFile cache(path);
        QVERIFY(cache.open(QIODevice::WriteOnly));
        cache.write(readData("clearurls-data.min.json"));
        cache.close();
        QVERIFY(cache.open(QIODevice::ReadWrite));
        QVERIFY(cache.setFileTime(QDateTime::currentDateTime().addDays(-8), QFileDevice::FileModificationTime));
        cache.close();

        QVERIFY(h.enableWithAllPermissions());
        QCOMPARE(h.network.urls.size(), 1); // stale: refresh requested
        QVERIFY(h.network.respond(false, {}));
        OutgoingMessage m;
        m.content = QStringLiteral("https://example.com/?gclid=1");
        h.host->dispatchBeforeSend(m);
        QCOMPARE(m.content, QStringLiteral("https://example.com/")); // still works from cache

        // A bad download does not replace good rules
        QVERIFY(h.host->setEnabled(h.id, false));
        QVERIFY(h.host->setEnabled(h.id, true));
        QVERIFY(h.network.respond(true, "not json"));
        m.content = QStringLiteral("https://example.com/?gclid=1");
        h.host->dispatchBeforeSend(m);
        QCOMPARE(m.content, QStringLiteral("https://example.com/"));
    }

    void pluginCancelsDownloadOnDisable()
    {
        SinglePluginHost h(qord_register_clearurls);
        QVERIFY(h.enableWithAllPermissions());
        QCOMPARE(h.network.requests.size(), 1);
        QVERIFY(h.host->setEnabled(h.id, false));
        QVERIFY(h.network.requests.isEmpty());
        QCOMPARE(h.network.cancelled, 1);
    }

    void benchmarkMessageWithoutUrls()
    {
        ClearUrls::RuleSet rules;
        QVERIFY(rules.load(readData("clearurls-data.min.json")));
        const QString msg = QStringLiteral("just a normal chat message without any links in it at all");
        QBENCHMARK {
            (void)rules.cleanMessage(msg);
        }
    }

    void benchmarkMessageWithUrl()
    {
        ClearUrls::RuleSet rules;
        QVERIFY(rules.load(readData("clearurls-data.min.json")));
        const QString msg = QStringLiteral("look https://www.youtube.com/watch?v=dQw4w9WgXcQ&si=abcdef&feature=share");
        QBENCHMARK {
            (void)rules.cleanMessage(msg);
        }
    }
};

QTEST_GUILESS_MAIN(TestClearUrls)
#include "tst_clearurls.moc"
