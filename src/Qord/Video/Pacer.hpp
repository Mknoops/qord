#pragma once

// Paces outgoing video packets. An encoder hands over a whole frame at once
// (a 720p keyframe is around a hundred RTP packets), and sending them in one
// burst overflows router and Wi-Fi queues, which loses packets and forces
// more keyframes. A leaky bucket lets them out at a pacing rate instead:
// 2.5 times the video bitrate (the factor WebRTC's pacer uses), so a normal
// frame still leaves well within its frame interval. When the queue holds
// more than MaxQueueMs of sending, the pacer stops holding back, because
// late video is worse than bursty video. Time comes from the caller, so it
// can be tested without a clock.

#include <QByteArray>
#include <QList>

#include <deque>

namespace Qord {
namespace Video {

class Pacer
{
public:
    static constexpr double PacingFactor = 2.5;
    static constexpr qint64 MinRateBps = 1'000'000;
    static constexpr int MaxQueueMs = 250;
    static constexpr int BurstMs = 5; // how much unused rate may build up

    // The media bitrate; packets leave at PacingFactor times it
    void setBitrate(qint64 bitsPerSecond);
    [[nodiscard]] qint64 rateBps() const { return rate; }

    void push(const QByteArray &packet, qint64 nowMs);
    // The packets that may leave at nowMs, in order
    QList<QByteArray> take(qint64 nowMs);
    // Milliseconds until the next packet may leave (0 when one may now, -1
    // when nothing is queued)
    [[nodiscard]] int nextDelayMs(qint64 nowMs) const;

    [[nodiscard]] bool isEmpty() const { return queue.empty(); }
    [[nodiscard]] qint64 queuedBytes() const { return bytes; }
    void clear();

private:
    void refill(qint64 nowMs);
    [[nodiscard]] double bytesPerMs() const { return double(rate) / 8000.0; }

    std::deque<QByteArray> queue;
    qint64 bytes = 0;
    qint64 rate = qint64(MinRateBps * PacingFactor);
    double budget = 0; // bytes that may leave now
    qint64 lastMs = -1;
};

} // namespace Video
} // namespace Qord
