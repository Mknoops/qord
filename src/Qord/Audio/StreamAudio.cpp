#include "StreamAudio.hpp"

#include "Core/Audio/IAudioBackend.hpp"
#include "Core/Audio/OpusEncoder.hpp"
#include "Core/Logging.hpp"

#include <mutex>

#if defined(Q_OS_WIN)
#include "Qord/Audio/WasapiLoopback.hpp"
#include <windows.h>
#elif defined(Q_OS_LINUX)
#include "Core/Audio/Miniaudio.hpp"
#include <cstring>
#endif

namespace Qord {
namespace Audio {

using Acheron::Core::Audio::AUDIO_CHANNELS;
using Acheron::Core::Audio::AUDIO_FRAME_SIZE;
using Acheron::Core::Audio::AUDIO_SAMPLE_RATE;

void FrameAssembler::push(const int16_t *samples, size_t frames,
                          const std::function<void(const QByteArray &)> &frame)
{
    pending.append(reinterpret_cast<const char *>(samples), qsizetype(frames * AUDIO_CHANNELS * sizeof(int16_t)));
    qsizetype offset = 0;
    while (pending.size() - offset >= AUDIO_FRAME_SIZE) {
        frame(pending.mid(offset, AUDIO_FRAME_SIZE));
        offset += AUDIO_FRAME_SIZE;
    }
    pending.remove(0, offset);
}

struct StreamAudio::Private
{
    std::mutex mutex; // the encoder, between the capture thread and stop()
    std::unique_ptr<Acheron::Core::Audio::OpusEncoder> encoder;
    FrameAssembler assembler;
    bool running = false;
    QString error;
#if defined(Q_OS_WIN)
    WasapiLoopback loopback;
#elif defined(Q_OS_LINUX)
    ma_context context;
    ma_device device;
    bool contextReady = false;
    bool deviceReady = false;
#endif
};

#if defined(Q_OS_LINUX)
namespace {
void onCaptured(ma_device *device, void *, const void *input, ma_uint32 frames)
{
    auto *self = static_cast<StreamAudio *>(device->pUserData);
    if (input)
        self->pushSamples(static_cast<const int16_t *>(input), frames);
}
} // namespace
#endif

bool StreamAudio::isSupported()
{
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
    return true;
#else
    return false;
#endif
}

bool StreamAudio::excludesOwnAudio()
{
#if defined(Q_OS_WIN)
    return true;
#else
    return false;
#endif
}

StreamAudio::StreamAudio(QObject *parent) : QObject(parent), d(std::make_unique<Private>()) {}

StreamAudio::~StreamAudio()
{
    stop();
}

bool StreamAudio::isRunning() const
{
    return d->running;
}

QString StreamAudio::errorString() const
{
    return d->error;
}

void StreamAudio::pushSamples(const int16_t *samples, size_t frames)
{
    std::lock_guard lock(d->mutex);
    if (!d->encoder)
        return;
    d->assembler.push(samples, frames, [this](const QByteArray &pcm) {
        const QByteArray opus = d->encoder->encode(pcm);
        // With DTX, 1-2 byte packets mean "nothing to send"
        if (opus.size() > 2)
            emit opusFrame(opus);
    });
}

bool StreamAudio::prepareEncoder()
{
    d->error.clear();
    auto encoder = std::make_unique<Acheron::Core::Audio::OpusEncoder>();
    if (!encoder->init(AUDIO_SAMPLE_RATE, AUDIO_CHANNELS, OPUS_APPLICATION_AUDIO)) {
        d->error = tr("The Opus encoder failed");
        return false;
    }
    encoder->setBitrate(BitrateBps);
    encoder->setComplexity(8);
    encoder->setSignalType(OPUS_SIGNAL_MUSIC);
    // Silence costs next to nothing with DTX
    encoder->setDtx(true);
    std::lock_guard lock(d->mutex);
    d->encoder = std::move(encoder);
    d->assembler.reset();
    return true;
}

bool StreamAudio::start(const LoopbackTarget &target)
{
    stop();
    if (!prepareEncoder())
        return false;

#if defined(Q_OS_WIN)
    // A window: that program and its children; a screen: everything but us
    const bool onlyTarget = target.processId != 0;
    const quint32 pid = onlyTarget ? target.processId : GetCurrentProcessId();
    std::string error;
    if (!d->loopback.start(pid, onlyTarget, [this](const int16_t *s, size_t n) { pushSamples(s, n); }, &error)) {
        d->error = tr("Windows can't capture the sound (%1). Stream audio needs Windows 10 2004 or later.")
                           .arg(QString::fromStdString(error));
        stop();
        return false;
    }
#elif defined(Q_OS_LINUX)
    Q_UNUSED(target);
    // PulseAudio (and PipeWire's pulse server) name the default output's
    // monitor "@DEFAULT_MONITOR@"
    const ma_backend backends[] = { ma_backend_pulseaudio };
    if (ma_context_init(backends, 1, nullptr, &d->context) != MA_SUCCESS) {
        d->error = tr("Stream audio needs PulseAudio or PipeWire");
        stop();
        return false;
    }
    d->contextReady = true;
    ma_device_id monitor {};
    std::strncpy(monitor.pulse, "@DEFAULT_MONITOR@", sizeof(monitor.pulse) - 1);
    ma_device_config config = ma_device_config_init(ma_device_type_capture);
    config.capture.pDeviceID = &monitor;
    config.capture.format = ma_format_s16;
    config.capture.channels = AUDIO_CHANNELS;
    config.sampleRate = AUDIO_SAMPLE_RATE;
    config.periodSizeInMilliseconds = 20;
    config.dataCallback = onCaptured;
    config.pUserData = this;
    ma_result result = ma_device_init(&d->context, &config, &d->device);
    if (result != MA_SUCCESS) {
        d->error = tr("Couldn't open the sound output's monitor (%1)").arg(QString::fromUtf8(ma_result_description(result)));
        stop();
        return false;
    }
    d->deviceReady = true;
    if ((result = ma_device_start(&d->device)) != MA_SUCCESS) {
        d->error = tr("Couldn't start capturing sound (%1)").arg(QString::fromUtf8(ma_result_description(result)));
        stop();
        return false;
    }
#else
    Q_UNUSED(target);
    d->error = tr("Stream audio isn't available on this system");
    stop();
    return false;
#endif
    d->running = true;
    qCInfo(LogVoice) << "Go Live: capturing stream audio"
                     << (target.processId ? QStringLiteral("of process %1").arg(target.processId) : QStringLiteral("of everything"));
    return true;
}

void StreamAudio::stop()
{
    // The capture threads are joined first, so no callback is still encoding
#if defined(Q_OS_WIN)
    d->loopback.stop();
#elif defined(Q_OS_LINUX)
    if (d->deviceReady)
        ma_device_uninit(&d->device);
    d->deviceReady = false;
    if (d->contextReady)
        ma_context_uninit(&d->context);
    d->contextReady = false;
#endif
    std::lock_guard lock(d->mutex);
    d->encoder.reset();
    d->running = false;
}

} // namespace Audio
} // namespace Qord
