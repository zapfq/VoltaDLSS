#define NOMINMAX

#include "overlay.h"

#include <d2d1.h>
#include <dwrite.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdio>
#include <cwchar>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

using Microsoft::WRL::ComPtr;


struct VoltaOverlay::Impl
{
    int width = 0;
    int height = 0;

    ComPtr<ID2D1Factory> d2dFactory;
    ComPtr<IDWriteFactory> writeFactory;
    ComPtr<IDWriteTextFormat> textFormat;

    ComPtr<ID2D1RenderTarget> renderTarget;

    ComPtr<ID2D1SolidColorBrush> backgroundBrush;
    ComPtr<ID2D1SolidColorBrush> textBrush;
    ComPtr<ID2D1SolidColorBrush> borderBrush;

    bool initialized = false;
};


VoltaOverlay::VoltaOverlay()
    : impl(new Impl)
{
}


VoltaOverlay::~VoltaOverlay()
{
    shutdown();

    delete impl;
    impl = nullptr;
}


bool VoltaOverlay::initialize(
    ID3D11Texture2D* backBuffer,
    int width,
    int height)
{
    shutdown();

    if (!impl ||
        !backBuffer ||
        width <= 0 ||
        height <= 0)
    {
        return false;
    }

    impl->width = width;
    impl->height = height;

    HRESULT hr =
        D2D1CreateFactory(
            D2D1_FACTORY_TYPE_SINGLE_THREADED,
            impl->d2dFactory.GetAddressOf());

    if (FAILED(hr))
        return false;

    hr =
        DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(
                impl->writeFactory.GetAddressOf()));

    if (FAILED(hr))
    {
        shutdown();
        return false;
    }

    hr =
        impl->writeFactory->CreateTextFormat(
            L"Segoe UI",
            nullptr,
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            14.0f,
            L"en-US",
            impl->textFormat.GetAddressOf());

    if (FAILED(hr))
    {
        shutdown();
        return false;
    }

    impl->textFormat->SetTextAlignment(
        DWRITE_TEXT_ALIGNMENT_LEADING);

    impl->textFormat->SetParagraphAlignment(
        DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    ComPtr<IDXGISurface> surface;

    hr =
        backBuffer->QueryInterface(
            IID_PPV_ARGS(
                surface.GetAddressOf()));

    if (FAILED(hr))
    {
        shutdown();
        return false;
    }

    D2D1_RENDER_TARGET_PROPERTIES properties =
        D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(
                DXGI_FORMAT_B8G8R8A8_UNORM,
                D2D1_ALPHA_MODE_PREMULTIPLIED),
            96.0f,
            96.0f);

    hr =
        impl->d2dFactory->CreateDxgiSurfaceRenderTarget(
            surface.Get(),
            &properties,
            impl->renderTarget.GetAddressOf());

    if (FAILED(hr))
    {
        shutdown();
        return false;
    }

    hr =
        impl->renderTarget->CreateSolidColorBrush(
            D2D1::ColorF(
                0.015f,
                0.015f,
                0.02f,
                0.82f),
            impl->backgroundBrush.GetAddressOf());

    if (FAILED(hr))
    {
        shutdown();
        return false;
    }

    hr =
        impl->renderTarget->CreateSolidColorBrush(
            D2D1::ColorF(
                0.95f,
                0.95f,
                0.95f,
                1.0f),
            impl->textBrush.GetAddressOf());

    if (FAILED(hr))
    {
        shutdown();
        return false;
    }

    hr =
        impl->renderTarget->CreateSolidColorBrush(
            D2D1::ColorF(
                0.30f,
                0.32f,
                0.36f,
                0.8f),
            impl->borderBrush.GetAddressOf());

    if (FAILED(hr))
    {
        shutdown();
        return false;
    }

    impl->initialized = true;

    return true;
}


void VoltaOverlay::shutdown()
{
    if (!impl)
        return;

    impl->borderBrush.Reset();
    impl->textBrush.Reset();
    impl->backgroundBrush.Reset();

    impl->renderTarget.Reset();
    impl->textFormat.Reset();
    impl->writeFactory.Reset();
    impl->d2dFactory.Reset();

    impl->width = 0;
    impl->height = 0;

    impl->initialized = false;
}


static double bytesToGB(
    std::uint64_t bytes)
{
    return static_cast<double>(bytes) /
        (1024.0 * 1024.0 * 1024.0);
}


bool VoltaOverlay::draw(
    const TelemetrySnapshot& telemetry)
{
    if (!impl ||
        !impl->initialized ||
        !impl->renderTarget ||
        !impl->textFormat)
    {
        return false;
    }

    const float left = 10.0f;
    const float top = 10.0f;
    const float barHeight = 32.0f;

    char text[2048]{};

    const double usedGB =
        bytesToGB(
            telemetry.vramUsedBytes);

    const double totalGB =
        bytesToGB(
            telemetry.vramTotalBytes);

    std::snprintf(
        text,
        sizeof(text),
        "FPS %.0f  |  %.1f ms  |  "
        "CAP %.1f ms  |  UPS %.1f ms  |  PRE %.1f ms  |  "
        "GPU %u%%  |  TENSOR ~%u%%  |  "
        "%u C  |  VRAM %.1f / %.1f GB",
        telemetry.fps,
        telemetry.frameTimeMs,
        telemetry.captureMs,
        telemetry.upscaleMs,
        telemetry.presentMs,
        telemetry.gpuUtilization,
        telemetry.tensorUtilization,
        telemetry.temperatureC,
        usedGB,
        totalGB);

    wchar_t wideText[2048]{};

    MultiByteToWideChar(
        CP_ACP,
        0,
        text,
        -1,
        wideText,
        ARRAYSIZE(wideText));

    /*
        Create a DirectWrite layout so the background bar fits
        the actual rendered text width instead of using a fixed
        1900-pixel rectangle.
    */

    ComPtr<IDWriteTextLayout> textLayout;

    HRESULT hr =
        impl->writeFactory->CreateTextLayout(
            wideText,
            static_cast<UINT32>(
                wcslen(wideText)),
            impl->textFormat.Get(),
            static_cast<float>(
                impl->width),
            barHeight,
            textLayout.GetAddressOf());

    if (FAILED(hr))
        return false;

    DWRITE_TEXT_METRICS metrics{};

    hr =
        textLayout->GetMetrics(
            &metrics);

    if (FAILED(hr))
        return false;

    const float horizontalPadding =
        18.0f;

    const float minimumWidth =
        120.0f;

    const float maximumWidth =
        static_cast<float>(
            impl->width - 20);

    float barWidth =
        metrics.width +
        horizontalPadding;

    barWidth =
        std::clamp(
            barWidth,
            minimumWidth,
            maximumWidth);

    const float right =
        left +
        barWidth;

    const float bottom =
        top +
        barHeight;

    impl->renderTarget->BeginDraw();

    impl->renderTarget->FillRectangle(
        D2D1::RectF(
            left,
            top,
            right,
            bottom),
        impl->backgroundBrush.Get());

    impl->renderTarget->FillRectangle(
        D2D1::RectF(
            left,
            bottom - 1.0f,
            right,
            bottom),
        impl->borderBrush.Get());

    impl->renderTarget->DrawTextLayout(
        D2D1::Point2F(
            left + 9.0f,
            top),
        textLayout.Get(),
        impl->textBrush.Get());

    hr =
        impl->renderTarget->EndDraw();

    if (hr == D2DERR_RECREATE_TARGET)
    {
        impl->renderTarget.Reset();
        return false;
    }

    return SUCCEEDED(hr);
}
