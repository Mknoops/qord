#include "Qord/Video/H264Rtp.hpp"

#include <QTest>

using namespace Qord::Video;

namespace {

QByteArray nal(uint8_t header, int size, char fill)
{
    QByteArray n(size, fill);
    n[0] = char(header);
    return n;
}

const QByteArray Start4("\0\0\0\1", 4);
const QByteArray Start3("\0\0\1", 3);

struct Packet
{
    uint16_t sequence;
    uint32_t timestamp;
    bool marker;
    QByteArray payload;
};

QList<Packet> packets(const QByteArray &frame, uint16_t &sequence, uint32_t timestamp, int maxPayload = 100)
{
    QList<Packet> out;
    const QList<QByteArray> payloads = packetize(frame, maxPayload);
    for (int i = 0; i < payloads.size(); ++i)
        out.append({ sequence++, timestamp, i == payloads.size() - 1, payloads[i] });
    return out;
}

} // namespace

class TestH264Rtp : public QObject
{
    Q_OBJECT

private slots:
    void splitsAnnexB()
    {
        const QByteArray sps = nal(0x67, 10, 'a');
        const QByteArray pps = nal(0x68, 4, 'b');
        const QByteArray idr = nal(0x65, 50, 'c');
        // Mixed start codes, as encoders write them
        const QByteArray frame = Start4 + sps + Start3 + pps + Start4 + idr;
        const QList<QByteArray> units = splitAnnexB(frame);
        QCOMPARE(units, (QList<QByteArray> { sps, pps, idr }));
        QCOMPARE(joinAnnexB(units), Start4 + sps + Start4 + pps + Start4 + idr);
        QVERIFY(isKeyframe(frame));
        QVERIFY(!isKeyframe(Start4 + nal(0x41, 20, 'd')));
        QVERIFY(splitAnnexB(QByteArray("\x01\x02", 2)).isEmpty());
    }

    void smallUnitsAreSinglePackets()
    {
        const QByteArray frame = Start4 + nal(0x67, 10, 'a') + Start4 + nal(0x68, 4, 'b');
        const QList<QByteArray> payloads = packetize(frame, 100);
        QCOMPARE(payloads.size(), 2);
        QCOMPARE(payloads[0], nal(0x67, 10, 'a'));
    }

    void largeUnitsAreFragmented()
    {
        QByteArray idr = nal(0x65, 250, 'c');
        for (int i = 1; i < idr.size(); ++i)
            idr[i] = char(i);
        const QList<QByteArray> payloads = packetize(Start4 + idr, 100);
        QCOMPARE(payloads.size(), 3); // 249 payload bytes in chunks of 98
        QCOMPARE(uint8_t(payloads[0][0]), uint8_t(0x60 | 28)); // NRI kept, FU-A
        QCOMPARE(uint8_t(payloads[0][1]), uint8_t(0x80 | 5)); // start, IDR
        QCOMPARE(uint8_t(payloads[1][1]), uint8_t(5));
        QCOMPARE(uint8_t(payloads[2][1]), uint8_t(0x40 | 5)); // end
        for (const QByteArray &p : payloads)
            QVERIFY(p.size() <= 100);
    }

    void roundTrip()
    {
        const QByteArray key = Start4 + nal(0x67, 10, 'a') + Start4 + nal(0x68, 4, 'b') + Start4 + nal(0x65, 777, 'c');
        const QByteArray delta = Start4 + nal(0x41, 333, 'd');
        Depacketizer d;
        uint16_t seq = 65530; // wraps around
        QList<Depacketizer::Frame> frames;
        for (const Packet &p : packets(key, seq, 3000) + packets(delta, seq, 6000))
            if (auto f = d.push(p.sequence, p.timestamp, p.marker, p.payload))
                frames.append(*f);
        QCOMPARE(frames.size(), 2);
        QCOMPARE(frames[0].annexB, joinAnnexB(splitAnnexB(key)));
        QVERIFY(frames[0].keyframe);
        QCOMPARE(frames[0].timestamp, 3000u);
        QCOMPARE(frames[1].annexB, delta);
        QVERIFY(!frames[1].keyframe);
        QVERIFY(!d.needsKeyframe());
    }

    void lossDropsUntilKeyframe()
    {
        const QByteArray key = Start4 + nal(0x65, 400, 'c');
        const QByteArray delta = Start4 + nal(0x41, 400, 'd');
        Depacketizer d;
        uint16_t seq = 1;
        int got = 0;
        const auto feed = [&](const QList<Packet> &list, int skip = -1) {
            for (int i = 0; i < list.size(); ++i)
                if (i != skip && d.push(list[i].sequence, list[i].timestamp, list[i].marker, list[i].payload))
                    ++got;
        };
        // Before any keyframe nothing is shown
        feed(packets(delta, seq, 100));
        QCOMPARE(got, 0);
        QVERIFY(d.needsKeyframe());
        feed(packets(key, seq, 200));
        QCOMPARE(got, 1);
        QVERIFY(!d.needsKeyframe());
        // A lost middle fragment drops the frame, and the next delta frame too
        feed(packets(delta, seq, 300), 2);
        QCOMPARE(got, 1);
        QVERIFY(d.needsKeyframe());
        QCOMPARE(d.droppedFrames(), 1);
        feed(packets(delta, seq, 400));
        QCOMPARE(got, 1);
        feed(packets(key, seq, 500));
        QCOMPARE(got, 2);
        // A frame whose marker packet is lost is dropped when the next one starts
        const QList<Packet> lostEnd = packets(delta, seq, 600);
        feed(lostEnd, int(lostEnd.size()) - 1);
        feed(packets(key, seq, 700));
        QCOMPARE(got, 3);
        QCOMPARE(d.droppedFrames(), 2);
    }

    void understandsStapA()
    {
        const QByteArray sps = nal(0x67, 10, 'a');
        const QByteArray pps = nal(0x68, 4, 'b');
        QByteArray stap(1, char(0x78));
        for (const QByteArray &n : { sps, pps })
            stap += char(n.size() >> 8) + QByteArray(1, char(n.size() & 0xFF)) + n;
        Depacketizer d;
        QVERIFY(!d.push(1, 10, false, stap));
        const auto frame = d.push(2, 10, true, nal(0x65, 30, 'c'));
        QVERIFY(frame);
        QCOMPARE(frame->annexB, Start4 + sps + Start4 + pps + Start4 + nal(0x65, 30, 'c'));
    }

    void extensionBytes()
    {
        const HeaderExtension ext = playoutDelayExtension();
        QCOMPARE(ext.header, QByteArray("\xBE\xDE\x00\x01", 4));
        QCOMPARE(ext.body.size(), 4);
        QCOMPARE(uint8_t(ext.body[0]) >> 4, 5); // id
        QCOMPARE(uint8_t(ext.body[0]) & 0x0F, 1); // two data bytes
    }
};

QTEST_GUILESS_MAIN(TestH264Rtp)
#include "tst_h264_rtp.moc"
