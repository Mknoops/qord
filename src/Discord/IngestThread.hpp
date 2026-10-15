#pragma once

#include <QObject>
#include <QJsonObject>
#include <QtZlib/zlib.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <deque>
#include <thread>

namespace Acheron {
namespace Discord {

class IngestThread : public QObject
{
    Q_OBJECT
public:
    explicit IngestThread(QObject *parent = nullptr);
    ~IngestThread() override;

    static constexpr int MaxQueuedBytes = 8 * 1024 * 1024;
    static constexpr int MaxQueuedChunks = 8192;
    static constexpr int MaxCompressedBytes = 8 * 1024 * 1024;
    static constexpr int MaxPayloadBytes = 32 * 1024 * 1024;
    static constexpr int MaxPendingPayloads = 32;
    static constexpr int MaxPendingPayloadBytes = 32 * 1024 * 1024;

    void start();
    void stop();

    void push(const QByteArray &data);
    void reset();
    bool acceptsGeneration(quint64 value);

signals:
    void payloadReceived(QJsonObject root, quint64 generation);
    // Emitted while the queue mutex is held. Direct handlers may only set
    // reconnect flags; they must not call back into this object.
    void streamFailed();

private:
    struct TaggedData
    {
        QByteArray data;
        uint64_t generation;
    };

    void threadLoop();
    void failGeneration(quint64 value);
    void enqueuePayload(const QJsonObject &root, quint64 value, int bytes);
    void deliverPayloads();
    struct PendingPayload {
        QJsonObject root;
        quint64 generation;
        int bytes;
    };

private:
    std::thread thread;
    std::atomic<bool> running = false;

    std::mutex mutex;
    std::condition_variable cv;
    std::deque<TaggedData> queue;
    int queuedBytes = 0;
    bool failed = false;
    std::deque<PendingPayload> pendingPayloads;
    int pendingPayloadBytes = 0;
    bool deliveryScheduled = false;
    QByteArray compressedBuffer;

    z_stream stream;
    bool streamActive = false;
    std::atomic<uint64_t> generation{ 0 };
};

} // namespace Discord
} // namespace Acheron
