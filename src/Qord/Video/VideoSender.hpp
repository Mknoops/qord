#pragma once

// Captures from a FrameSource and encodes H.264 on its own thread, for Go
// Live and cameras: the sender keeps the source's aspect ratio within the
// chosen size, forces a keyframe every few seconds and on request, and
// stamps frames with the 90 kHz RTP clock.

#include <QImage>
#include <QObject>
#include <QSize>
#include <QTimer>

#include <memory>

#include "Qord/Video/FrameSource.hpp"
#include "Qord/Video/VideoCodec.hpp"

namespace Qord {
namespace Video {

// Captures and encodes on its own thread, from a screen, window or camera
class VideoSender : public QObject
{
    Q_OBJECT
public:
    // `label` names the sender in the log ("Go Live", "Camera")
    VideoSender(std::unique_ptr<FrameSource> source, const H264Encoder::Settings &settings, const QString &label);
    ~VideoSender() override;

    // Also emit each picture, scaled to fit `size`, for a self view
    void setPreviewSize(const QSize &size) { previewSize = size; }

public slots:
    void begin();
    void end();
    void requestKeyframe();

signals:
    void frameEncoded(const QByteArray &annexB, quint32 timestamp);
    void previewFrame(const QImage &frame);
    void started(const QString &encoderName);
    void failed(const QString &reason);

private:
    void tick();

    std::unique_ptr<FrameSource> source;
    H264Encoder::Settings settings;
    QString label;
    QSize previewSize;
    H264Encoder encoder;
    QTimer *timer = nullptr;
    qint64 startMs = 0;
    qint64 lastKeyframeMs = 0;
    qint64 statStartMs = 0;
    qint64 statFrames = 0;
    qint64 statCaptureNs = 0;
    qint64 statTotalNs = 0;
    bool keyframeWanted = true;
    bool running = false;
};

} // namespace Video
} // namespace Qord
