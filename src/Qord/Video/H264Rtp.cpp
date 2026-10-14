#include "H264Rtp.hpp"

#include <algorithm>

namespace Qord {
namespace Video {

namespace {

constexpr uint8_t NalTypeMask = 0x1F;
constexpr uint8_t NalIdr = 5;
constexpr uint8_t NalSps = 7;
constexpr uint8_t NalStapA = 24;
constexpr uint8_t NalFuA = 28;

uint8_t nalType(const QByteArray &nal)
{
    return nal.isEmpty() ? 0 : uint8_t(nal[0]) & NalTypeMask;
}

} // namespace

QList<QByteArray> splitAnnexB(const QByteArray &frame)
{
    QList<QByteArray> units;
    const auto *p = reinterpret_cast<const uint8_t *>(frame.constData());
    const qsizetype n = frame.size();
    qsizetype start = -1;
    qsizetype i = 0;
    while (i + 2 < n) {
        if (p[i] == 0 && p[i + 1] == 0 && p[i + 2] == 1) {
            if (start >= 0) {
                // A 4-byte start code's leading zero belongs to it, not the NAL unit
                qsizetype end = i;
                if (end > start && p[end - 1] == 0)
                    --end;
                if (end > start)
                    units.append(frame.mid(start, end - start));
            }
            i += 3;
            start = i;
            continue;
        }
        ++i;
    }
    if (start >= 0 && start < n)
        units.append(frame.mid(start));
    return units;
}

QByteArray joinAnnexB(const QList<QByteArray> &nalUnits)
{
    static const QByteArray startCode("\0\0\0\1", 4);
    QByteArray out;
    for (const QByteArray &nal : nalUnits)
        out += startCode + nal;
    return out;
}

QList<QByteArray> packetize(const QByteArray &annexBFrame, int maxPayload)
{
    QList<QByteArray> payloads;
    if (maxPayload <= 2)
        return payloads;
    for (const QByteArray &nal : splitAnnexB(annexBFrame)) {
        if (nal.isEmpty())
            continue;
        if (nal.size() <= maxPayload) {
            payloads.append(nal);
            continue;
        }
        // FU-A: the NAL header's NRI and type move into the indicator and header
        const uint8_t header = uint8_t(nal[0]);
        const uint8_t indicator = uint8_t((header & 0xE0) | NalFuA);
        const int chunk = maxPayload - 2;
        for (qsizetype offset = 1; offset < nal.size(); offset += chunk) {
            const bool first = offset == 1;
            const bool last = offset + chunk >= nal.size();
            QByteArray fu;
            fu.reserve(maxPayload);
            fu.append(char(indicator));
            fu.append(char((first ? 0x80 : 0) | (last ? 0x40 : 0) | (header & NalTypeMask)));
            fu.append(nal.mid(offset, chunk));
            payloads.append(fu);
        }
    }
    return payloads;
}

HeaderExtension playoutDelayExtension()
{
    // 0xBEDE, one 32-bit word follows; element id 5 with 2 data bytes (len - 1 = 1)
    HeaderExtension ext;
    ext.header = QByteArray("\xBE\xDE\x00\x01", 4);
    ext.body = QByteArray("\x51\x00\x00\x00", 4);
    return ext;
}

bool isKeyframe(const QByteArray &annexBFrame)
{
    for (const QByteArray &nal : splitAnnexB(annexBFrame)) {
        const uint8_t type = nalType(nal);
        if (type == NalIdr || type == NalSps)
            return true;
    }
    return false;
}

void Depacketizer::reset()
{
    nalUnits.clear();
    fragment.clear();
    inFragment = false;
    broken = false;
    haveFrame = false;
    frameBytes = 0;
    framePackets = 0;
}

std::optional<Depacketizer::Frame> Depacketizer::finish()
{
    std::optional<Frame> out;
    if (haveFrame) {
        if (broken || inFragment || nalUnits.isEmpty()) {
            ++dropped;
            waitingForKeyframe = true;
        } else {
            Frame frame;
            frame.annexB = joinAnnexB(nalUnits);
            frame.timestamp = frameTimestamp;
            frame.keyframe = std::any_of(nalUnits.begin(), nalUnits.end(), [](const QByteArray &nal) {
                return nalType(nal) == NalIdr || nalType(nal) == NalSps;
            });
            if (frame.keyframe)
                waitingForKeyframe = false;
            if (!waitingForKeyframe)
                out = frame;
        }
    }
    reset();
    return out;
}

std::optional<Depacketizer::Frame> Depacketizer::push(uint16_t sequence, uint32_t timestamp, bool marker,
                                                      const QByteArray &payload)
{
    std::optional<Frame> completed;
    // A gap means this frame (or the start of it) lost packets
    bool gap = lastSequence && uint16_t(*lastSequence + 1) != sequence;
    // A new timestamp starts a new frame; the old one lost its last packet,
    // which is what the gap was
    if (haveFrame && timestamp != frameTimestamp) {
        broken = true;
        completed = finish();
        gap = false;
    }
    lastSequence = sequence;
    if (!haveFrame) {
        haveFrame = true;
        frameTimestamp = timestamp;
    }
    if (gap)
        broken = true;

    if (!broken && (framePackets >= MaxFramePackets || payload.size() > MaxFrameBytes - frameBytes))
        broken = true;
    if (broken) {
        // Retain the rejected timestamp until its boundary, so a later fragment
        // cannot turn the tail of an oversized/broken frame into a valid frame.
        nalUnits.clear();
        fragment.clear();
        inFragment = false;
        waitingForKeyframe = true;
    } else {
        ++framePackets;
        frameBytes += int(payload.size());
    }

    if (!broken && !payload.isEmpty()) {
        const uint8_t type = nalType(payload);
        if (type == NalFuA) {
            if (payload.size() >= 2) {
                const uint8_t fuHeader = uint8_t(payload[1]);
                const bool start = fuHeader & 0x80;
                const bool end = fuHeader & 0x40;
                if (start) {
                    if (inFragment)
                        broken = true;
                    fragment = QByteArray(1, char((uint8_t(payload[0]) & 0xE0) | (fuHeader & NalTypeMask)));
                    inFragment = true;
                } else if (!inFragment) {
                    broken = true; // the start was lost
                }
                if (inFragment) {
                    fragment += payload.mid(2);
                    if (end) {
                        nalUnits.append(fragment);
                        fragment.clear();
                        inFragment = false;
                    }
                }
            } else {
                broken = true;
            }
        } else if (type == NalStapA) {
            qsizetype offset = 1;
            while (offset + 2 <= payload.size()) {
                const int size = (uint8_t(payload.at(int(offset))) << 8) | uint8_t(payload.at(int(offset + 1)));
                offset += 2;
                if (size == 0 || offset + size > payload.size()) {
                    broken = true;
                    break;
                }
                if (nalUnits.size() >= MaxFrameNalUnits) {
                    broken = true;
                    break;
                }
                nalUnits.append(payload.mid(offset, size));
                offset += size;
            }
        } else if (type >= 1 && type <= 23) {
            nalUnits.append(payload);
        }
    }

    if (nalUnits.size() > MaxFrameNalUnits)
        broken = true;
    if (broken) {
        nalUnits.clear();
        fragment.clear();
        inFragment = false;
        waitingForKeyframe = true;
    }

    if (marker) {
        std::optional<Frame> frame = finish();
        return frame ? frame : completed;
    }
    return completed;
}

} // namespace Video
} // namespace Qord
