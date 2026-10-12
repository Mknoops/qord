#pragma once

// H.264 encoding and decoding for Discord video (Go Live and cameras), on
// ffmpeg. The encoder tries the GPU encoders first (NVENC, AMF, Quick Sync),
// then OpenH264 and Windows' Media Foundation encoder, and writes Annex B
// access units with SPS/PPS before every keyframe, no B-frames.

#include <QByteArray>
#include <QImage>
#include <QList>
#include <QString>
#include <QStringList>

struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

namespace Qord {
namespace Video {

class H264Encoder
{
public:
    struct Settings
    {
        int width = 1280;
        int height = 720;
        int fps = 30;
        int bitrateKbps = 2500;
        int keyframeIntervalSeconds = 3;
    };

    H264Encoder() = default;
    ~H264Encoder();
    H264Encoder(const H264Encoder &) = delete;
    H264Encoder &operator=(const H264Encoder &) = delete;

    // Opens the first encoder that works, in `preference` order (ffmpeg
    // names) or the default order. False when none does.
    bool open(const Settings &settings, const QStringList &preference = {});
    void close();
    [[nodiscard]] bool isOpen() const { return context != nullptr; }
    [[nodiscard]] const QString &encoderName() const { return name; }
    [[nodiscard]] const Settings &settings() const { return current; }
    static QStringList defaultEncoders();

    // Scales `image` to the encoder size and encodes it. Returns the access
    // units that came out (encoders may hold a frame back).
    QList<QByteArray> encode(const QImage &image, bool forceKeyframe);

private:
    bool openWith(const QString &encoder);
    QList<QByteArray> drain();

    AVCodecContext *context = nullptr;
    AVFrame *frame = nullptr;
    AVPacket *packet = nullptr;
    SwsContext *scaler = nullptr;
    QSize scalerSource;
    int scalerSourceFormat = -1;
    QByteArray parameterSets; // SPS/PPS from extradata, for encoders that keep them there
    Settings current;
    QString name;
    qint64 pts = 0;
};

class H264Decoder
{
public:
    H264Decoder() = default;
    ~H264Decoder();
    H264Decoder(const H264Decoder &) = delete;
    H264Decoder &operator=(const H264Decoder &) = delete;

    // The picture of this access unit, or a null image while the decoder
    // has nothing to show (or on errors)
    QImage decode(const QByteArray &annexB);

private:
    bool ensureOpen();

    AVCodecContext *context = nullptr;
    AVFrame *frame = nullptr;
    AVPacket *packet = nullptr;
    SwsContext *scaler = nullptr;
    int scalerWidth = 0;
    int scalerHeight = 0;
    int scalerFormat = -1;
};

} // namespace Video
} // namespace Qord
