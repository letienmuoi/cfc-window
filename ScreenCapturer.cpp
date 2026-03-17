#include "ScreenCapturer.h"

#include <opencv2/imgproc.hpp>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <windows.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

namespace wf = winrt::Windows::Foundation;
namespace wgc = winrt::Windows::Graphics::Capture;
namespace wgd = winrt::Windows::Graphics::DirectX;
namespace wgd11 = winrt::Windows::Graphics::DirectX::Direct3D11;

namespace {

template <typename T>
winrt::com_ptr<T> GetDXGIInterfaceFromObject(const wf::IInspectable& object)
{
    winrt::com_ptr<::IInspectable> inspectable = object.as<::IInspectable>();
    winrt::com_ptr<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess> access;
    winrt::check_hresult(inspectable->QueryInterface(
        __uuidof(::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess),
        access.put_void()));

    winrt::com_ptr<T> result;
    winrt::check_hresult(access->GetInterface(winrt::guid_of<T>(), result.put_void()));
    return result;
}

wgc::GraphicsCaptureItem CreateItemForPrimaryMonitor()
{
    POINT pt{};
    HMONITOR monitor = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
    if (!monitor)
        throw winrt::hresult_error(E_FAIL, L"Failed to locate primary monitor");

    auto interop = winrt::get_activation_factory<wgc::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    wgc::GraphicsCaptureItem item{ nullptr };
    winrt::check_hresult(interop->CreateForMonitor(
        monitor,
        winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(),
        winrt::put_abi(item)));
    return item;
}

} // namespace

struct ScreenCapturer::Impl
{
    winrt::com_ptr<ID3D11Device> d3dDevice;
    winrt::com_ptr<ID3D11DeviceContext> d3dContext;
    wgd11::IDirect3DDevice winrtDevice{ nullptr };
    wgc::GraphicsCaptureItem item{ nullptr };
    wgc::Direct3D11CaptureFramePool framePool{ nullptr };
    wgc::GraphicsCaptureSession session{ nullptr };
    winrt::com_ptr<ID3D11Texture2D> staging;
    int stagingWidth = 0;
    int stagingHeight = 0;
    bool started = false;
};

ScreenCapturer::ScreenCapturer()
{
    _impl = std::make_unique<Impl>();
    initCapture();
}

ScreenCapturer::~ScreenCapturer()
{
    releaseCapture();
}

void ScreenCapturer::setRegion(int x, int y, int width, int height)
{
    _srcX = x;
    _srcY = y;
    _width = width;
    _height = height;
    clampRegionToBounds();
}

void ScreenCapturer::initCapture()
{
    try
    {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    }
    catch (...) {}

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };
    D3D_FEATURE_LEVEL featureLevel{};

    winrt::check_hresult(D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        flags,
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        _impl->d3dDevice.put(),
        &featureLevel,
        _impl->d3dContext.put()));

    (void)featureLevel;

    auto dxgiDevice = _impl->d3dDevice.as<IDXGIDevice>();
    winrt::com_ptr<::IInspectable> inspectable;
    winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.get(), inspectable.put()));
    _impl->winrtDevice = inspectable.as<wgd11::IDirect3DDevice>();

    if (!wgc::GraphicsCaptureSession::IsSupported())
        throw winrt::hresult_error(E_NOTIMPL, L"Windows Graphics Capture is not supported on this OS");

    _impl->item = CreateItemForPrimaryMonitor();
    auto size = _impl->item.Size();
    _fullWidth = size.Width;
    _fullHeight = size.Height;

    if (_width <= 0 || _height <= 0)
    {
        _srcX = 0;
        _srcY = 0;
        _width = _fullWidth;
        _height = _fullHeight;
    }
    clampRegionToBounds();

    _impl->framePool = wgc::Direct3D11CaptureFramePool::Create(
        _impl->winrtDevice,
        wgd::DirectXPixelFormat::B8G8R8A8UIntNormalized,
        2,
        size);
    _impl->session = _impl->framePool.CreateCaptureSession(_impl->item);
    _impl->session.StartCapture();
    _impl->started = true;
}

void ScreenCapturer::releaseCapture()
{
    if (!_impl)
        return;

    if (_impl->session)
        _impl->session.Close();
    if (_impl->framePool)
        _impl->framePool.Close();

    _impl->session = nullptr;
    _impl->framePool = nullptr;
    _impl->item = nullptr;
    _impl->winrtDevice = nullptr;
    _impl->d3dContext = nullptr;
    _impl->d3dDevice = nullptr;
    _impl->staging = nullptr;
    _impl->started = false;
}

void ScreenCapturer::clampRegionToBounds()
{
    if (_fullWidth <= 0 || _fullHeight <= 0)
        return;

    if (_srcX < 0)
        _srcX = 0;
    if (_srcY < 0)
        _srcY = 0;

    if (_srcX >= _fullWidth)
        _srcX = _fullWidth - 1;
    if (_srcY >= _fullHeight)
        _srcY = _fullHeight - 1;

    if (_width <= 0)
        _width = _fullWidth;
    if (_height <= 0)
        _height = _fullHeight;

    if (_srcX + _width > _fullWidth)
        _width = _fullWidth - _srcX;
    if (_srcY + _height > _fullHeight)
        _height = _fullHeight - _srcY;
}

bool ScreenCapturer::grabFrame(cv::Mat& out)
{
    if (!_impl || !_impl->started || !_impl->framePool)
        return false;

    auto frame = _impl->framePool.TryGetNextFrame();
    if (!frame)
        return false;

    auto size = frame.ContentSize();
    if (size.Width <= 0 || size.Height <= 0)
        return false;

    if (size.Width != _fullWidth || size.Height != _fullHeight)
    {
        _fullWidth = size.Width;
        _fullHeight = size.Height;
        clampRegionToBounds();
        _impl->framePool.Recreate(_impl->winrtDevice,
                                  wgd::DirectXPixelFormat::B8G8R8A8UIntNormalized,
                                  2,
                                  size);
    }

    if (!_impl->staging || _impl->stagingWidth != size.Width || _impl->stagingHeight != size.Height)
    {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = static_cast<UINT>(size.Width);
        desc.Height = static_cast<UINT>(size.Height);
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        _impl->staging = nullptr;
        winrt::check_hresult(_impl->d3dDevice->CreateTexture2D(&desc, nullptr, _impl->staging.put()));
        _impl->stagingWidth = size.Width;
        _impl->stagingHeight = size.Height;
    }

    auto surfaceTex = GetDXGIInterfaceFromObject<ID3D11Texture2D>(frame.Surface());
    _impl->d3dContext->CopyResource(_impl->staging.get(), surfaceTex.get());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(_impl->d3dContext->Map(_impl->staging.get(), 0, D3D11_MAP_READ, 0, &mapped)))
        return false;

    cv::Mat bgra(_fullHeight, _fullWidth, CV_8UC4, mapped.pData, mapped.RowPitch);
    cv::Mat bgr;
    cv::cvtColor(bgra, bgr, cv::COLOR_BGRA2BGR);
    _impl->d3dContext->Unmap(_impl->staging.get(), 0);

    cv::Rect roi(_srcX, _srcY, _width, _height);
    if (roi.x >= 0 && roi.y >= 0 && roi.width > 0 && roi.height > 0 &&
        roi.x + roi.width <= bgr.cols && roi.y + roi.height <= bgr.rows)
    {
        out = bgr(roi).clone();
    }
    else
    {
        out = bgr;
    }

    return true;
}
