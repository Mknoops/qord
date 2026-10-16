#pragma once

// Qord: a video too large to upload, re-encoded to fit (FakeNitro's "Shrink
// large videos"). The result is MP4 with H.264 video and AAC audio, at a
// bitrate worked out from the length so the file stays under the limit, and
// a resolution and frame rate that bitrate can carry.

#include <QByteArray>
#include <QString>

#include <functional>
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

// What to give up first when the bitrate is short
enum class Priority
{
    Balanced, // up to 30 FPS, then 15 below 240 lines
    Sharpness, // 15 FPS, so each frame gets more pixels
    Smoothness, // the source's frame rate up to 60 FPS, fewer lines instead
};

// The resolution, frame rate and bitrates for a video of this size and
// length to come out under `limitBytes`; nullopt when even the smallest
// would not fit (the video is too long)
std::optional<ShrinkPlan> planShrink(int width, int height, double fps, double seconds, bool hasAudio,
                                     qint64 limitBytes, Priority priority = Priority::Balanced);

// How many seconds of video fit in `limitBytes` at the least planShrink takes
double secondsThatFit(bool hasAudio, qint64 limitBytes);

struct ShrinkOptions
{
    Priority priority = Priority::Balanced;
    double maxSeconds = 0; // only the start is kept; 0 keeps it all
    bool trimToFit = false; // a video too long to fit is cut to what does
    // Called now and then with how far along it is, 0 to 1; false stops it
    std::function<bool(double fraction)> progress;
};

struct ShrinkResult
{
    QByteArray data;
    double seconds = 0; // how long the result is
    bool trimmed = false;
};

// Re-encodes the video file at `path`. Nothing when it can't be read, no
// encoder works, it can't be made small enough or it was stopped; `error`
// then says why.
std::optional<ShrinkResult> shrinkVideo(const QString &path, qint64 limitBytes, const ShrinkOptions &options,
                                        QString *error = nullptr);
std::optional<QByteArray> shrinkVideo(const QString &path, qint64 limitBytes, QString *error = nullptr);

} // namespace Video
} // namespace Qord
