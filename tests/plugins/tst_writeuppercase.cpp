#include "PluginTestUtils.hpp"

#include "WriteUpperCase.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

namespace Qord::Plugins {
void qord_register_writeuppercase(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

class TestWriteUpperCase : public QObject
{
    Q_OBJECT

private slots:
    void golden_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("blocked");
        QTest::addColumn<QString>("expected");
        QFile f(QStringLiteral(QORD_TEST_DATA_DIR "/writeuppercase-golden.json"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        for (const auto &c : QJsonDocument::fromJson(f.readAll()).array()) {
            const QJsonObject o = c.toObject();
            QTest::newRow(qPrintable(o["input"].toString().left(40)))
                    << o["input"].toString() << o["blocked"].toString() << o["expected"].toString();
        }
    }

    // Same output as upstream's pre-send listener under V8
    void golden()
    {
        QFETCH(QString, input);
        QFETCH(QString, blocked);
        QFETCH(QString, expected);
        QCOMPARE(WriteUpperCase::capitalize(input, WriteUpperCase::parseBlockedWords(blocked)), expected);
    }

    void emptyBlocklistCapitalizes()
    {
        // Upstream: "".split(", ") is [""], which blocks every sentence; Qord ignores empty entries
        QCOMPARE(WriteUpperCase::parseBlockedWords(""), QStringList());
        QCOMPARE(WriteUpperCase::capitalize("ok. sure", {}), QStringLiteral("Ok. Sure"));
    }

    void plugin()
    {
        SinglePluginHost h(qord_register_writeuppercase);
        QVERIFY(h.info()->loadError.isEmpty());
        QVERIFY(h.enableWithAllPermissions());
        OutgoingMessage m;
        m.content = "ok then. hi";
        h.host->dispatchBeforeSend(m);
        QCOMPARE(m.content, QStringLiteral("ok then. Hi"));
        QVERIFY(h.host->settings(h.id)->setValue("blockedWords", "hi").isEmpty());
        m.content = "ok then. hi";
        h.host->dispatchBeforeSend(m);
        QCOMPARE(m.content, QStringLiteral("Ok then. hi"));
    }
};

QTEST_GUILESS_MAIN(TestWriteUpperCase)
#include "tst_writeuppercase.moc"
