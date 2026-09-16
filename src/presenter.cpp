#define NOMINMAX

#include "presenter.h"

#include "overlay.h"
#include "telemetry.h"

#include <windows.h>

#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <chrono>
#include <iostream>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;


struct D3D11Presenter::Impl
{
    HWND window = nullptr;

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;

    ComPtr<IDXGISwapChain> swapChain;

    ComPtr<ID3D11Texture2D> backBuffer;
    ComPtr<ID3D11Texture2D> outputTexture;

    int width = 0;
    int height = 0;

    VoltaOverlay overlay;
    Telemetry telemetry;

    bool overlayInitialized = false;
    bool telemetryInitialized = false;

    /*
        Hardware telemetry is refreshed at 4 Hz.

        The overlay is still drawn every frame, but it displays
        the same snapshot between telemetry updates.
    */
    static constexpr std::chrono::milliseconds telemetryInterval{
        250
    };

    std::chrono::steady_clock::time_point lastTelemetryUpdate{};
};


static bool createDevice(
    ID3D11Device* suppliedDevice,
    ID3D11DeviceContext* suppliedContext,
    ComPtr<ID3D11Device>& device,
    ComPtr<ID3D11DeviceContext>& context)
{
    if (suppliedDevice &&
        suppliedContext)
    {
        device =
            suppliedDevice;

        context =
            suppliedContext;

        return true;
    }

    UINT flags =
        D3D11_CREATE_DEVICE_BGRA_SUPPORT;

    D3D_FEATURE_LEVEL featureLevels[] =
    {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0
    };

    D3D_FEATURE_LEVEL featureLevel{};

    HRESULT hr =
        D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            flags,
            featureLevels,
            ARRAYSIZE(featureLevels),
            D3D11_SDK_VERSION,
            &device,
            &featureLevel,
            &context);

    return SUCCEEDED(hr);
}


static bool createOutputTexture(
    ID3D11Device* device,
    int width,
    int height,
    ComPtr<ID3D11Texture2D>& texture)
{
    D3D11_TEXTURE2D_DESC desc{};

    desc.Width =
        static_cast<UINT>(width);

    desc.Height =
        static_cast<UINT>(height);

    desc.MipLevels = 1;
    desc.ArraySize = 1;

    desc.Format =
        DXGI_FORMAT_B8G8R8A8_UNORM;

    desc.SampleDesc.Count = 1;

    desc.Usage =
        D3D11_USAGE_DEFAULT;

    desc.BindFlags =
        D3D11_BIND_SHADER_RESOURCE |
        D3D11_BIND_UNORDERED_ACCESS;

    HRESULT hr =
        device->CreateTexture2D(
            &desc,
            nullptr,
            &texture);

    return SUCCEEDED(hr);
}


D3D11Presenter::D3D11Presenter()
{
    impl =
        new Impl();
}


D3D11Presenter::~D3D11Presenter()
{
    shutdown();

    delete impl;

    impl =
        nullptr;
}


bool D3D11Presenter::initialize(
    HWND window,
    int width,
    int height,
    ID3D11Device* device,
    ID3D11DeviceContext* context)
{
    if (!impl ||
        !window ||
        width <= 0 ||
        height <= 0)
    {
        return false;
    }

    impl->window =
        window;

    impl->width =
        width;

    impl->height =
        height;

    if (!createDevice(
            device,
            context,
            impl->device,
            impl->context))
    {
        return false;
    }

    ComPtr<IDXGIDevice> dxgiDevice;

    if (FAILED(
            impl->device.As(
                &dxgiDevice)))
    {
        return false;
    }

    IDXGIAdapter* adapter =
        nullptr;

    HRESULT hr =
        dxgiDevice->GetAdapter(
            &adapter);

    if (FAILED(hr))
        return false;

    IDXGIFactory2* factory =
        nullptr;

    hr =
        adapter->GetParent(
            __uuidof(IDXGIFactory2),
            reinterpret_cast<void**>(
                &factory));

    adapter->Release();

    if (FAILED(hr))
        return false;

    DXGI_SWAP_CHAIN_DESC1 desc{};

    desc.Width =
        static_cast<UINT>(width);

    desc.Height =
        static_cast<UINT>(height);

    desc.Format =
        DXGI_FORMAT_B8G8R8A8_UNORM;

    desc.SampleDesc.Count = 1;

    desc.BufferUsage =
        DXGI_USAGE_RENDER_TARGET_OUTPUT |
        DXGI_USAGE_SHADER_INPUT;

    desc.BufferCount = 2;

    desc.SwapEffect =
        DXGI_SWAP_EFFECT_FLIP_DISCARD;

    desc.Scaling =
        DXGI_SCALING_STRETCH;

    desc.AlphaMode =
        DXGI_ALPHA_MODE_IGNORE;

    IDXGISwapChain1* swapChain =
        nullptr;

    hr =
        factory->CreateSwapChainForHwnd(
            impl->device.Get(),
            window,
            &desc,
            nullptr,
            nullptr,
            &swapChain);

    factory->Release();

    if (FAILED(hr))
        return false;

    impl->swapChain =
        swapChain;

    swapChain->Release();

    hr =
        impl->swapChain->GetBuffer(
            0,
            __uuidof(ID3D11Texture2D),
            reinterpret_cast<void**>(
                impl->backBuffer.GetAddressOf()));

    if (FAILED(hr))
        return false;

    if (!createOutputTexture(
            impl->device.Get(),
            width,
            height,
            impl->outputTexture))
    {
        return false;
    }

    if (impl->overlay.initialize(
            impl->backBuffer.Get(),
            width,
            height))
    {
        impl->overlayInitialized =
            true;

        std::cout
            << "Telemetry overlay initialized.\n";
    }
    else
    {
        std::cerr
            << "Telemetry overlay initialization failed.\n";
    }

    if (impl->telemetry.initialize())
    {
        impl->telemetryInitialized =
            true;

        impl->telemetry.setTensorOutputResolution(
            width,
            height);
    }
    else
    {
        std::cerr
            << "NVML telemetry unavailable.\n";
    }

    /*
        Force the first hardware telemetry update immediately.
    */
    impl->lastTelemetryUpdate =
        std::chrono::steady_clock::now() -
        Impl::telemetryInterval;

    std::cout
        << "D3D11 presenter using shared capture device.\n";

    return true;
}


