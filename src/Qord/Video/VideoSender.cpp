#include "VideoSender.hpp"

#include "Core/Logging.hpp"
#include "Qord/Video/H264Rtp.hpp"

#include <QDateTime>
#include <QElapsedTimer>

namespace Qord {
namespace Video {

VideoSender::VideoSender(std::unique_ptr<FrameSource> source, const H264Encoder::Settings &settings,
                         const QString &label)
    : source(std::move(source)), settings(settings), label(label)
{
}

VideoSender::~VideoSender() = default;

void VideoSender::begin()
{
    if (!source->start()) {
        emit failed(tr("Couldn't capture %1").arg(source->name()));
        return;
    }
    running = true;
    if (!encoder.open(settings)) {
        end();
        emit failed(tr("No H.264 encoder is available"));
        return;
    }
    emit started(encoder.encoderName());
    startMs = QDateTime::currentMSecsSinceEpoch();
    timer = new QTimer(this);
    timer->setTimerType(Qt::PreciseTimer);
    connect(timer, &QTimer::timeout, this, &VideoSender::tick);
    timer->start(1000 / qMax(1, settings.fps));
}

void VideoSender::end()
{
    if (timer)
        timer->stop();
    if (running)
        source->stop();
    running = false;
    encoder.close();
}

void VideoSender::requestKeyframe()
{
    keyframeWanted = true;
}

void VideoSender::tick()
{
    QElapsedTimer timing;
    timing.start();
    const QImage image = source->grab();
    const qint64 captureNs = timing.nsecsElapsed();
    if (image.isNull()) {
        if (source->sourceGone()) {
            const QString name = source->name();
            end();
            emit failed(tr("%1 is no longer available").arg(name));
        }
        return; // no picture yet
    }
    if (previewSize.isValid())
        emit previewFrame(image.scaled(previewSize, Qt::KeepAspectRatio, Qt::FastTransformation));
    // Keep the source's aspect ratio inside the chosen size
    QSize target = image.size().scaled(QSize(settings.width, settings.height), Qt::KeepAspectRatio);
    if (target.width() > image.width() || target.height() > image.height())
        target = image.size();
    target = QSize(target.width() & ~1, target.height() & ~1);
    if (target != QSize(encoder.settings().width, encoder.settings().height)) {
        H264Encoder::Settings s = settings;
        s.width = target.width();
        s.height = target.height();
        if (!encoder.open(s, { encoder.encoderName() }) && !encoder.open(s)) {
            end();
            emit failed(tr("The encoder stopped"));
            return;
        }
        keyframeWanted = true;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const quint32 timestamp = quint32((now - startMs) * (VideoClockRate / 1000));
    // Periodic keyframes ourselves: not every encoder keeps to its GOP size
    // (AMF doesn't), and viewers joining or losing packets need them
    if (now - lastKeyframeMs >= qint64(settings.keyframeIntervalSeconds) * 1000)
        keyframeWanted = true;
    const bool key = keyframeWanted;
    keyframeWanted = false;
    if (key)
        lastKeyframeMs = now;
    for (const QByteArray &unit : encoder.encode(image, key))
        emit frameEncoded(unit, timestamp);

    // How the stream keeps up, every five seconds
    ++statFrames;
    statCaptureNs += captureNs;
    statTotalNs += timing.nsecsElapsed();
    if (now - statStartMs >= 5000) {
        if (statStartMs)
            qCInfo(LogVoice).nospace() << label << ": " << statFrames * 1000.0 / (now - statStartMs) << " fps, capture "
                                       << statCaptureNs / statFrames / 1000000.0 << " ms, capture+encode "
                                       << statTotalNs / statFrames / 1000000.0 << " ms (" << encoder.encoderName()
                                       << ", " << encoder.settings().width << "x" << encoder.settings().height << ")";
        statStartMs = now;
        statFrames = 0;
        statCaptureNs = statTotalNs = 0;
    }
}

} // namespace Video
} // namespace Qord
