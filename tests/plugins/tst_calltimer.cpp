#include "CallTimer.hpp"
#include "PluginTestUtils.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

namespace Qord::Plugins {
void qord_register_calltimer(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

class TestCallTimer : public QObject
{
    Q_OBJECT

    static VoiceStateChange change(Id userId, Id channelId, qint64 atMs, bool isSelf = false)
    {
        VoiceStateChange c;
        c.accountId = 1;
        c.guildId = 2;
        c.userId = userId;
        c.channelId = channelId;
        c.atMs = atMs;
        c.isSelf = isSelf;
        return c;
    }
    static VoiceParticipantContext participant(Id userId, qint64 nowMs, bool isSelf = false)
    {
        VoiceParticipantContext p;
        p.accountId = 1;
        p.guildId = 2;
        p.channelId = 10;
        p.userId = userId;
        p.nowMs = nowMs;
        p.isSelf = isSelf;
        return p;
    }

private slots:
    // formatDurationMs, run in Node from Equicord's source
    void formatMatchesUpstream()
    {
        QFile file(QStringLiteral(QORD_TEST_DATA_DIR "/calltimer-golden.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonArray cases = QJsonDocument::fromJson(file.readAll()).array();
        QCOMPARE(cases.size(), 64);
        for (const auto &v : cases) {
            const QJsonObject c = v.toObject();
            QCOMPARE(CallTimer::formatDurationMs(qint64(c[u"ms"].toDouble()), c[u"human"].toBool(),
                                                 c[u"seconds"].toBool()),
                     c[u"expected"].toString());
        }
    }

    void connectionTimer()
    {
        SinglePluginHost h(qord_register_calltimer);
        QVERIFY(h.info()->loadError.isEmpty());
        QVERIFY(h.enableWithAllPermissions());
        VoiceConnectionContext me;
        me.accountId = 1;
        me.userId = 7;
        me.nowMs = 100000;
        QVERIFY(!h.host->dispatchVoiceConnectionText(me)); // not in voice
        h.host->dispatchVoiceStateChanged(change(7, 10, 1000, true));
        QCOMPARE(h.host->dispatchVoiceConnectionText(me).value_or(QString()), QStringLiteral("01:39"));
        h.host->settings(h.id)->setValue(QStringLiteral("format"), QStringLiteral("human"));
        QCOMPARE(h.host->dispatchVoiceConnectionText(me).value_or(QString()), QStringLiteral("1m 39s"));
        // Leaving stops it
        h.host->dispatchVoiceStateChanged(change(7, 0, 200000, true));
        QVERIFY(!h.host->dispatchVoiceConnectionText(me));
    }

    void participantTimers()
    {
        SinglePluginHost h(qord_register_calltimer);
        QVERIFY(h.enableWithAllPermissions());
        auto *s = h.host->settings(h.id);
        h.host->dispatchVoiceStateChanged(change(5, 10, 1000)); // 0 means "unknown", as upstream
        // Off by default (allCallTimers)
        QVERIFY(!h.host->dispatchVoiceParticipantLabel(participant(5, 3601000)));
        s->setValue(QStringLiteral("allCallTimers"), true);
        auto label = h.host->dispatchVoiceParticipantLabel(participant(5, 3662000));
        QVERIFY(label);
        QCOMPARE(label->text, QStringLiteral("01:01")); // hours and minutes, no seconds by default
        QVERIFY(!label->inlineText);
        s->setValue(QStringLiteral("showSeconds"), true);
        s->setValue(QStringLiteral("showWithoutHover"), true);
        label = h.host->dispatchVoiceParticipantLabel(participant(5, 3662000));
        QCOMPARE(label->text, QStringLiteral("01:01:01"));
        QVERIFY(label->inlineText);

        // Moving restarts the timer; a repeated join for the same channel does not
        h.host->dispatchVoiceStateChanged(change(5, 10, 5000));
        QCOMPARE(h.host->dispatchVoiceParticipantLabel(participant(5, 3662000))->text, QStringLiteral("01:01:01"));
        h.host->dispatchVoiceStateChanged(change(5, 11, 3601000));
        QCOMPARE(h.host->dispatchVoiceParticipantLabel(participant(5, 3662000))->text, QStringLiteral("01:01"));
        // Unknown users have no timer
        QVERIFY(!h.host->dispatchVoiceParticipantLabel(participant(6, 3661000)));
    }

    void selfOnlyWithTrackSelf()
    {
        SinglePluginHost h(qord_register_calltimer);
        QVERIFY(h.enableWithAllPermissions());
        auto *s = h.host->settings(h.id);
        s->setValue(QStringLiteral("allCallTimers"), true);
        h.host->dispatchVoiceStateChanged(change(7, 10, 1000, true));
        QVERIFY(!h.host->dispatchVoiceParticipantLabel(participant(7, 60000, true)));
        s->setValue(QStringLiteral("trackSelf"), true);
        QVERIFY(h.host->dispatchVoiceParticipantLabel(participant(7, 60000, true)));
    }

    void needsPermission()
    {
        SinglePluginHost h(qord_register_calltimer);
        h.host->setGranted(h.id, {});
        QVERIFY(h.host->setEnabled(h.id, true));
        h.host->dispatchVoiceStateChanged(change(7, 10, 0, true));
        VoiceConnectionContext me;
        me.accountId = 1;
        me.userId = 7;
        me.nowMs = 1000;
        QVERIFY(!h.host->dispatchVoiceConnectionText(me));
    }
};

QTEST_GUILESS_MAIN(TestCallTimer)
#include "tst_calltimer.moc"
