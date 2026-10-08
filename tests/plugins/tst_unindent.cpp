#include "PluginTestUtils.hpp"

#include "Unindent.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

namespace Qord::Plugins {
void qord_register_unindent(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

class TestUnindent : public QObject
{
    Q_OBJECT

private slots:
    void golden_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QFile f(QStringLiteral(QORD_TEST_DATA_DIR "/unindent-golden.json"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonArray cases = QJsonDocument::fromJson(f.readAll()).array();
        QVERIFY(cases.size() >= 15);
        for (const auto &c : cases) {
            const QJsonObject o = c.toObject();
            QTest::newRow(qPrintable(o["name"].toString())) << o["input"].toString() << o["expected"].toString();
        }
    }

    // Same output as upstream unindentMsg() under V8
    void golden()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(Unindent::unindentMessage(input), expected);
    }

    void unindentDirect()
    {
        QCOMPARE(Unindent::unindent("  a\n    b"), QStringLiteral("a\n  b"));
        QCOMPARE(Unindent::unindent(""), QString());
        QCOMPARE(Unindent::unindent("\n\n"), QStringLiteral("\n\n"));
        QCOMPARE(Unindent::unindent("\ta"), QStringLiteral("a"));
    }

    void pluginOnSendAndEdit()
    {
        SinglePluginHost h(qord_register_unindent);
        QVERIFY(h.info()->loadError.isEmpty());
        QVERIFY(h.enableWithAllPermissions());
        OutgoingMessage m;
        m.content = "```\n    x\n```";
        h.host->dispatchBeforeSend(m);
        QCOMPARE(m.content, QStringLiteral("```\nx\n```"));
        m.content = "```\n\ty\n```";
        h.host->dispatchBeforeEdit(m);
        QCOMPARE(m.content, QStringLiteral("```\ny\n```"));
    }

    void displaySide()
    {
        // Upstream unindents the code block's content when rendering it
        QCOMPARE(Unindent::unindentRendered("a\n```cpp\n    int x;\n      y;\n```\nb"),
                 QStringLiteral("a\n```cpp\nint x;\n  y;```\nb"));
        // No language: the first code line must not turn into one
        QCOMPARE(Unindent::unindentRendered("```\n  foo\n    bar```"), QStringLiteral("```\nfoo\n  bar```"));
        QCOMPARE(Unindent::unindentRendered("plain `code` text"), QStringLiteral("plain `code` text"));

        SinglePluginHost h(qord_register_unindent);
        QVERIFY(h.enableWithAllPermissions());
        IncomingMessage in;
        in.content = "```\n    x\n```";
        h.host->dispatchTransformIncomingContent(in);
        QCOMPARE(in.content, QStringLiteral("```\nx```"));
    }

    void benchmarkPlainMessage()
    {
        const QString msg = QStringLiteral("no code blocks in this one, just chatting");
        QBENCHMARK {
            (void)Unindent::unindentMessage(msg);
        }
    }
};

QTEST_GUILESS_MAIN(TestUnindent)
#include "tst_unindent.moc"
