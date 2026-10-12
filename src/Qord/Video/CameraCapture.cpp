#include "CameraCapture.hpp"

#include "Core/Logging.hpp"
#include "Qord/DevMock.hpp"

#include <QDateTime>
#include <QPainter>

#include <atomic>
#include <cmath>
#include <limits>
#include <mutex>
#include <thread>

#if defined(Q_OS_WIN)
#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#elif defined(Q_OS_LINUX)
#include <QDir>
#include <cerrno>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace Qord {
namespace Video {

namespace {
const QString MockCameraId = QStringLiteral("qord-mock-camera");

inline uchar clamp8(int v)
{
    return uchar(v < 0 ? 0 : v > 255 ? 255 : v);
}
} // namespace

QImage convertYuyv(const uchar *data, int width, int height, int stride)
{
    QImage out(width, height, QImage::Format_RGB32);
    for (int y = 0; y < height; ++y) {
        const uchar *in = data + qsizetype(y) * stride;
        auto *line = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x + 1 < width; x += 2, in += 4) {
            const int u = in[1] - 128;
            const int v = in[3] - 128;
            const int r = 409 * v + 128;
            const int g = -100 * u - 208 * v + 128;
            const int b = 516 * u + 128;
            for (int i = 0; i < 2; ++i) {
                const int c = 298 * (in[i * 2] - 16);
                line[x + i] = qRgb(clamp8((c + r) >> 8), clamp8((c + g) >> 8), clamp8((c + b) >> 8));
            }
        }
    }
    return out;
}

int scoreCameraMode(const QSize &mode, double fps, const QSize &wanted, int wantedFps)
{
    if (mode.isEmpty() || fps < 10)
        return -1;
    const qint64 area = qint64(mode.width()) * mode.height();
    const qint64 wantedArea = qint64(wanted.width()) * wanted.height();
    // A mode that fits is worth twice its size; a bigger one gets scaled down,
    // which costs time, so it scores below the wanted size
    const qint64 base = mode.width() <= wanted.width() && mode.height() <= wanted.height()
            ? 2 * area
            : qMax<qint64>(1, wantedArea - (area - wantedArea) / 8);
    return int(qMin<qint64>(base * qMin(1.0, fps / qMax(1, wantedFps)), std::numeric_limits<int>::max()));
}

// The state a reader thread shares with the sender
struct CameraCapture::Private
{
    std::thread reader;
    std::atomic<bool> running { false };
    std::atomic<bool> gone { false };
    std::mutex mutex;
    QImage latest;
    qint64 mockStart = 0;

    void publish(const QImage &image)
    {
        std::lock_guard lock(mutex);
        latest = image;
    }

#if defined(Q_OS_WIN)
    IMFMediaSource *source = nullptr;
    IMFSourceReader *readerObject = nullptr;
    QSize size;
    LONG stride = 0;
    bool comInitialized = false;
    bool mfStarted = false;
#elif defined(Q_OS_LINUX)
    int fd = -1;
    struct Buffer
    {
        void *start = nullptr;
        size_t length = 0;
    };
    std::vector<Buffer> buffers;
    quint32 pixelFormat = 0;
    QSize size;
    int bytesPerLine = 0;
#endif
};

namespace {

QImage mockPicture(qint64 ms, const QSize &size)
{
    // A moving test pattern with the time, so frozen or dropped frames show
    QImage image(size, QImage::Format_RGB32);
    QPainter p(&image);
    QLinearGradient bg(0, 0, size.width(), size.height());
    bg.setColorAt(0, QColor(0x31, 0x33, 0x8a));
    bg.setColorAt(1, QColor(0x12, 0x6e, 0x6a));
    p.fillRect(image.rect(), bg);
    p.setRenderHint(QPainter::Antialiasing);
    const double t = ms / 1000.0;
    const double x = (0.5 + 0.4 * std::sin(t * 1.3)) * size.width();
    const double y = (0.5 + 0.35 * std::cos(t * 0.9)) * size.height();
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xfe, 0xe7, 0x5c));
    p.drawEllipse(QPointF(x, y), size.height() / 8.0, size.height() / 8.0);
    QFont font = p.font();
    font.setPixelSize(size.height() / 12);
    font.setBold(true);
    p.setFont(font);
    p.setPen(Qt::white);
    p.drawText(image.rect().adjusted(16, 16, -16, -16), Qt::AlignLeft | Qt::AlignTop,
               QStringLiteral("Qord mock camera\n%1").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz"))));
    return image;
}

