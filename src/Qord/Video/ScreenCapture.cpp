#include "ScreenCapture.hpp"

#include <QGuiApplication>
#include <QHash>
#include <QPainter>
#include <QPixmap>
#include <QScreen>

#include <algorithm>
#include <cstring>

#ifdef Q_OS_WIN
#include "WgcCapture.hpp"

#include <windows.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#endif

namespace Qord {
namespace Video {

#ifdef Q_OS_WIN
namespace {

// A 32-bit top-down DIB section and a DC on it
struct Dib
{
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previous = nullptr;
    void *bits = nullptr;
    int width = 0;
    int height = 0;

    bool create(int w, int h)
    {
        release();
        BITMAPINFO info {};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = w;
        info.bmiHeader.biHeight = -h;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        dc = CreateCompatibleDC(nullptr);
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!dc || !bitmap) {
            release();
            return false;
        }
        previous = SelectObject(dc, bitmap);
        width = w;
        height = h;
        return true;
    }
    void release()
    {
        if (dc && previous)
            SelectObject(dc, previous);
        if (bitmap)
            DeleteObject(bitmap);
        if (dc)
            DeleteDC(dc);
        dc = nullptr;
        bitmap = nullptr;
        previous = nullptr;
        bits = nullptr;
    }
    QImage image() const
    {
        // Copy, opaque: GDI leaves alpha undefined
        QImage out(static_cast<const uchar *>(bits), width, height, width * 4, QImage::Format_RGB32);
        return out.copy();
    }
    ~Dib() { release(); }
};

QList<HMONITOR> monitors()
{
    QList<HMONITOR> list;
    EnumDisplayMonitors(
            nullptr, nullptr,
            [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
                reinterpret_cast<QList<HMONITOR> *>(data)->append(monitor);
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&list));
    // Primary first, as Discord lists them
    std::stable_sort(list.begin(), list.end(), [](HMONITOR a, HMONITOR b) {
        MONITORINFO ia { sizeof(MONITORINFO) }, ib { sizeof(MONITORINFO) };
        GetMonitorInfoW(a, &ia);
        GetMonitorInfoW(b, &ib);
        return (ia.dwFlags & MONITORINFOF_PRIMARY) && !(ib.dwFlags & MONITORINFOF_PRIMARY);
    });
    return list;
}

QRect monitorRect(HMONITOR monitor)
{
    MONITORINFO info { sizeof(MONITORINFO) };
    if (!GetMonitorInfoW(monitor, &info))
        return {};
    return QRect(info.rcMonitor.left, info.rcMonitor.top, info.rcMonitor.right - info.rcMonitor.left,
                 info.rcMonitor.bottom - info.rcMonitor.top);
}

QImage grabRectGdi(const QRect &rect)
{
    Dib dib;
    if (rect.isEmpty() || !dib.create(rect.width(), rect.height()))
        return {};
    HDC screen = GetDC(nullptr);
    BitBlt(dib.dc, 0, 0, rect.width(), rect.height(), screen, rect.x(), rect.y(), SRCCOPY | CAPTUREBLT);
    ReleaseDC(nullptr, screen);
    return dib.image();
}

// The visible part of a window (without the invisible resize borders)
QRect visibleWindowRect(HWND window)
{
    RECT r {};
    if (FAILED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r))))
        GetWindowRect(window, &r);
    return QRect(r.left, r.top, r.right - r.left, r.bottom - r.top);
}

QImage grabWindow(HWND window)
{
    if (!IsWindow(window) || IsIconic(window))
        return {};
    RECT full {};
    GetWindowRect(window, &full);
    const QRect visible = visibleWindowRect(window);
    const int w = full.right - full.left;
    const int h = full.bottom - full.top;
    Dib dib;
    if (w <= 0 || h <= 0 || !dib.create(w, h))
        return {};
    // PW_RENDERFULLCONTENT (2) also captures windows drawn with the GPU
    if (!PrintWindow(window, dib.dc, 2))
        return {};
    return dib.image().copy(visible.translated(-full.left, -full.top));
}

