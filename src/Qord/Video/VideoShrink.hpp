#pragma once

// Qord: a video too large to upload, re-encoded to fit (FakeNitro's "Shrink
// large videos"). The result is MP4 with H.264 video and AAC audio, at a
// bitrate worked out from the length so the file stays under the limit, and
// a resolution and frame rate that bitrate can carry.

#include <QByteArray>
#include <QString>

#include <optional>

namespace Qord {
namespace Video {

struct ShrinkPlan
{
    int width = 0;
    int height = 0;
    double fps = 30;
    qint64 videoBitrate = 0; // bits per second
    qint64 audioBitrate = 0;
};

// The resolution, frame rate and bitrates for a video of this size and
// length to come out under `limitBytes`; nullopt when even the smallest
// would not fit (the video is too long)
std::optional<ShrinkPlan> planShrink(int width, int height, double fps, double seconds, bool hasAudio,
                                     qint64 limitBytes);

// Re-encodes the video file at `path`. Nothing when it can't be read, no
// encoder works, or it can't be made small enough; `error` then says why.
std::optional<QByteArray> shrinkVideo(const QString &path, qint64 limitBytes, QString *error = nullptr);

} // namespace Video
} // namespace Qord