#if defined(Q_OS_WIN)

template<typename T>
void release(T *&p)
{
    if (p) {
        p->Release();
        p = nullptr;
    }
}

struct MfSession
{
    bool com = false;
    bool mf = false;
    MfSession()
    {
        com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
        mf = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE));
    }
    ~MfSession()
    {
        if (mf)
            MFShutdown();
        if (com)
            CoUninitialize();
    }
};

QString mfString(IMFActivate *activate, const GUID &key)
{
    WCHAR *text = nullptr;
    UINT32 length = 0;
    if (FAILED(activate->GetAllocatedString(key, &text, &length)))
        return {};
    const QString s = QString::fromWCharArray(text, int(length));
    CoTaskMemFree(text);
    return s;
}

QList<CameraDevice> listMfCameras(IMFActivate ***out = nullptr, UINT32 *outCount = nullptr)
{
    QList<CameraDevice> list;
    IMFAttributes *attributes = nullptr;
    if (FAILED(MFCreateAttributes(&attributes, 1)))
        return list;
    attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    IMFActivate **devices = nullptr;
    UINT32 count = 0;
    if (SUCCEEDED(MFEnumDeviceSources(attributes, &devices, &count))) {
        for (UINT32 i = 0; i < count; ++i)
            list.append({ mfString(devices[i], MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK),
                          mfString(devices[i], MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME) });
        if (out) {
            *out = devices;
            *outCount = count;
        } else {
            for (UINT32 i = 0; i < count; ++i)
                devices[i]->Release();
            CoTaskMemFree(devices);
        }
    }
    attributes->Release();
    return list;
}

bool startMf(CameraCapture::Private *d, const CameraDevice &device, const QSize &preferred, int fps)
{
    IMFActivate **devices = nullptr;
    UINT32 count = 0;
    const QList<CameraDevice> found = listMfCameras(&devices, &count);
    HRESULT hr = E_FAIL;
    for (UINT32 i = 0; i < count; ++i) {
        if (!d->source && found.value(int(i)).id == device.id)
            hr = devices[i]->ActivateObject(IID_PPV_ARGS(&d->source));
        devices[i]->Release();
    }
    CoTaskMemFree(devices);
    if (FAILED(hr) || !d->source) {
        qCWarning(LogVoice) << "Camera: couldn't open" << device.name << Qt::hex << ulong(hr);
        return false;
    }

    IMFAttributes *attributes = nullptr;
    MFCreateAttributes(&attributes, 1);
    // Lets the reader decode MJPEG and convert to RGB32 for us
    attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    hr = MFCreateSourceReaderFromMediaSource(d->source, attributes, &d->readerObject);
    attributes->Release();
    if (FAILED(hr))
        return false;

    constexpr DWORD Stream = DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
    IMFMediaType *best = nullptr;
    int bestScore = -1;
    for (DWORD i = 0;; ++i) {
        IMFMediaType *type = nullptr;
        if (FAILED(d->readerObject->GetNativeMediaType(Stream, i, &type)))
            break;
        UINT32 w = 0, h = 0, num = 0, den = 1;
        MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &w, &h);
        MFGetAttributeRatio(type, MF_MT_FRAME_RATE, &num, &den);
        const int score = scoreCameraMode(QSize(int(w), int(h)), den ? double(num) / den : 0, preferred, fps);
        if (score > bestScore) {
            release(best);
            best = type;
            bestScore = score;
        } else {
            type->Release();
        }
    }
    if (best) {
        d->readerObject->SetCurrentMediaType(Stream, nullptr, best);
        best->Release();
    }

    IMFMediaType *rgb = nullptr;
    MFCreateMediaType(&rgb);
    rgb->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    rgb->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    hr = d->readerObject->SetCurrentMediaType(Stream, nullptr, rgb);
    rgb->Release();
    if (FAILED(hr)) {
        qCWarning(LogVoice) << "Camera: no RGB32 output from" << device.name << Qt::hex << ulong(hr);
        return false;
    }
    IMFMediaType *current = nullptr;
    if (FAILED(d->readerObject->GetCurrentMediaType(Stream, &current)))
        return false;
    UINT32 w = 0, h = 0;
    MFGetAttributeSize(current, MF_MT_FRAME_SIZE, &w, &h);
    d->size = QSize(int(w), int(h));
    UINT32 stride = 0;
    if (SUCCEEDED(current->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride)))
        d->stride = LONG(stride);
    else
        d->stride = LONG(w * 4);
    current->Release();
    qCInfo(LogVoice) << "Camera:" << device.name << d->size;
    return !d->size.isEmpty();
}

