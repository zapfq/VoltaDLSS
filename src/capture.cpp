#define NOMINMAX

#include "capture.h"

#include <windows.h>

#include <d3d11.h>
#include <dxgi.h>

#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <winrt/base.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <chrono>
#include <iostream>
#include <mutex>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "windowsapp.lib")

using Microsoft::WRL::ComPtr;

namespace WGC =
    winrt::Windows::Graphics::Capture;

namespace WGD =
    winrt::Windows::Graphics::DirectX;

namespace WGD11 =
    winrt::Windows::Graphics::DirectX::Direct3D11;

using DxgiAccess =
    ::Windows::Graphics::DirectX::Direct3D11::
        IDirect3DDxgiInterfaceAccess;


/*
    -------------------------------------------------------------
    GPU scheduling
    -------------------------------------------------------------
*/

static bool setCaptureGpuPriority(
    ID3D11Device* device)
{
    if (!device)
        return false;

    ComPtr<IDXGIDevice>
        dxgiDevice;

    HRESULT hr =
        device->QueryInterface(
            IID_PPV_ARGS(
                dxgiDevice.GetAddressOf()));

    if (FAILED(hr))
    {
        std::cerr
            << "Query IDXGIDevice for capture priority failed: 0x"
            << std::hex
            << static_cast<unsigned long>(hr)
            << std::dec
            << '\n';

        return false;
    }

    constexpr INT kRelativeGpuPriority =
        7;

    hr =
        dxgiDevice->SetGPUThreadPriority(
            kRelativeGpuPriority);

    if (FAILED(hr))
    {
        std::cerr
            << "SetGPUThreadPriority(capture, +7) failed: 0x"
            << std::hex
            << static_cast<unsigned long>(hr)
            << std::dec
            << '\n';

        return false;
    }

    INT reportedPriority =
        0;

    hr =
        dxgiDevice->GetGPUThreadPriority(
            &reportedPriority);

    if (SUCCEEDED(hr))
    {
        std::cout
            << "WGC D3D11 GPU priority: +7"
            << " (reported "
            << reportedPriority
            << ")\n";
    }
    else
    {
        std::cout
            << "WGC D3D11 GPU priority: +7\n";
    }

    return true;
}


struct DesktopCapture::Impl
{
    HWND targetWindow = nullptr;

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;

    WGD11::IDirect3DDevice winrtDevice{
        nullptr
    };

    WGC::Direct3D11CaptureFramePool framePool{
        nullptr
    };

    WGC::GraphicsCaptureSession session{
        nullptr
    };

    winrt::event_token frameArrivedToken{};

    std::mutex mutex;
    std::condition_variable condition;

    bool frameAvailable = false;
    bool shuttingDown = false;
    bool initialized = false;

    ComPtr<ID3D11Texture2D> persistentTexture;

    int persistentWidth = 0;
    int persistentHeight = 0;

    int cropLeft = 0;
    int cropTop = 0;
    int cropWidth = 0;
    int cropHeight = 0;

    int fullWidth = 0;
    int fullHeight = 0;
};


static void makeProcessDpiAware()
{
    SetProcessDpiAwarenessContext(
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
}


static bool createD3D11Device(
    DesktopCapture::Impl& impl)
{
    UINT flags =
        D3D11_CREATE_DEVICE_BGRA_SUPPORT;

    D3D_FEATURE_LEVEL featureLevel{};

    const D3D_FEATURE_LEVEL levels[] =
    {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };

    HRESULT hr =
        D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            flags,
            levels,
            ARRAYSIZE(levels),
            D3D11_SDK_VERSION,
            impl.device.ReleaseAndGetAddressOf(),
            &featureLevel,
            impl.context.ReleaseAndGetAddressOf());

    if (FAILED(hr))
    {
        std::cerr
            << "D3D11CreateDevice failed: 0x"
            << std::hex
            << static_cast<unsigned long>(hr)
            << std::dec
            << '\n';

        return false;
    }

    std::cout
        << "WGC D3D11 feature level: 0x"
        << std::hex
        << static_cast<unsigned int>(
               featureLevel)
        << std::dec
        << '\n';

    setCaptureGpuPriority(
        impl.device.Get());

    return true;
}


static bool createWinRTDevice(
    DesktopCapture::Impl& impl)
{
    ComPtr<IDXGIDevice>
        dxgiDevice;

    HRESULT hr =
        impl.device.As(
            &dxgiDevice);

    if (FAILED(hr))
        return false;

    hr =
        ::CreateDirect3D11DeviceFromDXGIDevice(
            dxgiDevice.Get(),
            reinterpret_cast<::IInspectable**>(
                winrt::put_abi(
                    impl.winrtDevice)));

    if (FAILED(hr))
    {
        std::cerr
            << "CreateDirect3D11DeviceFromDXGIDevice failed: 0x"
            << std::hex
            << static_cast<unsigned long>(hr)
            << std::dec
            << '\n';

        return false;
    }

    return impl.winrtDevice != nullptr;
}


