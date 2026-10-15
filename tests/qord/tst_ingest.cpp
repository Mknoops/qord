#include "Discord/IngestThread.hpp"

#include <QJsonObject>
#include <QLoggingCategory>
#include <QSignalSpy>
#include <QTest>
#include <QThread>

#include <array>

Q_LOGGING_CATEGORY(LogNetwork, "test.ingest")
using Acheron::Discord::IngestThread;

namespace {
// Produces Discord-style persistent zlib streams, using dummy JSON only.
class Compressor
{
public:
    Compressor() { ready = deflateInit(&stream, Z_DEFAULT_COMPRESSION) == Z_OK; }
    ~Compressor() { if (ready) deflateEnd(&stream); }
    QByteArray message(QByteArray input)
    {
        if (!ready)
            return {};
        stream.next_in = reinterpret_cast<Bytef *>(input.data());
        stream.avail_in = static_cast<uInt>(input.size());
        QByteArray output;
        std::array<char, 32768> chunk;
        do {
            stream.next_out = reinterpret_cast<Bytef *>(chunk.data());
            stream.avail_out = chunk.size();
            if (deflate(&stream, Z_SYNC_FLUSH) != Z_OK)
                return {};
            output.append(chunk.data(), int(chunk.size() - stream.avail_out));
        } while (stream.avail_in || stream.avail_out == 0);
        return output;
    }
private:
    z_stream stream{};
    bool ready = false;
};
}

class TestIngest : public QObject
{
    Q_OBJECT
private slots:
    void splitFlushMarker_data()
    {
        QTest::addColumn<int>("tail");
        for (int tail = 1; tail <= 4; ++tail)
            QTest::newRow(qPrintable(QString::number(tail))) << tail;
    }

    void splitFlushMarker()
    {
        QFETCH(int, tail);
        IngestThread ingest;
        QSignalSpy payload(&ingest, &IngestThread::payloadReceived);
        QSignalSpy failure(&ingest, &IngestThread::streamFailed);
        Compressor compressor;
        const QByteArray first = compressor.message("{\"op\":1}");
        QVERIFY(first.endsWith(QByteArray("\0\0\xff\xff", 4)));
        ingest.start();
        ingest.push(first.left(first.size() - tail));
        ingest.push(first.right(tail));
        QTRY_COMPARE(payload.count(), 1);
        QCOMPARE(qvariant_cast<QJsonObject>(payload.at(0).at(0)).value("op").toInt(), 1);
        const QByteArray second = compressor.message("{\"op\":2}");
        QVERIFY(!second.isEmpty());
        ingest.push(second);
        QTRY_COMPARE(payload.count(), 2);
        QCOMPARE(failure.count(), 0);
    }

    void oversizedExpansionFailsClosedAndResetRecovers()
    {
        IngestThread ingest;
        QSignalSpy payload(&ingest, &IngestThread::payloadReceived);
        QSignalSpy failure(&ingest, &IngestThread::streamFailed);
        Compressor compressor;
        const QByteArray bomb = compressor.message(QByteArray("{\"data\":\"") +
            QByteArray(IngestThread::MaxPayloadBytes, 'x') + "\"}");
        QVERIFY(!bomb.isEmpty());
        QVERIFY(bomb.size() < IngestThread::MaxQueuedBytes);
        ingest.start();
        ingest.push(bomb);
        QTRY_COMPARE(failure.count(), 1);
        QVERIFY(!ingest.acceptsGeneration(0));
        QCOMPARE(payload.count(), 0);
        // A failed zlib generation stays closed even for otherwise valid data.
        ingest.push(compressor.message("{\"op\":2}"));
        QVERIFY(!ingest.acceptsGeneration(0));
        ingest.reset();
        QVERIFY(!ingest.acceptsGeneration(0));
        QVERIFY(ingest.acceptsGeneration(1));
        Compressor fresh;
        ingest.push(fresh.message("{\"op\":3}"));
        QTRY_COMPARE(payload.count(), 1);
        QCOMPARE(payload.at(0).at(1).toULongLong(), qulonglong(1));
        QCOMPARE(qvariant_cast<QJsonObject>(payload.at(0).at(0)).value("op").toInt(), 3);
        QCOMPARE(failure.count(), 1);
    }

    void queueOverflowFailsClosed()
    {
        IngestThread ingest;
        QSignalSpy failure(&ingest, &IngestThread::streamFailed);
        ingest.push(QByteArray(IngestThread::MaxQueuedBytes + 1, 'x'));
        QCOMPARE(failure.count(), 1);
        ingest.push("more");
        QCOMPARE(failure.count(), 1);
        QVERIFY(!ingest.acceptsGeneration(0));
        ingest.reset();
        QVERIFY(ingest.acceptsGeneration(1));
    }

    void tinyChunksCannotGrowQueueWithoutBound()
    {
        IngestThread ingest;
        QSignalSpy failure(&ingest, &IngestThread::streamFailed);
        for (int i = 0; i < IngestThread::MaxQueuedChunks; ++i)
            ingest.push("x");
        QCOMPARE(failure.count(), 0);
        ingest.push("x");
        QCOMPARE(failure.count(), 1);
    }

    void resetDropsQueuedOldGeneration()
    {
        IngestThread ingest;
        QSignalSpy payload(&ingest, &IngestThread::payloadReceived);
        Compressor old;
        ingest.push(old.message("{\"op\":1}"));
        ingest.reset();
        Compressor fresh;
        ingest.push(fresh.message("{\"op\":2}"));
        ingest.start();
        QTRY_COMPARE(payload.count(), 1);
        QCOMPARE(payload.at(0).at(1).toULongLong(), qulonglong(1));
        QCOMPARE(qvariant_cast<QJsonObject>(payload.at(0).at(0)).value("op").toInt(), 2);
    }

    void slowConsumerCannotGrowQtEventBacklog()
    {
        IngestThread ingest;
        QSignalSpy payload(&ingest, &IngestThread::payloadReceived);
        Compressor compressor;
        ingest.start();
        for (int i = 0; i <= IngestThread::MaxPendingPayloads; ++i)
            ingest.push(compressor.message("{\"op\":1}"));
        // Deliberately do not pump the owning event loop: the worker must
        // reject the backlog instead of enqueueing unbounded JSON events.
        for (int i = 0; i < 500 && ingest.acceptsGeneration(0); ++i)
            QThread::msleep(10);
        QVERIFY(!ingest.acceptsGeneration(0));
        QCoreApplication::processEvents();
        QCOMPARE(payload.count(), 0);
        ingest.reset();
        Compressor fresh;
        ingest.push(fresh.message("{\"op\":2}"));
        QTRY_COMPARE(payload.count(), 1);
    }

    void rejectsCorruptStream()
    {
        IngestThread ingest;
        QSignalSpy failure(&ingest, &IngestThread::streamFailed);
        QSignalSpy payload(&ingest, &IngestThread::payloadReceived);
        ingest.start();
        ingest.push(QByteArray("invalid zlib") + QByteArray("\0\0\xff\xff", 4));
        QTRY_COMPARE(failure.count(), 1);
        QCOMPARE(payload.count(), 0);
    }
};

QTEST_GUILESS_MAIN(TestIngest)
#include "tst_ingest.moc"