QString processNameOf(HWND window)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return {};
    wchar_t path[MAX_PATH];
    DWORD size = MAX_PATH;
    QString name;
    if (QueryFullProcessImageNameW(process, 0, path, &size))
        name = QString::fromWCharArray(path, int(size)).section(QLatin1Char('\\'), -1);
    CloseHandle(process);
    return name;
}

bool isShareableWindow(HWND window)
{
    if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER))
        return false;
    const LONG_PTR ex = GetWindowLongPtrW(window, GWL_EXSTYLE);
    if (ex & WS_EX_TOOLWINDOW)
        return false;
    BOOL cloaked = FALSE;
    DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    if (cloaked)
        return false;
    if (GetWindowTextLengthW(window) == 0)
        return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid == GetCurrentProcessId())
        return false;
    wchar_t className[64] = {};
    GetClassNameW(window, className, 64);
    const QString cls = QString::fromWCharArray(className);
    return cls != QLatin1String("Progman") && cls != QLatin1String("WorkerW") &&
           cls != QLatin1String("Shell_TrayWnd");
}

// Draws the mouse pointer onto a capture of the area at `origin`
void paintCursor(QImage &image, const QPoint &origin)
{
    CURSORINFO info { sizeof(CURSORINFO) };
    if (!GetCursorInfo(&info) || !(info.flags & CURSOR_SHOWING) || !info.hCursor)
        return;
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    // Qt 5 has no QImage::fromHICON. Draw onto the capture with GDI, which
    // also handles monochrome cursor masks without guessing their alpha.
    ICONINFO icon {};
    if (!GetIconInfo(info.hCursor, &icon))
        return;
    const QPoint position = QPoint(info.ptScreenPos.x, info.ptScreenPos.y) - origin -
                            QPoint(int(icon.xHotspot), int(icon.yHotspot));
    if (icon.hbmColor)
        DeleteObject(icon.hbmColor);
    if (icon.hbmMask)
        DeleteObject(icon.hbmMask);
    const QImage pixels = image.convertToFormat(QImage::Format_RGB32);
    Dib dib;
    if (!dib.create(pixels.width(), pixels.height()))
        return;
    std::memcpy(dib.bits, pixels.constBits(), size_t(pixels.width()) * size_t(pixels.height()) * 4);
    if (DrawIconEx(dib.dc, position.x(), position.y(), info.hCursor, 0, 0, 0, nullptr, DI_NORMAL))
        image = dib.image();
#else
    static QHash<HCURSOR, QPair<QImage, QPoint>> cache;
    auto it = cache.find(info.hCursor);
    if (it == cache.end()) {
        ICONINFO icon {};
        QPoint hotspot;
        if (GetIconInfo(info.hCursor, &icon)) {
            hotspot = QPoint(int(icon.xHotspot), int(icon.yHotspot));
            if (icon.hbmColor)
                DeleteObject(icon.hbmColor);
            if (icon.hbmMask)
                DeleteObject(icon.hbmMask);
        }
        it = cache.insert(info.hCursor, { QImage::fromHICON(info.hCursor), hotspot });
    }
    if (it->first.isNull())
        return;
    QPainter p(&image);
    p.drawImage(QPoint(info.ptScreenPos.x, info.ptScreenPos.y) - it->second - origin, it->first);
#endif
}

} // namespace

struct ScreenCapturer::Private
{
    CaptureSource source;
    QRect rect; // monitor rect, desktop coordinates
    HMONITOR monitor = nullptr;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> deviceContext;
    ComPtr<IDXGIOutputDuplication> duplication;
    ComPtr<ID3D11Texture2D> staging;
    std::unique_ptr<WgcCapture> wgc; // windows, when Windows.Graphics.Capture works
    QImage last;
    bool running = false;

