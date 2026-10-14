#include "IngestThread.hpp"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonObject>

#include <array>
#include <cstring>

#include "Core/Logging.hpp"

namespace Acheron {
namespace Discord {

IngestThread::IngestThread(QObject *parent)
{
    memset(&stream, 0, sizeof(stream));
}

IngestThread::~IngestThread()
{
    stop();
    if (streamActive)
        inflateEnd(&stream);
}

void IngestThread::start()
{
    if (running) {
        qCDebug(LogNetwork) << "Attempt to start already running IngestThread";
        return;
    }
    running = true;
    thread = std::thread(&IngestThread::threadLoop, this);
}

void IngestThread::stop()
{
    {
        std::lock_guard<std::mutex> lock(mutex);
        running = false;
    }

    cv.notify_one();

    if (thread.joinable())
        thread.join();
}

void IngestThread::push(const QByteArray &data)
{
    if (data.isEmpty())
        return;
    {
        std::lock_guard lock(mutex);
        if (failed)
            return;
        if (queue.size() >= MaxQueuedChunks || data.size() > MaxQueuedBytes - queuedBytes) {
            failed = true;
            queue.clear();
            queuedBytes = 0;
            emit streamFailed();
            return;
        }
        queuedBytes += int(data.size());
        queue.push_back({ data, generation.load() });
    }
    cv.notify_one();
}

void IngestThread::reset()
{
    // zlib and the compressed buffer are owned exclusively by the worker.
    std::lock_guard lock(mutex);
    generation++;
    queue.clear();
    queuedBytes = 0;
    failed = false;
}

bool IngestThread::acceptsGeneration(quint64 value)
{
    std::lock_guard lock(mutex);
    return !failed && generation.load() == value;
}

void IngestThread::failGeneration(quint64 value)
{
    std::lock_guard lock(mutex);
    if (generation.load() != value || failed)
        return;
    failed = true;
    queue.clear();
    queuedBytes = 0;
    emit streamFailed();
}

void IngestThread::threadLoop()
{
    uint64_t activeGeneration = generation.load();
    const auto clearStream = [this] {
        if (streamActive)
            inflateEnd(&stream);
        streamActive = false;
        memset(&stream, 0, sizeof(stream));
        compressedBuffer.clear();
    };

    while (true) {
        QByteArray data;
        uint64_t dataGen;
        {
            std::unique_lock lock(mutex);
            cv.wait(lock, [this] { return !queue.empty() || !running; });
            if (!running)
                break;
            data = queue.front().data;
            dataGen = queue.front().generation;
            queuedBytes -= int(data.size());
            queue.pop_front();
        }

        if (dataGen != activeGeneration) {
            clearStream();
            activeGeneration = dataGen;
        }
        if (!acceptsGeneration(dataGen))
            continue;

        if (data.size() > MaxCompressedBytes - compressedBuffer.size()) {
            clearStream();
            failGeneration(dataGen);
            continue;
        }
        compressedBuffer.append(data);
        // A curl read may split the four-byte Z_SYNC_FLUSH suffix anywhere.
        // Keep compressed input until the complete message boundary arrives.
        if (!compressedBuffer.endsWith(QByteArray("\x00\x00\xff\xff", 4)))
            continue;

        if (!streamActive) {
            if (inflateInit2(&stream, MAX_WBITS + 32) != Z_OK) {
                clearStream();
                failGeneration(dataGen);
                continue;
            }
            streamActive = true;
        }
        stream.next_in = reinterpret_cast<Bytef *>(compressedBuffer.data());
        stream.avail_in = static_cast<uInt>(compressedBuffer.size());
        std::array<char, 32768> out;
        QByteArray decompressed;
        bool valid = true;
        do {
            // Reset/stop must also interrupt expansion of an old generation.
            if (!running || !acceptsGeneration(dataGen)) {
                valid = false;
                break;
            }
            stream.avail_out = sizeof(out);
            stream.next_out = reinterpret_cast<Bytef *>(out.data());
            const auto before = stream.avail_in;
            const int ret = inflate(&stream, Z_SYNC_FLUSH);
            const int have = int(sizeof(out) - stream.avail_out);
            if ((ret != Z_OK && ret != Z_BUF_ERROR) ||
                have > MaxPayloadBytes - decompressed.size() ||
                (have == 0 && before == stream.avail_in && stream.avail_in != 0)) {
                valid = false;
                break;
            }
            decompressed.append(out.data(), have);
        } while (stream.avail_in != 0 || stream.avail_out == 0);
        compressedBuffer.clear();

        if (!valid) {
            clearStream();
            failGeneration(dataGen);
            continue;
        }
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(decompressed, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) {
            clearStream();
            failGeneration(dataGen);
        } else if (acceptsGeneration(dataGen)) {
            emit payloadReceived(doc.object(), dataGen);
        }
    }
    clearStream();
}

} // namespace Discord
} // namespace Acheron
