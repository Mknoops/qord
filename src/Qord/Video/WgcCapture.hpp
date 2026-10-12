#pragma once

// Window capture with Windows.Graphics.Capture (Windows 10 1903+), the API
// Windows provides for screen sharing: the GPU copies the window's own
// surface, so it is fast, sees windows behind others and includes the
// cursor. Kept apart from Qt code because the C++/WinRT headers must come
// before Qt's keyword macros.

#include <QImage>

#include <memory>

namespace Qord {
namespace Video {

class WgcCapture
{
public:
    WgcCapture();
    ~WgcCapture();
    WgcCapture(const WgcCapture &) = delete;
    WgcCapture &operator=(const WgcCapture &) = delete;

    static bool isSupported();
    // `window` is an HWND
    bool start(quintptr window);
    void stop();
    // The newest frame (the previous one when nothing changed); null once the
    // window is closed
    QImage grab();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace Video
} // namespace Qord
