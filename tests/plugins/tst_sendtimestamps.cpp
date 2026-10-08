#include "PluginTestUtils.hpp"

#include "SendTimestamps.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

namespace Qord::Plugins {
void qord_register_sendtimestamps(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

class TestSendTimestamps : public QObject
{
    Q_OBJECT

private slots:
    // Same answers as V8's Date parser (tests/plugins/data/generate-sendtimestamps-golden.mjs)
    void matchesV8Parsing()
    {
        QFile f(QStringLiteral(QORD_TEST_DATA_DIR "/sendtimestamps-golden.json"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonArray cases = QJsonDocument::fromJson(f.readAll()).array();
        QVERIFY(cases.size() > 20);
        static const QRegularExpression meridiem(QStringLiteral(R"((\d)(AM|PM)$)"),
                                                 QRegularExpression::CaseInsensitiveOption);
        for (const QJsonValue &v : cases) {
            const QJsonObject c = v.toObject();
            QString clean = c.value("input").toString();
            clean.replace(meridiem, QStringLiteral("\\1 \\2"));
            const auto got = SendTimestamps::secondsAfterMidnight(clean);
            const QJsonValue want = c.value("secondsAfterMidnight");
            if (want.isNull())
                QVERIFY2(!got, qPrintable(c.value("input").toString()));
            else
                QVERIFY2(got && *got == want.toInt(), qPrintable(c.value("input").toString()));
        }
    }

    void nextOccurrence()
    {
        const QDateTime now(QDate(2026, 10, 8), QTime(15, 0));
        const qint64 midnight = QDateTime(QDate(2026, 10, 8), QTime(0, 0)).toSecsSinceEpoch();
        // Later today
        QCOMPARE(SendTimestamps::parseTime("`17:59`", now), QStringLiteral("<t:%1:t>").arg(midnight + 64740));
        // Already past: tomorrow
        QCOMPARE(SendTimestamps::parseTime("`3:51`", now), QStringLiteral("<t:%1:t>").arg(midnight + 13860 + 86400));
        // Invalid stays as typed
        QCOMPARE(SendTimestamps::parseTime("`13:00 PM`", now), QStringLiteral("`13:00 PM`"));
    }

    void replacesOnlyBacktickedTimes()
    {
        const QDateTime now(QDate(2026, 10, 8), QTime(9, 0));
        const qint64 midnight = QDateTime(QDate(2026, 10, 8), QTime(0, 0)).toSecsSinceEpoch();
        QCOMPARE(SendTimestamps::replaceTimes("meet at `12:00` or 12:00 or `7:05PM`!", now),
                 QStringLiteral("meet at <t:%1:t> or 12:00 or <t:%2:t>!").arg(midnight + 43200).arg(midnight + 68700));
        QCOMPARE(SendTimestamps::replaceTimes("`1:2` `12:000`", now), QStringLiteral("`1:2` `12:000`"));
    }

    void pluginSendAndPicker()
    {
        SinglePluginHost h(qord_register_sendtimestamps);
        QVERIFY(h.info()->loadError.isEmpty());
        QVERIFY(h.enableWithAllPermissions());
        OutgoingMessage m;
        m.content = "at `23:59`";
        h.host->dispatchBeforeSend(m);
        QVERIFY(m.content.startsWith("at <t:"));
        QVERIFY(h.host->settings(h.id)->setValue("replaceMessageContents", false).isEmpty());
        m.content = "at `23:59`";
        h.host->dispatchBeforeSend(m);
        QCOMPARE(m.content, QStringLiteral("at `23:59`"));

        ChatContext chat;
        const auto buttons = h.host->dispatchChatBarButtons(chat);
        QCOMPARE(buttons.size(), 1);
        QCOMPARE(buttons[0].button.tooltip, QStringLiteral("Insert Timestamp"));
        h.host->dispatchChatBarButtonClicked(h.id, "insert", chat);
        QVERIFY(h.ui.pendingPick);
        h.ui.pendingPick("<t:1700000000:R>");
        QCOMPARE(h.ui.inserted, QStringList { "<t:1700000000:R> " });

        // A pick that arrives after the plugin stopped is dropped
        h.host->dispatchChatBarButtonClicked(h.id, "insert", chat);
        auto late = h.ui.pendingPick;
        QVERIFY(h.host->setEnabled(h.id, false));
        late("<t:1:t>");
        QCOMPARE(h.ui.inserted.size(), 1);
    }
};

QTEST_GUILESS_MAIN(TestSendTimestamps)
#include "tst_sendtimestamps.moc"
