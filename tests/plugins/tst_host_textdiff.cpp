#include "Plugins/TextDiff.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace Qord::Plugins;

static QString render(const QList<DiffPart> &parts)
{
    QStringList out;
    for (const auto &p : parts) {
        const char kind = p.kind == DiffPart::Kind::Added ? 'a' : p.kind == DiffPart::Kind::Removed ? 'r' : 'u';
        out.append(QString(QLatin1Char(kind)) + QLatin1Char('[') + p.text + QLatin1Char(']'));
    }
    return out.join(u' ');
}

class TestTextDiff : public QObject
{
    Q_OBJECT

private slots:
    void golden_data()
    {
        QTest::addColumn<QString>("oldText");
        QTest::addColumn<QString>("newText");
        QTest::addColumn<QString>("expected");
        QFile f(QStringLiteral(QORD_TEST_DATA_DIR "/textdiff-golden.json"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        int row = 0;
        for (const auto &c : QJsonDocument::fromJson(f.readAll()).array()) {
            const QJsonObject o = c.toObject();
            QList<DiffPart> parts;
            for (const auto &p : o["parts"].toArray()) {
                const QString type = p.toObject()["type"].toString();
                const auto kind = type == "added" ? DiffPart::Kind::Added
                                                  : type == "removed" ? DiffPart::Kind::Removed : DiffPart::Kind::Unchanged;
                parts.append({ kind, p.toObject()["text"].toString() });
            }
            QTest::addRow("%d", row++) << o["old"].toString() << o["new"].toString() << render(parts);
        }
    }

    // Same parts as upstream's createWordDiff under V8
    void golden()
    {
        QFETCH(QString, oldText);
        QFETCH(QString, newText);
        QFETCH(QString, expected);
        QCOMPARE(render(createWordDiff(oldText, newText)), expected);
    }

    void benchmarkTypicalEdit()
    {
        const QString a = QStringLiteral("I think we should meet at 5pm tomorrow near the station, ok?");
        const QString b = QStringLiteral("I think we could meet at 6pm tomorrow by the station, okay?");
        QBENCHMARK {
            (void)createWordDiff(a, b);
        }
    }
};

QTEST_GUILESS_MAIN(TestTextDiff)
#include "tst_host_textdiff.moc"