static WGC::GraphicsCaptureItem
createCaptureItem(
    HWND hwnd)
{
    auto factory =
        winrt::get_activation_factory<
            WGC::GraphicsCaptureItem,
            IGraphicsCaptureItemInterop>();

    WGC::GraphicsCaptureItem item{
        nullptr
    };

    HRESULT hr =
        factory->CreateForWindow(
            hwnd,
            winrt::guid_of<
                WGC::GraphicsCaptureItem>(),
            reinterpret_cast<void**>(
                winrt::put_abi(item)));

    if (FAILED(hr))
    {
        std::cerr
            << "CreateForWindow failed: 0x"
            << std::hex
            << static_cast<unsigned long>(hr)
            << std::dec
            << '\n';

        return nullptr;
    }

    return item;
}


static bool getClientCrop(
    HWND hwnd,
    int captureWidth,
    int captureHeight,
    int& cropLeft,
    int& cropTop,
    int& cropWidth,
    int& cropHeight)
{
    RECT clientRect{};

    if (!GetClientRect(
            hwnd,
            &clientRect))
    {
        return false;
    }

    const int clientWidth =
        clientRect.right -
        clientRect.left;

    const int clientHeight =
        clientRect.bottom -
        clientRect.top;

    if (clientWidth <= 0 ||
        clientHeight <= 0)
    {
        return false;
    }

    POINT clientTopLeft{};

    if (!ClientToScreen(
            hwnd,
            &clientTopLeft))
    {
        return false;
    }

    RECT windowRect{};

    if (!GetWindowRect(
            hwnd,
            &windowRect))
    {
        return false;
    }

    const int windowWidth =
        windowRect.right -
        windowRect.left;

    const int windowHeight =
        windowRect.bottom -
        windowRect.top;

    if (windowWidth <= 0 ||
        windowHeight <= 0)
    {
        return false;
    }

    const double scaleX =
        static_cast<double>(
            captureWidth) /
        static_cast<double>(
            windowWidth);

    const double scaleY =
        static_cast<double>(
            captureHeight) /
        static_cast<double>(
            windowHeight);

    int left =
        static_cast<int>(
            std::lround(
                static_cast<double>(
                    clientTopLeft.x -
                    windowRect.left) *
                scaleX));

    int top =
        static_cast<int>(
            std::lround(
                static_cast<double>(
                    clientTopLeft.y -
                    windowRect.top) *
                scaleY));

    int width =
        static_cast<int>(
            std::lround(
                static_cast<double>(
                    clientWidth) *
                scaleX));

    int height =
        static_cast<int>(
            std::lround(
                static_cast<double>(
                    clientHeight) *
                scaleY));

    left =
        std::max(
            0,
            left);

    top =
        std::max(
            0,
            top);

    if (left >= captureWidth ||
        top >= captureHeight)
    {
        return false;
    }

    width =
        std::min(
            width,
            captureWidth -
                left);

    height =
        std::min(
            height,
            captureHeight -
                top);

    if (width <= 0 ||
        height <= 0)
    {
        return false;
    }

    cropLeft =
        left;

    cropTop =
        top;

    cropWidth =
        width;

    cropHeight =
        height;

    return true;
}


static bool createPersistentTexture(
    DesktopCapture::Impl& impl,
    int width,
    int height)
{
    D3D11_TEXTURE2D_DESC desc{};

    desc.Width =
        static_cast<UINT>(
            width);

    desc.Height =
        static_cast<UINT>(
            height);

    desc.MipLevels =
        1;

    desc.ArraySize =
        1;

    desc.Format =
        DXGI_FORMAT_B8G8R8A8_UNORM;

    desc.SampleDesc.Count =
        1;

    desc.Usage =
        D3D11_USAGE_DEFAULT;

    desc.BindFlags =
        D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr =
        impl.device->CreateTexture2D(
            &desc,
            nullptr,
            impl.persistentTexture.ReleaseAndGetAddressOf());

    if (FAILED(hr))
    {
        std::cerr
            << "Create persistent texture failed: 0x"
            << std::hex
            << static_cast<unsigned long>(hr)
            << std::dec
            << '\n';

        return false;
    }

    impl.persistentWidth =
        width;

    impl.persistentHeight =
        height;

    return true;
}


