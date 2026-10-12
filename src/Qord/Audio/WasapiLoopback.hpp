#pragma once

// Windows process loopback (Windows 10 2004 and later): the audio of one
// program and its children, or of everything except one program, as 48 kHz
// stereo 16-bit PCM. Go Live uses it as Discord does: a shared window
// streams that program's audio, a shared screen streams everything but
// Qord, so the call is never sent back into the stream. No Qt here.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace Qord {
namespace Audio {

class WasapiLoopback
{
public:
    // Interleaved stereo samples, from the capture thread
    using Callback = std::function<void(const int16_t *samples, size_t frames)>;

    WasapiLoopback();
    ~WasapiLoopback();
    WasapiLoopback(const WasapiLoopback &) = delete;
    WasapiLoopback &operator=(const WasapiLoopback &) = delete;

    // includeTree: only processId and its children; otherwise everything
    // but them. False (with error) when Windows can't do it.
    bool start(uint32_t processId, bool includeTree, Callback callback, std::string *error);
    void stop();

    struct Private;

private:
    std::unique_ptr<Private> d;
};

} // namespace Audio
} // namespace Qord
