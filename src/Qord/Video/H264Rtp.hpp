#pragma once

// H.264 over RTP the way Discord's clients send it (RFC 6184: single NAL unit
// packets and FU-A fragments, STAP-A understood on receive), and the RTP
// header extension Discord video packets carry. No network or codec code, so
// it is unit tested (tests/qord/tst_h264_rtp.cpp).

#include <QByteArray>
#include <QList>

#include <cstdint>
#include <optional>

namespace Qord {
namespace Video {

// Discord's video payload types (voice SELECT_PROTOCOL codecs)
constexpr int H264PayloadType = 101;
constexpr int H264RtxPayloadType = 102;
// The RTP clock for video
constexpr int VideoClockRate = 90000;
// Payload bytes per packet, as Discord's clients keep packets under the MTU
constexpr int DefaultMaxPayload = 1100;

// The NAL units of an Annex B access unit, without their start codes
QList<QByteArray> splitAnnexB(const QByteArray &frame);
// Annex B again, with 4-byte start codes (what DAVE's H.264 handling writes)
QByteArray joinAnnexB(const QList<QByteArray> &nalUnits);

// RTP payloads for one access unit: each NAL unit in one packet when it
// fits, else FU-A fragments. The last payload ends the frame (RTP marker).
QList<QByteArray> packetize(const QByteArray &annexBFrame, int maxPayload = DefaultMaxPayload);

// The RTP header extension on Discord video packets: the one-byte form
// (0xBEDE) with playout-delay (id 5) set to 0. Returns the 4-byte extension
// header (sent in the clear, part of the AEAD's additional data) and the
// 4-byte body (encrypted with the payload).
struct HeaderExtension
{
    QByteArray header;
    QByteArray body;
};
HeaderExtension playoutDelayExtension();

bool isKeyframe(const QByteArray &annexBFrame);

// Rebuilds access units from received payloads of one SSRC. A frame with a
// missing packet is dropped, and after that only a keyframe is accepted;
// needsKeyframe() then says to ask the sender for one (RTCP PLI).
class Depacketizer
{
public:
    struct Frame
    {
        QByteArray annexB;
        uint32_t timestamp = 0;
        bool keyframe = false;
    };

    // `payload` is the RTP payload after decryption, without the extension body
    std::optional<Frame> push(uint16_t sequence, uint32_t timestamp, bool marker, const QByteArray &payload);

    [[nodiscard]] bool needsKeyframe() const { return waitingForKeyframe; }
    // Frames dropped because packets were missing
    [[nodiscard]] int droppedFrames() const { return dropped; }

private:
    std::optional<Frame> finish();
    void reset();

    QList<QByteArray> nalUnits;
    QByteArray fragment; // FU-A in progress
    bool inFragment = false;
    bool broken = false;
    bool haveFrame = false;
    uint32_t frameTimestamp = 0;
    std::optional<uint16_t> lastSequence;
    bool waitingForKeyframe = true;
    int dropped = 0;
};

} // namespace Video
} // namespace Qord