static bool updateCrop(
    DesktopCapture::Impl& impl,
    int captureWidth,
    int captureHeight)
{
    int left = 0;
    int top = 0;
    int width = 0;
    int height = 0;

    if (!getClientCrop(
            impl.targetWindow,
            captureWidth,
            captureHeight,
            left,
            top,
            width,
            height))
    {
        return false;
    }

    const bool changed =
        impl.fullWidth != captureWidth ||
        impl.fullHeight != captureHeight ||
        impl.cropLeft != left ||
        impl.cropTop != top ||
        impl.cropWidth != width ||
        impl.cropHeight != height;

    impl.fullWidth =
        captureWidth;

    impl.fullHeight =
        captureHeight;

    impl.cropLeft =
        left;

    impl.cropTop =
        top;

    impl.cropWidth =
        width;

    impl.cropHeight =
        height;

    if (!impl.persistentTexture ||
        impl.persistentWidth != width ||
        impl.persistentHeight != height)
    {
        if (!createPersistentTexture(
                impl,
                width,
                height))
        {
            return false;
        }
    }

    if (changed)
    {
        std::cout
            << "WGC capture surface: "
            << captureWidth
            << " x "
            << captureHeight
            << '\n';

        std::cout
            << "Client crop: "
            << width
            << " x "
            << height
            << '\n';

        std::cout
            << "Crop offset: "
            << left
            << ", "
            << top
            << '\n';
    }

    return true;
}


DesktopCapture::DesktopCapture()
    : impl(new Impl)
{
}


DesktopCapture::~DesktopCapture()
{
    shutdown();

    delete impl;

    impl =
        nullptr;
}


bool DesktopCapture::initialize(
    HWND targetWindow)
{
    shutdown();

    if (!targetWindow ||
        !IsWindow(targetWindow))
    {
        std::cerr
            << "Invalid target window.\n";

        return false;
    }

    impl->targetWindow =
        targetWindow;

    impl->shuttingDown =
        false;

    impl->frameAvailable =
        false;

    makeProcessDpiAware();

    winrt::init_apartment(
        winrt::apartment_type::multi_threaded);

    if (!createD3D11Device(
            *impl))
    {
        return false;
    }

    if (!createWinRTDevice(
            *impl))
    {
        return false;
    }

    WGC::GraphicsCaptureItem item =
        createCaptureItem(
            targetWindow);

    if (!item)
        return false;

    auto size =
        item.Size();

    const int captureWidth =
        size.Width;

    const int captureHeight =
        size.Height;

    if (captureWidth <= 0 ||
        captureHeight <= 0)
    {
        return false;
    }

    if (!updateCrop(
            *impl,
            captureWidth,
            captureHeight))
    {
        std::cerr
            << "Failed to determine client crop.\n";

        return false;
    }

    impl->framePool =
        WGC::Direct3D11CaptureFramePool::CreateFreeThreaded(
            impl->winrtDevice,
            WGD::DirectXPixelFormat::B8G8R8A8UIntNormalized,
            2,
            size);

    if (!impl->framePool)
    {
        std::cerr
            << "Failed to create WGC frame pool.\n";

        return false;
    }

    impl->frameArrivedToken =
        impl->framePool.FrameArrived(
            [this](
                auto const&,
                auto const&)
            {
                {
                    std::lock_guard<std::mutex>
                        lock(
                            impl->mutex);

                    if (impl->shuttingDown)
                        return;

                    impl->frameAvailable =
                        true;
                }

                impl->condition.notify_one();
            });

    impl->session =
        impl->framePool.CreateCaptureSession(
            item);

    if (!impl->session)
    {
        std::cerr
            << "Failed to create capture session.\n";

        return false;
    }

    try
    {
        impl->session.IsCursorCaptureEnabled(
            false);
    }
    catch (...)
    {
    }

    impl->session.StartCapture();

    impl->initialized =
        true;

    std::cout
        << "Windows Graphics Capture initialized.\n";

    std::cout
        << "Target: "
        << captureWidth
        << " x "
        << captureHeight
        << '\n';

    std::cout
        << "Client: "
        << impl->cropWidth
        << " x "
        << impl->cropHeight
        << '\n';

    std::cout
        << "WGC capture ready.\n";

    return true;
}


bool DesktopCapture::waitForFrame(
    int timeoutMs)
{
    if (!impl ||
        !impl->initialized)
    {
        return false;
    }

    std::unique_lock<std::mutex>
        lock(
            impl->mutex);

    const bool ready =
        impl->condition.wait_for(
            lock,
            std::chrono::milliseconds(
                timeoutMs),
            [this]()
            {
                return
                    impl->frameAvailable ||
                    impl->shuttingDown;
            });

    if (!ready ||
        impl->shuttingDown)
    {
        return false;
    }

    impl->frameAvailable =
        false;

    return true;
}


