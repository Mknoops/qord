#include "PluginTestUtils.hpp"

#include "PolishWording.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

namespace Qord::Plugins {
void qord_register_polishwording(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

static PolishWording::Options optionsFrom(const QJsonObject &o)
{
    PolishWording::Options opt;
    opt.quickDisable = o["quickDisable"].toBool();
    opt.blockedWords = o["blockedWords"].toString();
    opt.fixApostrophes = o["fixApostrophes"].toBool();
    opt.expandContractions = o["expandContractions"].toBool();
    opt.fixCapitalization = o["fixCapitalization"].toBool();
    opt.fixPunctuation = o["fixPunctuation"].toBool();
    opt.fixPunctuationFrequency = o["fixPunctuationFrequency"].toInt();
    return opt;
}

class TestPolishWording : public QObject
{
    Q_OBJECT

private slots:
    void golden_data()
    {
        QTest::addColumn<QJsonObject>("c");
        QFile f(QStringLiteral(QORD_TEST_DATA_DIR "/polishwording-golden.json"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        int row = 0;
        for (const auto &c : QJsonDocument::fromJson(f.readAll()).array())
            QTest::addRow("%d %s", row++, qPrintable(c.toObject()["input"].toString().left(30))) << c.toObject();
    }

    // Same output as upstream textProcessing() under V8, with the same random draws
    void golden()
    {
        QFETCH(QJsonObject, c);
        QList<double> draws;
        for (const auto &v : c["random"].toArray())
            draws.append(v.toDouble());
        auto random = [&draws] { return draws.isEmpty() ? 0.0 : draws.takeFirst(); };
        const QString input = c["input"].toString();
        QString expected = c["expected"].toString();
        // Intentional difference: a hand-typed placeholder stays as typed (upstream: "undefined")
        if (input.startsWith(QLatin1String("__CODE_BLOCK_0__")))
            expected = input;
        QCOMPARE(PolishWording::process(input, optionsFrom(c["options"].toObject()), random), expected);
    }

    void plugin()
    {
        SinglePluginHost h(qord_register_polishwording);
        QVERIFY(h.info()->loadError.isEmpty());
        QVERIFY(h.enableWithAllPermissions());
        OutgoingMessage m;
        m.content = "dont worry";
        h.host->dispatchBeforeSend(m);
        QCOMPARE(m.content, QStringLiteral("don't worry")); // fixApostrophes defaults on
        QVERIFY(h.host->settings(h.id)->setValue("quickDisable", true).isEmpty());
        m.content = "dont worry";
        h.host->dispatchBeforeSend(m);
        QCOMPARE(m.content, QStringLiteral("dont worry"));
    }

    void benchmarkAllOptions()
    {
        PolishWording::Options all;
        all.fixCapitalization = all.fixPunctuation = all.expandContractions = true;
        const QString msg = QStringLiteral("hey dont forget the meeting. im running late? youre fine");
        QBENCHMARK {
            (void)PolishWording::process(msg, all, [] { return 0.0; });
        }
    }
};

QTEST_GUILESS_MAIN(TestPolishWording)
#include "tst_polishwording.moc"
