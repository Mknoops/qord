#pragma once

// Cameras for video calls. Windows reads them through Media Foundation (a
// source reader that converts to RGB32), Linux through V4L2 (YUYV or MJPEG).
// A reader thread keeps the latest picture; VideoSender takes it at its own
// frame rate. In mock mode (QORD_MOCK_DISCORD) a test-pattern camera is
// listed first, so calls can be tried on machines without one.

#include <QList>
#include <QSize>
#include <QString>

#include <memory>

#include "Qord/Video/FrameSource.hpp"

namespace Qord {
namespace Video {

struct CameraDevice
{
    QString id; // Windows: the symbolic link; Linux: the device path
    QString name;
};

// Can block for a moment while drivers answer; call it off the UI thread
// when you can
QList<CameraDevice> listCameras();

class CameraCapture : public FrameSource
{
public:
    // Opens the mode closest to `preferred` (not above it) at `fps` or more
    CameraCapture(const CameraDevice &device, const QSize &preferred, int fps);
    ~CameraCapture() override;
    CameraCapture(const CameraCapture &) = delete;
    CameraCapture &operator=(const CameraCapture &) = delete;

    bool start() override;
    void stop() override;
    QImage grab() override;
    [[nodiscard]] bool sourceGone() const override;
    [[nodiscard]] QString name() const override { return device.name; }

    struct Private;

private:
    CameraDevice device;
    QSize preferred;
    int fps;
    std::unique_ptr<Private> d;
};

// YUYV 4:2:2 (BT.601, limited range) to RGB32, as V4L2 cameras deliver it
QImage convertYuyv(const uchar *data, int width, int height, int stride);

// How well a camera mode suits a wanted size and frame rate; higher is
// better, negative means unusable
int scoreCameraMode(const QSize &mode, double fps, const QSize &wanted, int wantedFps);

} // namespace Video
} // namespace Qord
