#pragma once

// Screens and windows to share (Go Live). On Windows, screens are captured
// with DXGI desktop duplication (falling back to GDI) and windows with
// PrintWindow, which also sees GPU-drawn windows; the cursor is drawn in.
// Elsewhere only screens are offered, through QScreen::grabWindow.

#include <QImage>
#include <QList>
#include <QSize>
#include <QString>

#include <memory>

namespace Qord {
namespace Video {

struct CaptureSource
{
    enum class Kind { Screen, Window };
    Kind kind = Kind::Screen;
    QString name;
    QImage thumbnail;
    QSize size;
    quintptr window = 0; // HWND for windows
    int screenIndex = 0; // the n-th monitor
    QString processName; // for windows: the program, as Discord shows it
};

// Screens first, then the visible windows of other programs
QList<CaptureSource> listCaptureSources(const QSize &thumbnailSize, bool includeWindows = true);

class ScreenCapturer
{
public:
    ScreenCapturer();
    ~ScreenCapturer();
    ScreenCapturer(const ScreenCapturer &) = delete;
    ScreenCapturer &operator=(const ScreenCapturer &) = delete;

    bool start(const CaptureSource &source);
    void stop();
    // The latest picture (the previous one again when nothing changed); null
    // when the source is gone (a closed window)
    QImage grab();
    [[nodiscard]] bool isRunning() const;
    // The shared window was closed
    [[nodiscard]] bool sourceGone() const;
    void setDrawCursor(bool draw) { drawCursor = draw; }

private:
    struct Private;
    std::unique_ptr<Private> d;
    bool drawCursor = true;
};

} // namespace Video
} // namespace Qord