    bool openDuplication()
    {
        duplication.Reset();
        staging.Reset();
        deviceContext.Reset();
        device.Reset();
        ComPtr<IDXGIFactory1> factory;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
            return false;
        ComPtr<IDXGIAdapter1> adapter;
        for (UINT a = 0; factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a) {
            ComPtr<IDXGIOutput> output;
            for (UINT o = 0; adapter->EnumOutputs(o, &output) != DXGI_ERROR_NOT_FOUND; ++o) {
                DXGI_OUTPUT_DESC desc;
                if (FAILED(output->GetDesc(&desc)) || desc.Monitor != monitor)
                    continue;
                ComPtr<IDXGIOutput1> output1;
                if (FAILED(output.As(&output1)))
                    return false;
                if (FAILED(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0,
                                             D3D11_SDK_VERSION, &device, nullptr, &deviceContext)))
                    return false;
                return SUCCEEDED(output1->DuplicateOutput(device.Get(), &duplication));
            }
        }
        return false;
    }

    QImage grabDuplication()
    {
        if (!duplication && !openDuplication())
            return {};
        DXGI_OUTDUPL_FRAME_INFO info {};
        ComPtr<IDXGIResource> resource;
        HRESULT hr = duplication->AcquireNextFrame(0, &info, &resource);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT)
            return last; // nothing changed
        if (hr == DXGI_ERROR_ACCESS_LOST) {
            duplication.Reset(); // mode change, UAC desktop, ...: open again next time
            return last;
        }
        if (FAILED(hr))
            return {};
        QImage image = last;
        ComPtr<ID3D11Texture2D> texture;
        if (SUCCEEDED(resource.As(&texture))) {
            D3D11_TEXTURE2D_DESC desc;
            texture->GetDesc(&desc);
            if (staging) {
                D3D11_TEXTURE2D_DESC have;
                staging->GetDesc(&have);
                if (have.Width != desc.Width || have.Height != desc.Height)
                    staging.Reset();
            }
            if (!staging) {
                desc.Usage = D3D11_USAGE_STAGING;
                desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                desc.BindFlags = 0;
                desc.MiscFlags = 0;
                desc.MipLevels = 1;
                desc.ArraySize = 1;
                desc.SampleDesc.Count = 1;
                device->CreateTexture2D(&desc, nullptr, &staging);
            }
            if (staging) {
                deviceContext->CopyResource(staging.Get(), texture.Get());
                D3D11_MAPPED_SUBRESOURCE mapped;
                if (SUCCEEDED(deviceContext->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
                    D3D11_TEXTURE2D_DESC have;
                    staging->GetDesc(&have);
                    image = QImage(static_cast<const uchar *>(mapped.pData), int(have.Width), int(have.Height),
                                   int(mapped.RowPitch), QImage::Format_RGB32)
                                    .copy();
                    deviceContext->Unmap(staging.Get(), 0);
                }
            }
        }
        duplication->ReleaseFrame();
        return image;
    }
};

ScreenCapturer::ScreenCapturer() : d(std::make_unique<Private>()) {}
ScreenCapturer::~ScreenCapturer() = default;

bool ScreenCapturer::start(const CaptureSource &source)
{
    stop();
    d->source = source;
    if (source.kind == CaptureSource::Kind::Screen) {
        const QList<HMONITOR> list = monitors();
        if (source.screenIndex < 0 || source.screenIndex >= list.size())
            return false;
        d->monitor = list[source.screenIndex];
        d->rect = monitorRect(d->monitor);
        d->openDuplication(); // GDI is used if this fails
    } else if (!IsWindow(reinterpret_cast<HWND>(source.window))) {
        return false;
    } else if (WgcCapture::isSupported()) {
        d->wgc = std::make_unique<WgcCapture>();
        if (!d->wgc->start(source.window))
            d->wgc.reset(); // PrintWindow then
    }
    d->running = true;
    return true;
}

void ScreenCapturer::stop()
{
    d->wgc.reset();
    d->duplication.Reset();
    d->staging.Reset();
    d->deviceContext.Reset();
    d->device.Reset();
    d->last = {};
    d->running = false;
}

bool ScreenCapturer::isRunning() const
{
    return d->running;
}

bool ScreenCapturer::sourceGone() const
{
    return d->running && d->source.kind == CaptureSource::Kind::Window &&
           !IsWindow(reinterpret_cast<HWND>(d->source.window));
}