void readMf(CameraCapture::Private *d)
{
    MfSession session;
    constexpr DWORD Stream = DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
    while (d->running) {
        DWORD flags = 0;
        LONGLONG time = 0;
        IMFSample *sample = nullptr;
        const HRESULT hr = d->readerObject->ReadSample(Stream, 0, nullptr, &flags, &time, &sample);
        if (FAILED(hr) || (flags & (MF_SOURCE_READERF_ERROR | MF_SOURCE_READERF_ENDOFSTREAM))) {
            release(sample);
            if (d->running)
                d->gone = true;
            break;
        }
        if (!sample)
            continue;
        IMFMediaBuffer *buffer = nullptr;
        if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer))) {
            BYTE *data = nullptr;
            DWORD length = 0;
            if (SUCCEEDED(buffer->Lock(&data, nullptr, &length))) {
                const int w = d->size.width(), h = d->size.height();
                const LONG stride = d->stride;
                const qsizetype needed = qsizetype(qAbs(stride)) * h;
                if (qsizetype(length) >= needed) {
                    QImage image(w, h, QImage::Format_RGB32);
                    // A negative stride is a bottom-up picture
                    const BYTE *first = stride < 0 ? data + qsizetype(-stride) * (h - 1) : data;
                    for (int y = 0; y < h; ++y)
                        memcpy(image.scanLine(y), first + qsizetype(stride) * y, size_t(w) * 4);
                    d->publish(image);
                }
                buffer->Unlock();
            }
            buffer->Release();
        }
        sample->Release();
    }
}

#elif defined(Q_OS_LINUX)

int xioctl(int fd, unsigned long request, void *arg)
{
    int r;
    do
        r = ioctl(fd, request, arg);
    while (r == -1 && errno == EINTR);
    return r;
}

bool isCaptureDevice(int fd, QString *name)
{
    v4l2_capability cap {};
    if (xioctl(fd, VIDIOC_QUERYCAP, &cap) != 0)
        return false;
    const quint32 caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps : cap.capabilities;
    if (!(caps & V4L2_CAP_VIDEO_CAPTURE) || !(caps & V4L2_CAP_STREAMING))
        return false;
    if (name)
        *name = QString::fromUtf8(reinterpret_cast<const char *>(cap.card));
    return true;
}

QList<CameraDevice> listV4l2Cameras()
{
    QList<CameraDevice> list;
    const QStringList nodes = QDir(QStringLiteral("/dev")).entryList({ QStringLiteral("video*") }, QDir::System,
                                                                       QDir::Name);
    for (const QString &node : nodes) {
        const QString path = QStringLiteral("/dev/") + node;
        const int fd = ::open(path.toLocal8Bit().constData(), O_RDWR | O_NONBLOCK);
        if (fd < 0)
            continue;
        QString name;
        if (isCaptureDevice(fd, &name))
            list.append({ path, name });
        ::close(fd);
    }
    return list;
}

void closeV4l2(CameraCapture::Private *d)
{
    if (d->fd < 0)
        return;
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    xioctl(d->fd, VIDIOC_STREAMOFF, &type);
    for (const auto &b : d->buffers)
        if (b.start && b.start != MAP_FAILED)
            munmap(b.start, b.length);
    d->buffers.clear();
    ::close(d->fd);
    d->fd = -1;
}