bool DesktopCapture::captureTexture(
    ID3D11Texture2D** texture,
    int& width,
    int& height)
{
    if (!texture)
        return false;

    *texture = nullptr;

    width = 0;
    height = 0;

    if (!impl ||
        !impl->initialized ||
        !impl->framePool)
    {
        return false;
    }

    WGC::Direct3D11CaptureFrame newestFrame{
        nullptr
    };

    while (true)
    {
        auto nextFrame =
            impl->framePool.TryGetNextFrame();

        if (!nextFrame)
            break;

        newestFrame =
            std::move(
                nextFrame);
    }

    if (!newestFrame)
        return false;

    auto frameSize =
        newestFrame.ContentSize();

    const int captureWidth =
        frameSize.Width;

    const int captureHeight =
        frameSize.Height;

    if (captureWidth <= 0 ||
        captureHeight <= 0)
    {
        return false;
    }

    if (!updateCrop(
            *impl,
            captureWidth,
            captureHeight))
    {
        return false;
    }

    auto surface =
        newestFrame.Surface();

    if (!surface)
        return false;

    winrt::com_ptr<DxgiAccess>
        access{
            surface.as<DxgiAccess>()
        };

    if (!access)
        return false;

    ComPtr<ID3D11Texture2D>
        sourceTexture;

    HRESULT hr =
        access->GetInterface(
            __uuidof(ID3D11Texture2D),
            reinterpret_cast<void**>(
                sourceTexture.GetAddressOf()));

    if (FAILED(hr) ||
        !sourceTexture)
    {
        std::cerr
            << "GetInterface(ID3D11Texture2D) failed: 0x"
            << std::hex
            << static_cast<unsigned long>(hr)
            << std::dec
            << '\n';

        return false;
    }

    D3D11_BOX sourceBox{};

    sourceBox.left =
        static_cast<UINT>(
            impl->cropLeft);

    sourceBox.top =
        static_cast<UINT>(
            impl->cropTop);

    sourceBox.front =
        0;

    sourceBox.right =
        static_cast<UINT>(
            impl->cropLeft +
            impl->cropWidth);

    sourceBox.bottom =
        static_cast<UINT>(
            impl->cropTop +
            impl->cropHeight);

    sourceBox.back =
        1;

    impl->context->CopySubresourceRegion(
        impl->persistentTexture.Get(),
        0,
        0,
        0,
        0,
        sourceTexture.Get(),
        0,
        &sourceBox);

    *texture =
        impl->persistentTexture.Get();

    (*texture)->AddRef();

    width =
        impl->persistentWidth;

    height =
        impl->persistentHeight;

    return true;
}


ID3D11Device*
DesktopCapture::getDevice() const
{
    if (!impl)
        return nullptr;

    return impl->device.Get();
}


ID3D11DeviceContext*
DesktopCapture::getContext() const
{
    if (!impl)
        return nullptr;

    return impl->context.Get();
}


bool DesktopCapture::capture(
    Image& image)
{
    (void)image;

    return false;
}


void DesktopCapture::shutdown()
{
    if (!impl)
        return;

    {
        std::lock_guard<std::mutex>
            lock(
                impl->mutex);

        impl->shuttingDown =
            true;

        impl->frameAvailable =
            false;
    }

    impl->condition.notify_all();

    if (impl->session)
    {
        try
        {
            impl->session.Close();
        }
        catch (...)
        {
        }

        impl->session =
            nullptr;
    }

    if (impl->framePool)
    {
        try
        {
            if (impl->frameArrivedToken.value != 0)
            {
                impl->framePool.FrameArrived(
                    impl->frameArrivedToken);

                impl->frameArrivedToken =
                    {};
            }

            impl->framePool.Close();
        }
        catch (...)
        {
        }

        impl->framePool =
            nullptr;
    }

    impl->persistentTexture.Reset();

    impl->persistentWidth =
        0;

    impl->persistentHeight =
        0;

    impl->cropLeft =
        0;

    impl->cropTop =
        0;

    impl->cropWidth =
        0;

    impl->cropHeight =
        0;

    impl->fullWidth =
        0;

    impl->fullHeight =
        0;

    impl->winrtDevice =
        nullptr;

    if (impl->context)
    {
        impl->context->ClearState();
    }

    impl->context.Reset();

    impl->device.Reset();

    impl->targetWindow =
        nullptr;

    impl->initialized =
        false;
}