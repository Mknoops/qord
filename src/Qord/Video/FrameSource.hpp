#pragma once

// Where VideoSender gets its pictures: a screen or window (Go Live) or a
// camera. start(), grab() and stop() all run on the sender's thread.

#include <QImage>
#include <QString>

namespace Qord {
namespace Video {

class FrameSource
{
public:
    virtual ~FrameSource() = default;

    virtual bool start() = 0;
    virtual void stop() = 0;
    // The latest picture (the previous one again when nothing changed); null
    // while there is none yet, or once the source is gone
    virtual QImage grab() = 0;
    // The source went away (a closed window, an unplugged camera)
    [[nodiscard]] virtual bool sourceGone() const = 0;
    [[nodiscard]] virtual QString name() const = 0;
};

} // namespace Video
} // namespace Qord
