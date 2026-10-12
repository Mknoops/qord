#pragma once

// Go Live's audio: what the computer plays, captured and Opus-encoded
// (48 kHz stereo, 128 kbit/s, music) for the stream's voice connection.
// Windows: process loopback, so a shared window sends that program's sound
// and a shared screen sends everything except Qord (never the call itself).
// Linux: the default output's PulseAudio/PipeWire monitor, which also holds
// Qord's own sound, so the picker leaves audio off there by default.

#include <QByteArray>
#include <QObject>
#include <QString>

#include <cstdint>
#include <functional>
#include <memory>

namespace Qord {
namespace Audio {

// Collects any number of samples into 20 ms frames (960 stereo s16)
class FrameAssembler
{
public:
    void push(const int16_t *samples, size_t frames, const std::function<void(const QByteArray &)> &frame);
    void reset() { pending.clear(); }

private:
    QByteArray pending;
};

struct LoopbackTarget
{
    // 0: everything (except Qord where the system allows it)
    quint32 processId = 0;
};

class StreamAudio : public QObject
{
    Q_OBJECT
public:
    static constexpr int BitrateBps = 128000;

    // Whether this system can capture what it plays at all
    static bool isSupported();
    // Whether the capture leaves Qord's own sound (the call) out
    static bool excludesOwnAudio();

    explicit StreamAudio(QObject *parent = nullptr);
    ~StreamAudio() override;

    bool start(const LoopbackTarget &target);
    void stop();
    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] QString errorString() const;

    // Sets up the Opus encoder (start() does this first); with it, samples
    // pushed here come out as opusFrame. Public for tests.
    bool prepareEncoder();
    void pushSamples(const int16_t *samples, size_t frames);

    struct Private;

signals:
    // One 20 ms Opus packet, emitted from the capture thread
    void opusFrame(const QByteArray &opus);

private:
    std::unique_ptr<Private> d;
};

} // namespace Audio
} // namespace Qord
