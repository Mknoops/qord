#include "Qord/Video/Pacer.hpp"

#include <QTest>

using Qord::Video::Pacer;

namespace {
QByteArray packet(int size = 1200)
{
    return QByteArray(size, 'x');
}
} // namespace

class TestPacer : public QObject
{
    Q_OBJECT

private slots:
    void rateIsAFactorOfTheBitrate()
    {
        Pacer pacer;
        pacer.setBitrate(2'500'000);
        QCOMPARE(pacer.rateBps(), qint64(6'250'000));
        // Low bitrates still pace at a useful speed
        pacer.setBitrate(100'000);
        QCOMPARE(pacer.rateBps(), qint64(Pacer::MinRateBps * Pacer::PacingFactor));
    }

    void keyframeIsSpreadOut()
    {
        // 2.5 Mbit/s video, so 6.25 Mbit/s pacing: about 781 bytes per ms
        Pacer pacer;
        pacer.setBitrate(2'500'000);
        for (int i = 0; i < 100; ++i) // a 120 kB keyframe
            pacer.push(packet(), 0);
        // Only the burst allowance (5 ms) leaves at once, not the frame
        const int first = int(pacer.take(0).size());
        QVERIFY2(first >= 3 && first <= 5, qPrintable(QString::number(first)));
        // The rest leaves at the pacing rate: all of it within ~160 ms,
        // well inside the 250 ms cap, and steadily, not at the end
        int sent = first;
        qint64 now = 0;
        int maxPerStep = 0;
        while (!pacer.isEmpty()) {
            const int delay = pacer.nextDelayMs(now);
            QVERIFY(delay >= 0);
            now += qMax(1, delay);
            const int n = int(pacer.take(now).size());
            maxPerStep = qMax(maxPerStep, n);
            sent += n;
            QVERIFY(now < 1000);
        }
        QCOMPARE(sent, 100);
        QVERIFY2(now >= 140 && now <= 170, qPrintable(QString::number(now)));
        QVERIFY(maxPerStep <= 2);
    }

    void orderIsKept()
    {
        Pacer pacer;
        for (int i = 0; i < 20; ++i)
            pacer.push(QByteArray::number(i).leftJustified(1000, ' '), 0);
        QList<QByteArray> out;
        for (qint64 now = 0; !pacer.isEmpty(); ++now)
            out += pacer.take(now);
        QCOMPARE(out.size(), 20);
        for (int i = 0; i < 20; ++i)
            QCOMPARE(out[i].trimmed(), QByteArray::number(i));
    }

    void idleTimeDoesNotBecomeABurst()
    {
        Pacer pacer;
        pacer.setBitrate(2'500'000);
        pacer.push(packet(), 0);
        QCOMPARE(pacer.take(0).size(), 1);
        // A second of nothing, then a big frame: still only one burst's worth
        for (int i = 0; i < 50; ++i)
            pacer.push(packet(), 1000);
        QVERIFY(pacer.take(1000).size() <= 5);
    }

    void farBehindFlushes()
    {
        // At the minimum rate (2.5 Mbit/s pacing, ~312 bytes/ms), 100 kB is
        // 320 ms of sending: more than the cap, so it all goes
        Pacer pacer;
        pacer.setBitrate(Pacer::MinRateBps);
        for (int i = 0; i < 84; ++i)
            pacer.push(packet(), 0);
        QCOMPARE(pacer.take(0).size(), 84);
        QVERIFY(pacer.isEmpty());
        QCOMPARE(pacer.nextDelayMs(0), -1);
    }

    void clearForgetsEverything()
    {
        Pacer pacer;
        pacer.push(packet(), 0);
        pacer.push(packet(), 0);
        pacer.clear();
        QVERIFY(pacer.isEmpty());
        QCOMPARE(pacer.queuedBytes(), 0);
        QCOMPARE(pacer.nextDelayMs(5), -1);
    }
};

QTEST_GUILESS_MAIN(TestPacer)
#include "tst_pacer.moc"