bool startV4l2(CameraCapture::Private *d, const CameraDevice &device, const QSize &preferred, int fps)
{
    d->fd = ::open(device.id.toLocal8Bit().constData(), O_RDWR | O_NONBLOCK);
    if (d->fd < 0 || !isCaptureDevice(d->fd, nullptr)) {
        qCWarning(LogVoice) << "Camera: couldn't open" << device.id << strerror(errno);
        closeV4l2(d);
        return false;
    }
    // YUYV is converted here; MJPEG (what most cameras send above 480p at
    // 30 fps) is decoded by Qt's JPEG reader. Score each mode of each.
    quint32 bestFormat = 0;
    QSize bestSize;
    int bestScore = -1;
    for (quint32 format : { quint32(V4L2_PIX_FMT_YUYV), quint32(V4L2_PIX_FMT_MJPEG) }) {
        v4l2_frmsizeenum sizes {};
        sizes.pixel_format = format;
        for (sizes.index = 0; xioctl(d->fd, VIDIOC_ENUM_FRAMESIZES, &sizes) == 0; ++sizes.index) {
            if (sizes.type != V4L2_FRMSIZE_TYPE_DISCRETE) {
                // Stepwise: ask for the wanted size and let the driver adjust
                if (bestScore < 0) {
                    bestFormat = format;
                    bestSize = preferred;
                    bestScore = 0;
                }
                break;
            }
            const QSize size(int(sizes.discrete.width), int(sizes.discrete.height));
            double best = 0;
            v4l2_frmivalenum intervals {};
            intervals.pixel_format = format;
            intervals.width = sizes.discrete.width;
            intervals.height = sizes.discrete.height;
            for (intervals.index = 0; xioctl(d->fd, VIDIOC_ENUM_FRAMEINTERVALS, &intervals) == 0; ++intervals.index) {
                if (intervals.type == V4L2_FRMIVAL_TYPE_DISCRETE && intervals.discrete.numerator)
                    best = qMax(best, double(intervals.discrete.denominator) / intervals.discrete.numerator);
                else
                    best = qMax(best, double(fps));
            }
            // YUYV costs less to use than JPEG at the same score
            const int score = scoreCameraMode(size, best, preferred, fps) + (format == V4L2_PIX_FMT_YUYV ? 1 : 0);
            if (score > bestScore) {
                bestScore = score;
                bestFormat = format;
                bestSize = size;
            }
        }
    }
    if (!bestFormat) {
        qCWarning(LogVoice) << "Camera:" << device.name << "offers neither YUYV nor MJPEG";
        closeV4l2(d);
        return false;
    }

    v4l2_format format {};
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width = quint32(bestSize.width());
    format.fmt.pix.height = quint32(bestSize.height());
    format.fmt.pix.pixelformat = bestFormat;
    format.fmt.pix.field = V4L2_FIELD_ANY;
    if (xioctl(d->fd, VIDIOC_S_FMT, &format) != 0) {
        qCWarning(LogVoice) << "Camera: VIDIOC_S_FMT failed" << strerror(errno);
        closeV4l2(d);
        return false;
    }
    d->pixelFormat = format.fmt.pix.pixelformat;
    d->size = QSize(int(format.fmt.pix.width), int(format.fmt.pix.height));
    d->bytesPerLine = int(format.fmt.pix.bytesperline ? format.fmt.pix.bytesperline : format.fmt.pix.width * 2);

    v4l2_streamparm parm {};
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = quint32(fps);
    xioctl(d->fd, VIDIOC_S_PARM, &parm); // best effort

    v4l2_requestbuffers request {};
    request.count = 4;
    request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    request.memory = V4L2_MEMORY_MMAP;
    if (xioctl(d->fd, VIDIOC_REQBUFS, &request) != 0 || request.count < 2) {
        closeV4l2(d);
        return false;
    }
    for (quint32 i = 0; i < request.count; ++i) {
        v4l2_buffer buffer {};
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = i;
        if (xioctl(d->fd, VIDIOC_QUERYBUF, &buffer) != 0) {
            closeV4l2(d);
            return false;
        }
        void *start = mmap(nullptr, buffer.length, PROT_READ | PROT_WRITE, MAP_SHARED, d->fd, buffer.m.offset);
        d->buffers.push_back({ start, buffer.length });
        if (start == MAP_FAILED || xioctl(d->fd, VIDIOC_QBUF, &buffer) != 0) {
            closeV4l2(d);
            return false;
        }
    }
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(d->fd, VIDIOC_STREAMON, &type) != 0) {
        closeV4l2(d);
        return false;
    }
    qCInfo(LogVoice) << "Camera:" << device.name << d->size << (d->pixelFormat == V4L2_PIX_FMT_MJPEG ? "MJPEG" : "YUYV");
    return true;
}

