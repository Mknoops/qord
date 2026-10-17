#include "NotificationSound.hpp"

#include "Qord/Preferences.hpp"

#include <QApplication>
#include <QSettings>
#include <QTimer>

#include <atomic>
#include <cmath>
#include <vector>

#ifdef ACHERON_HAVE_MINIAUDIO
#include "Core/Audio/Miniaudio.hpp"
#endif

namespace Acheron {
namespace UI {

namespace {
constexpr const char *EnabledKey = "notifications/sound";
constexpr double Pi = 3.14159265358979323846;
constexpr int SampleRate = 48000;

// Two soft notes (E6 then A6) with quick attack and exponential decay
std::vector<float> chime()
{
    std::vector<float> samples;
    const auto note = [&samples](double frequency, double seconds, double volume) {
        const int count = int(seconds * SampleRate);
        for (int i = 0; i < count; ++i) {
            const double t = double(i) / SampleRate;
            const double attack = t < 0.004 ? t / 0.004 : 1.0;
            const double envelope = attack * std::exp(-t * 18.0);
            const double tone = std::sin(2 * Pi * frequency * t) + 0.25 * std::sin(4 * Pi * frequency * t);
            samples.push_back(float(volume * envelope * tone));
        }
    };
    note(1318.5, 0.11, 0.22);
    note(1760.0, 0.22, 0.22);
    return samples;
}
} // namespace

#ifdef ACHERON_HAVE_MINIAUDIO
struct NotificationSound::Device
{
    ma_device device {};
    bool initialized = false;
    std::vector<float> samples = chime();
    std::atomic<size_t> position { 0 };
    std::atomic<float> gain { 1.0f }; // Settings > Notifications volume

    static void callback(ma_device *d, void *output, const void *, ma_uint32 frames)
    {
        auto *self = static_cast<Device *>(d->pUserData);
        auto *out = static_cast<float *>(output);
        size_t pos = self->position.load();
        const float gain = self->gain.load();
        for (ma_uint32 i = 0; i < frames; ++i)
            out[i] = pos < self->samples.size() ? gain * self->samples[pos++] : 0.0f;
        self->position.store(pos);
    }
};
#else
struct NotificationSound::Device
{
};
#endif

NotificationSound::NotificationSound(QObject *parent) : QObject(parent) {}

NotificationSound::~NotificationSound()
{
#ifdef ACHERON_HAVE_MINIAUDIO
    if (device && device->initialized)
        ma_device_uninit(&device->device);
#endif
    delete device;
}

bool NotificationSound::enabled()
{
    return QSettings().value(EnabledKey, true).toBool();
}

void NotificationSound::setEnabled(bool enabled)
{
    Qord::Preferences::set(Qord::Pref::NotifySound, enabled);
}

void NotificationSound::play()
{
#ifdef ACHERON_HAVE_MINIAUDIO
    if (!device) {
        device = new Device;
        ma_device_config config = ma_device_config_init(ma_device_type_playback);
        config.playback.format = ma_format_f32;
        config.playback.channels = 1;
        config.sampleRate = SampleRate;
        config.dataCallback = &Device::callback;
        config.pUserData = device;
        device->initialized = ma_device_init(nullptr, &config, &device->device) == MA_SUCCESS;
    }
    if (!device->initialized) {
        QApplication::beep();
        return;
    }
    device->gain.store(float(qBound(0, Qord::Preferences::number(Qord::Pref::NotifyVolume), 100)) / 100.0f);
    device->position.store(0);
    if (!ma_device_is_started(&device->device))
        ma_device_start(&device->device);
    // Release the output device once the chime has played
    const int ms = int(device->samples.size() * 1000 / SampleRate) + 300;
    QTimer::singleShot(ms, this, [this]() {
        if (device && device->initialized && device->position.load() >= device->samples.size())
            ma_device_stop(&device->device);
    });
#else
    QApplication::beep();
#endif
}

} // namespace UI
} // namespace Acheron