QImage ScreenCapturer::grab()
{
    if (!d->running)
        return {};
    if (d->source.kind == CaptureSource::Kind::Window) {
        const HWND window = reinterpret_cast<HWND>(d->source.window);
        if (!IsWindow(window))
            return {};
        if (d->wgc)
            return d->wgc->grab(); // with the cursor already
        QImage image = grabWindow(window);
        if (image.isNull())
            return d->last; // minimized: keep showing the last picture
        if (drawCursor)
            paintCursor(image, visibleWindowRect(window).topLeft());
        d->last = image;
        return image;
    }
    QImage image = d->grabDuplication();
    if (image.isNull())
        image = grabRectGdi(d->rect);
    if (image.isNull())
        return d->last;
    d->last = image;
    QImage out = image;
    if (drawCursor)
        paintCursor(out, d->rect.topLeft());
    return out;
}

QList<CaptureSource> listCaptureSources(const QSize &thumbnailSize, bool includeWindows)
{
    QList<CaptureSource> sources;
    const QList<HMONITOR> list = monitors();
    for (int i = 0; i < list.size(); ++i) {
        CaptureSource s;
        s.kind = CaptureSource::Kind::Screen;
        s.screenIndex = i;
        s.name = QStringLiteral("Screen %1").arg(i + 1);
        const QRect rect = monitorRect(list[i]);
        s.size = rect.size();
        s.thumbnail = grabRectGdi(rect).scaled(thumbnailSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        sources.append(s);
    }
    if (!includeWindows)
        return sources;
    QList<HWND> windows;
    EnumWindows(
            [](HWND window, LPARAM data) -> BOOL {
                if (isShareableWindow(window))
                    reinterpret_cast<QList<HWND> *>(data)->append(window);
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&windows));
    for (HWND window : windows) {
        CaptureSource s;
        s.kind = CaptureSource::Kind::Window;
        s.window = reinterpret_cast<quintptr>(window);
        const int length = GetWindowTextLengthW(window);
        std::wstring title(size_t(length) + 1, L'\0');
        GetWindowTextW(window, title.data(), length + 1);
        s.name = QString::fromWCharArray(title.c_str(), length);
        s.processName = processNameOf(window);
        s.size = visibleWindowRect(window).size();
        const QImage picture = grabWindow(window);
        if (!picture.isNull())
            s.thumbnail = picture.scaled(thumbnailSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        sources.append(s);
    }
    return sources;
}

#else // not Windows: screens through Qt

struct ScreenCapturer::Private
{
    QScreen *screen = nullptr;
    QImage last;
};

ScreenCapturer::ScreenCapturer() : d(std::make_unique<Private>()) {}
ScreenCapturer::~ScreenCapturer() = default;

bool ScreenCapturer::start(const CaptureSource &source)
{
    const QList<QScreen *> screens = QGuiApplication::screens();
    if (source.kind != CaptureSource::Kind::Screen || source.screenIndex < 0 || source.screenIndex >= screens.size())
        return false;
    d->screen = screens[source.screenIndex];
    return true;
}

void ScreenCapturer::stop()
{
    d->screen = nullptr;
    d->last = {};
}

bool ScreenCapturer::isRunning() const
{
    return d->screen != nullptr;
}

bool ScreenCapturer::sourceGone() const
{
    return false;
}

QImage ScreenCapturer::grab()
{
    if (!d->screen)
        return {};
    const QImage image = d->screen->grabWindow(0).toImage();
    if (!image.isNull())
        d->last = image;
    return d->last;
}

QList<CaptureSource> listCaptureSources(const QSize &thumbnailSize, bool)
{
    QList<CaptureSource> sources;
    const QList<QScreen *> screens = QGuiApplication::screens();
    for (int i = 0; i < screens.size(); ++i) {
        CaptureSource s;
        s.screenIndex = i;
        s.name = QStringLiteral("Screen %1").arg(i + 1);
        s.size = screens[i]->size() * screens[i]->devicePixelRatio();
        s.thumbnail = screens[i]->grabWindow(0).toImage().scaled(thumbnailSize, Qt::KeepAspectRatio,
                                                                 Qt::SmoothTransformation);
        sources.append(s);
    }
    return sources;
}

#endif

} // namespace Video
} // namespace Qord