void readV4l2(CameraCapture::Private *d)
{
    while (d->running) {
        pollfd p { d->fd, POLLIN, 0 };
        const int r = poll(&p, 1, 200);
        if (r < 0 && errno == EINTR)
            continue;
        if (r < 0 || (p.revents & (POLLERR | POLLHUP | POLLNVAL))) {
            d->gone = true;
            break;
        }
        if (r == 0)
            continue;
        v4l2_buffer buffer {};
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        if (xioctl(d->fd, VIDIOC_DQBUF, &buffer) != 0) {
            if (errno == EAGAIN)
                continue;
            d->gone = true; // ENODEV: unplugged
            break;
        }
        const auto *data = static_cast<const uchar *>(d->buffers[buffer.index].start);
        QImage image;
        if (d->pixelFormat == V4L2_PIX_FMT_MJPEG) {
            image = QImage::fromData(data, int(buffer.bytesused), "JPEG");
            if (!image.isNull())
                image = image.convertToFormat(QImage::Format_RGB32);
        } else if (buffer.bytesused >= quint32(d->bytesPerLine * d->size.height())) {
            image = convertYuyv(data, d->size.width(), d->size.height(), d->bytesPerLine);
        }
        xioctl(d->fd, VIDIOC_QBUF, &buffer);
        if (!image.isNull())
            d->publish(image);
    }
}

#endif

} // namespace

QList<CameraDevice> listCameras()
{
    QList<CameraDevice> list;
    if (Qord::DevMock::enabled())
        list.append({ MockCameraId, QStringLiteral("Qord Mock Camera") });
#if defined(Q_OS_WIN)
    MfSession session;
    list += listMfCameras();
#elif defined(Q_OS_LINUX)
    list += listV4l2Cameras();
#endif
    return list;
}

CameraCapture::CameraCapture(const CameraDevice &device, const QSize &preferred, int fps)
    : device(device), preferred(preferred), fps(fps), d(std::make_unique<Private>())
{
}

CameraCapture::~CameraCapture()
{
    stop();
}

bool CameraCapture::start()
{
    stop();
    d->gone = false;
    d->latest = QImage();
    if (device.id == MockCameraId) {
        if (!Qord::DevMock::enabled())
            return false;
        d->mockStart = QDateTime::currentMSecsSinceEpoch();
        d->running = true;
        return true;
    }
#if defined(Q_OS_WIN)
    d->comInitialized = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    d->mfStarted = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE));
    if (!startMf(d.get(), device, preferred, fps)) {
        stop();
        return false;
    }
    d->running = true;
    d->reader = std::thread(readMf, d.get());
    return true;
#elif defined(Q_OS_LINUX)
    if (!startV4l2(d.get(), device, preferred, fps))
        return false;
    d->running = true;
    d->reader = std::thread(readV4l2, d.get());
    return true;
#else
    return false;
#endif
}

void CameraCapture::stop()
{
    const bool wasMock = device.id == MockCameraId;
    d->running = false;
    if (d->reader.joinable())
        d->reader.join();
    if (wasMock)
        return;
#if defined(Q_OS_WIN)
    release(d->readerObject);
    if (d->source) {
        d->source->Shutdown();
        release(d->source);
    }
    if (d->mfStarted) {
        MFShutdown();
        d->mfStarted = false;
    }
    if (d->comInitialized) {
        CoUninitialize();
        d->comInitialized = false;
    }
#elif defined(Q_OS_LINUX)
    closeV4l2(d.get());
#endif
}

QImage CameraCapture::grab()
{
    if (device.id == MockCameraId)
        return d->running ? mockPicture(QDateTime::currentMSecsSinceEpoch() - d->mockStart, QSize(1280, 720))
                          : QImage();
    if (d->gone)
        return {};
    std::lock_guard lock(d->mutex);
    return d->latest;
}

bool CameraCapture::sourceGone() const
{
    return d->gone;
}

} // namespace Video
} // namespace Qord
