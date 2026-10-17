#pragma once

#include <QByteArray>
#include <QImage>
#include <QList>

namespace Qord {
namespace Plugins {
namespace ApngToGif {

// One frame of an animated PNG, as upstream's parseAPNG() (src/utils/apng.ts)
// reads it
struct Frame
{
    int left = 0;
    int top = 0;
    int width = 0;
    int height = 0;
    double delayMs = 100;
    int disposeOp = 0; // 0 none, 1 background, 2 previous
    int blendOp = 0; // 0 source, 1 over
    QImage image;
};

struct Animation
{
    int width = 0;
    int height = 0;
    QList<Frame> frames;
};

// Empty when the data is not an animated PNG
Animation parse(const QByteArray &png);

// FakeNitro's sendAnimatedSticker(): every frame drawn onto a square canvas
// of `resolution` pixels and written as a looping GIF. Empty on failure.
QByteArray convert(const QByteArray &apng, int resolution);

// A looping GIF from same-sized frames (pixels with alpha below 128 are
// transparent) and their delays in milliseconds
QByteArray encodeGif(const QList<QImage> &frames, const QList<double> &delaysMs);

// Every frame scaled to `size` (smooth), several at a time
QList<QImage> scaledFrames(const QList<QImage> &frames, const QSize &size);

} // namespace ApngToGif
} // namespace Plugins
} // namespace Qord
