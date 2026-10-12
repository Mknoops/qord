#include "Pacer.hpp"

#include <cmath>

namespace Qord {
namespace Video {

void Pacer::setBitrate(qint64 bitsPerSecond)
{
    rate = qint64(qMax(bitsPerSecond, MinRateBps) * PacingFactor);
}

void Pacer::refill(qint64 nowMs)
{
    if (lastMs < 0)
        budget = BurstMs * bytesPerMs(); // a new pacer is an idle one
    else if (nowMs > lastMs)
        budget += double(nowMs - lastMs) * bytesPerMs();
    lastMs = qMax(lastMs, nowMs);
    // Idle time doesn't save up into a burst
    budget = qMin(budget, BurstMs * bytesPerMs());
}

void Pacer::push(const QByteArray &packet, qint64 nowMs)
{
    if (queue.empty())
        refill(nowMs); // start the bucket from now, with what it held
    queue.push_back(packet);
    bytes += packet.size();
}

QList<QByteArray> Pacer::take(qint64 nowMs)
{
    refill(nowMs);
    QList<QByteArray> out;
    // Too far behind: let everything go
    const bool flush = double(bytes) / bytesPerMs() > MaxQueueMs;
    // A packet may leave while the budget is positive; it can go below zero,
    // which the next refill pays back, so large packets aren't starved
    while (!queue.empty() && (flush || budget > 0)) {
        QByteArray packet = std::move(queue.front());
        queue.pop_front();
        bytes -= packet.size();
        budget -= packet.size();
        out.append(std::move(packet));
    }
    if (flush)
        budget = qMin(budget, 0.0);
    return out;
}

int Pacer::nextDelayMs(qint64 nowMs) const
{
    if (queue.empty())
        return -1;
    double available = budget;
    if (lastMs >= 0 && nowMs > lastMs)
        available += double(nowMs - lastMs) * bytesPerMs();
    if (available > 0)
        return 0;
    return qMax(1, int(std::ceil((-available + 1) / bytesPerMs())));
}

void Pacer::clear()
{
    queue.clear();
    bytes = 0;
    budget = 0;
    lastMs = -1;
}

} // namespace Video
} // namespace Qord