bool D3D11Presenter::initializeTextureOnly(
    int width,
    int height)
{
    return initializeTextureOnly(
        width,
        height,
        nullptr,
        nullptr);
}


bool D3D11Presenter::initializeTextureOnly(
    int width,
    int height,
    ID3D11Device* device,
    ID3D11DeviceContext* context)
{
    if (!impl ||
        width <= 0 ||
        height <= 0)
    {
        return false;
    }

    impl->width =
        width;

    impl->height =
        height;

    if (!createDevice(
            device,
            context,
            impl->device,
            impl->context))
    {
        return false;
    }

    if (!createOutputTexture(
            impl->device.Get(),
            width,
            height,
            impl->outputTexture))
    {
        return false;
    }

    return true;
}


void D3D11Presenter::setFrameStats(
    double fps,
    double frameTimeMs,
    double captureMs,
    double upscaleMs,
    double presentMs)
{
    if (!impl ||
        !impl->telemetryInitialized)
    {
        return;
    }

    /*
        Frame statistics themselves can be updated every frame.
        No request is made for a hardware telemetry refresh here.
    */

    impl->telemetry.setFrameStats(
        fps,
        frameTimeMs,
        captureMs,
        upscaleMs,
        presentMs);
}


bool D3D11Presenter::present()
{
    if (!impl ||
        !impl->context ||
        !impl->swapChain ||
        !impl->backBuffer ||
        !impl->outputTexture)
    {
        return false;
    }

    /*
        ---------------------------------------------------------
        IMAGE
        ---------------------------------------------------------
    */

    impl->context->CopyResource(
        impl->backBuffer.Get(),
        impl->outputTexture.Get());


    /*
        ---------------------------------------------------------
        HARDWARE TELEMETRY
        ---------------------------------------------------------

        NVML is queried only once every 250 ms.
    */

    const auto now =
        std::chrono::steady_clock::now();

    if ((now -
         impl->lastTelemetryUpdate) >=
        Impl::telemetryInterval)
    {
        if (impl->telemetryInitialized)
        {
            impl->telemetry.updateHardware();
        }

        impl->lastTelemetryUpdate =
            now;
    }


    /*
        ---------------------------------------------------------
        OVERLAY
        ---------------------------------------------------------

        Render every frame because the swapchain uses
        FLIP_DISCARD.

        The underlying telemetry snapshot remains unchanged
        between the 250 ms hardware updates, so the displayed
        numbers do not rapidly flicker.
    */

    if (impl->overlayInitialized)
    {
        impl->overlay.draw(
            impl->telemetry.getSnapshot());
    }


    /*
        ---------------------------------------------------------
        PRESENT
        ---------------------------------------------------------
    */

    HRESULT hr =
        impl->swapChain->Present(
            0,
            0);

    return SUCCEEDED(hr);
}


ID3D11Texture2D*
D3D11Presenter::getOutputTexture() const
{
    if (!impl)
        return nullptr;

    return impl->outputTexture.Get();
}


void D3D11Presenter::shutdown()
{
    if (!impl)
        return;

    if (impl->overlayInitialized)
    {
        impl->overlay.shutdown();

        impl->overlayInitialized =
            false;
    }

    if (impl->telemetryInitialized)
    {
        impl->telemetry.shutdown();

        impl->telemetryInitialized =
            false;
    }

    impl->outputTexture.Reset();
    impl->backBuffer.Reset();
    impl->swapChain.Reset();
    impl->context.Reset();
    impl->device.Reset();

    impl->window =
        nullptr;

    impl->width =
        0;

    impl->height =
        0;
}
