// C++/WinRT first: Qt's keyword macros must not reach these headers
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <d3d11.h>
#include <dxgi.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include "WgcCapture.hpp"

#include <atomic>

namespace Qord {
namespace Video {

namespace wgc = winrt::Windows::Graphics::Capture;
namespace wd3d = winrt::Windows::Graphics::DirectX::Direct3D11;
using winrt::Windows::Graphics::DirectX::DirectXPixelFormat;

struct WgcCapture::Impl
{
    winrt::com_ptr<ID3D11Device> device;
    winrt::com_ptr<ID3D11DeviceContext> context;
    wd3d::IDirect3DDevice rtDevice { nullptr };
    wgc::GraphicsCaptureItem item { nullptr };
    wgc::Direct3D11CaptureFramePool pool { nullptr };
    wgc::GraphicsCaptureSession session { nullptr };
    winrt::event_token closedToken {};
    winrt::Windows::Graphics::SizeInt32 poolSize {};
    winrt::com_ptr<ID3D11Texture2D> staging;
    D3D11_TEXTURE2D_DESC stagingDesc {};
    std::atomic<bool> closed { false };
    QImage last;
};

WgcCapture::WgcCapture() : d(std::make_unique<Impl>()) {}

WgcCapture::~WgcCapture()
{
    stop();
}

bool WgcCapture::isSupported()
{
    try {
        return wgc::GraphicsCaptureSession::IsSupported();
    } catch (...) {
        return false;
    }
}

bool WgcCapture::start(quintptr window)
{
    stop();
    try {
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
        } catch (...) {
            // already initialised on this thread
        }
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                     nullptr, 0, D3D11_SDK_VERSION, d->device.put(), nullptr, d->context.put())))
            return false;
        winrt::com_ptr<::IInspectable> inspectable;
        if (FAILED(CreateDirect3D11DeviceFromDXGIDevice(d->device.as<IDXGIDevice>().get(), inspectable.put())))
            return false;
        d->rtDevice = inspectable.as<wd3d::IDirect3DDevice>();

        auto interop = winrt::get_activation_factory<wgc::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        if (FAILED(interop->CreateForWindow(reinterpret_cast<HWND>(window), winrt::guid_of<wgc::GraphicsCaptureItem>(),
                                            winrt::put_abi(d->item))))
            return false;
        d->closed = false;
        Impl *impl = d.get();
        d->closedToken = d->item.Closed([impl](auto &&, auto &&) { impl->closed = true; });
        d->poolSize = d->item.Size();
        d->pool = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(
                d->rtDevice, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, d->poolSize);
        d->session = d->pool.CreateCaptureSession(d->item);
        try {
            d->session.IsCursorCaptureEnabled(true);
        } catch (...) {
        }
        try {
            // Windows 11 draws a yellow border otherwise, when allowed to leave it out
            d->session.IsBorderRequired(false);
        } catch (...) {
        }
        d->session.StartCapture();
        return true;
    } catch (...) {
        stop();
        return false;
    }
}

void WgcCapture::stop()
{
    try {
        if (d->item && d->closedToken.value)
            d->item.Closed(d->closedToken);
        if (d->session)
            d->session.Close();
        if (d->pool)
            d->pool.Close();
    } catch (...) {
    }
    d->closedToken = {};
    d->session = nullptr;
    d->pool = nullptr;
    d->item = nullptr;
    d->rtDevice = nullptr;
    d->staging = nullptr;
    d->context = nullptr;
    d->device = nullptr;
    d->last = {};
}

QImage WgcCapture::grab()
{
    if (!d->pool || d->closed)
        return {};
    try {
        // The newest frame; older ones are dropped
        wgc::Direct3D11CaptureFrame frame { nullptr };
        for (auto next = d->pool.TryGetNextFrame(); next; next = d->pool.TryGetNextFrame()) {
            if (frame)
                frame.Close();
            frame = next;
        }
        if (!frame)
            return d->last;

        const auto content = frame.ContentSize();
        auto access = frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        winrt::com_ptr<ID3D11Texture2D> texture;
        access->GetInterface(IID_PPV_ARGS(texture.put()));
        D3D11_TEXTURE2D_DESC desc;
        texture->GetDesc(&desc);
        const UINT width = UINT(std::min<int>(content.Width, int(desc.Width)));
        const UINT height = UINT(std::min<int>(content.Height, int(desc.Height)));

        QImage image = d->last;
        if (width > 0 && height > 0) {
            if (!d->staging || d->stagingDesc.Width != width || d->stagingDesc.Height != height) {
                D3D11_TEXTURE2D_DESC s = desc;
                s.Width = width;
                s.Height = height;
                s.Usage = D3D11_USAGE_STAGING;
                s.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                s.BindFlags = 0;
                s.MiscFlags = 0;
                s.MipLevels = 1;
                s.ArraySize = 1;
                s.SampleDesc.Count = 1;
                d->staging = nullptr;
                d->device->CreateTexture2D(&s, nullptr, d->staging.put());
                d->stagingDesc = s;
            }
            if (d->staging) {
                D3D11_BOX box { 0, 0, 0, width, height, 1 };
                d->context->CopySubresourceRegion(d->staging.get(), 0, 0, 0, 0, texture.get(), 0, &box);
                D3D11_MAPPED_SUBRESOURCE mapped;
                if (SUCCEEDED(d->context->Map(d->staging.get(), 0, D3D11_MAP_READ, 0, &mapped))) {
                    image = QImage(static_cast<const uchar *>(mapped.pData), int(width), int(height),
                                   int(mapped.RowPitch), QImage::Format_RGB32)
                                    .copy();
                    d->context->Unmap(d->staging.get(), 0);
                }
            }
        }
        // The window was resized: frames come at the new size from now on
        if (content.Width != d->poolSize.Width || content.Height != d->poolSize.Height) {
            d->poolSize = content;
            d->pool.Recreate(d->rtDevice, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, d->poolSize);
        }
        frame.Close();
        d->last = image;
        return image;
    } catch (...) {
        return d->last;
    }
}

} // namespace Video
} // namespace Qord
