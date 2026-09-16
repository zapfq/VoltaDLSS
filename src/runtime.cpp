#define NOMINMAX

#include "runtime.h"

#include "capture.h"
#include "easu_tensor.h"
#include "presenter.h"
#include "quality.h"
#include "telemetry.h"

#include <windows.h>

#include <chrono>
#include <cstdlib>
#include <iostream>

#pragma comment(lib, "gdi32.lib")

static HWND gWindow = nullptr;
static HWND gTargetWindow = nullptr;


static QualityPreset getStartupQualityPreset(
    QualityPreset fallback)
{
    const char* value =
        std::getenv("VOLTADLSS_QUALITY");

    if (!value || value[0] == '\0')
        return fallback;

    QualityPreset preset{};

    if (parseQualityPreset(
            value,
            preset))
    {
        std::cout
            << "Startup quality: "
            << getQualityPresetName(preset)
            << '\n';

        return preset;
    }

    std::cerr
        << "Invalid VOLTADLSS_QUALITY: "
        << value
        << '\n';

    std::cerr
        << "Using existing/default preset: "
        << getQualityPresetName(fallback)
        << '\n';

    return fallback;
}


struct MonitorOutputInfo
{
    HMONITOR monitor = nullptr;
    RECT rect{};
    int width = 0;
    int height = 0;
};


static bool getPrimaryMonitor(
    MonitorOutputInfo& info)
{
    POINT point{};

    HMONITOR monitor =
        MonitorFromPoint(
            point,
            MONITOR_DEFAULTTOPRIMARY);

    if (!monitor)
        return false;

    MONITORINFO monitorInfo{};

    monitorInfo.cbSize =
        sizeof(monitorInfo);

    if (!GetMonitorInfoA(
            monitor,
            &monitorInfo))
    {
        return false;
    }

    info.monitor =
        monitor;

    info.rect =
        monitorInfo.rcMonitor;

    info.width =
        info.rect.right -
        info.rect.left;

    info.height =
        info.rect.bottom -
        info.rect.top;

    return
        info.width > 0 &&
        info.height > 0;
}


/*
    -------------------------------------------------------------
    CPU process / thread scheduling
    -------------------------------------------------------------
*/

static void configureRuntimeScheduling()
{
    if (SetPriorityClass(
            GetCurrentProcess(),
            HIGH_PRIORITY_CLASS))
    {
        std::cout
            << "Process CPU priority: HIGH\n";
    }
    else
    {
        std::cerr
            << "SetPriorityClass(HIGH) failed. Error="
            << GetLastError()
            << '\n';
    }

    if (SetThreadPriority(
            GetCurrentThread(),
            THREAD_PRIORITY_HIGHEST))
    {
        std::cout
            << "Runtime thread CPU priority: HIGHEST\n";
    }
    else
    {
        std::cerr
            << "SetThreadPriority(HIGHEST) failed. Error="
            << GetLastError()
            << '\n';
    }
}


/*
    -------------------------------------------------------------
    Window procedure
    -------------------------------------------------------------
*/

static LRESULT CALLBACK windowProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    switch (message)
    {
    case WM_CLOSE:

        DestroyWindow(
            hwnd);

        return 0;

    case WM_DESTROY:

        PostQuitMessage(
            0);

        return 0;
    }

    return DefWindowProcA(
        hwnd,
        message,
        wParam,
        lParam);
}


/*
    -------------------------------------------------------------
    Runtime output window
    -------------------------------------------------------------
*/

static bool createRuntimeWindow(
    int width,
    int height)
{
    const char* className =
        "VoltaDLSSRuntimeWindow";

    WNDCLASSA wc{};

    wc.lpfnWndProc =
        windowProc;

    wc.hInstance =
        GetModuleHandleA(nullptr);

    wc.lpszClassName =
        className;

    wc.hCursor =
        LoadCursor(
            nullptr,
            IDC_ARROW);

    wc.hbrBackground =
        reinterpret_cast<HBRUSH>(
            GetStockObject(
                BLACK_BRUSH));

    if (!RegisterClassA(
            &wc))
    {
        if (GetLastError() !=
            ERROR_CLASS_ALREADY_EXISTS)
        {
            std::cerr
                << "RegisterClass failed. Error="
                << GetLastError()
                << '\n';

            return false;
        }
    }

    const DWORD exStyle =
        WS_EX_APPWINDOW;

    const DWORD style =
        WS_POPUP;

    MonitorOutputInfo monitor{};

    if (!getPrimaryMonitor(
            monitor))
    {
        std::cerr
            << "Failed to determine primary monitor.\n";

        return false;
    }

    gWindow =
        CreateWindowExA(
            exStyle,
            className,
            "VoltaDLSS",
            style,
            monitor.rect.left,
            monitor.rect.top,
            monitor.width,
            monitor.height,
            nullptr,
            nullptr,
            GetModuleHandleA(nullptr),
            nullptr);

    if (!gWindow)
    {
        std::cerr
            << "CreateWindowEx failed. Error="
            << GetLastError()
            << '\n';

        return false;
    }

    if (!SetWindowPos(
            gWindow,
            HWND_TOP,
            monitor.rect.left,
            monitor.rect.top,
            monitor.width,
            monitor.height,
            SWP_FRAMECHANGED |
            SWP_SHOWWINDOW))
    {
        std::cerr
            << "SetWindowPos failed. Error="
            << GetLastError()
            << '\n';

        DestroyWindow(
            gWindow);

        gWindow =
            nullptr;

        return false;
    }

    ShowWindow(
        gWindow,
        SW_SHOW);

    UpdateWindow(
        gWindow);

    RECT client{};

    GetClientRect(
        gWindow,
        &client);

    std::cout
        << "=====================================\n"
        << "       VoltaDLSS Output Window\n"
        << "=====================================\n";

    std::cout
        << "Output client:    "
        << client.right -
               client.left
        << " x "
        << client.bottom -
               client.top
        << '\n';

    std::cout
        << "Monitor:          "
        << monitor.width
        << " x "
        << monitor.height
        << '\n';

    std::cout
        << "Window mode:      borderless fullscreen\n";

    std::cout
        << "Target window:    background/capture target\n";

    std::cout
        << "Presentation:     normal activatable window\n";

    std::cout
        << "Mouse:            handled by VoltaDLSS\n";

    std::cout
        << "Virtual input:    will be handled by HID layer\n";

    std::cout
        << "ESC exit:         removed\n";

    SetForegroundWindow(
        gWindow);

    SetFocus(
        gWindow);

    return true;
}


/*
    -------------------------------------------------------------
    Find target window
    -------------------------------------------------------------
*/

static HWND findTargetWindow()
{
    const char* value =
        std::getenv(
            "VOLTADLSS_TARGET");

    if (!value ||
        value[0] == '\0')
    {
        std::cerr
            << "VOLTADLSS_TARGET is not set.\n";

        return nullptr;
    }

    char* endPtr = nullptr;

    const unsigned long pid =
        std::strtoul(
            value,
            &endPtr,
            10);

    if (endPtr == value ||
        *endPtr != '\0' ||
        pid == 0)
    {
        std::cerr
            << "Invalid VOLTADLSS_TARGET PID:\n"
            << value
            << '\n';

        return nullptr;
    }

    struct SearchContext
    {
        DWORD pid;
        HWND result;
    };

    SearchContext context{
        static_cast<DWORD>(pid),
        nullptr
    };

    EnumWindows(
        [](HWND window, LPARAM parameter) -> BOOL
        {
            auto* context =
                reinterpret_cast<SearchContext*>(
                    parameter);

            DWORD windowPid = 0;

            GetWindowThreadProcessId(
                window,
                &windowPid);

            if (windowPid !=
                context->pid)
            {
                return TRUE;
            }

            if (!IsWindowVisible(
                    window))
            {
                return TRUE;
            }

            if (GetWindow(
                    window,
                    GW_OWNER) != nullptr)
            {
                return TRUE;
            }

            context->result =
                window;

            return FALSE;
        },
        reinterpret_cast<LPARAM>(
            &context));

    HWND hwnd =
        context.result;

    if (!hwnd)
    {
        std::cerr
            << "Could not find visible target window for PID:\n"
            << pid
            << '\n';

        return nullptr;
    }

    char title[512]{};

    GetWindowTextA(
        hwnd,
        title,
        static_cast<int>(
            sizeof(title)));

    std::cout
        << "Target window: "
        << (title[0]
                ? title
                : "<untitled>")
        << '\n';

    std::cout
        << "Target PID: "
        << pid
        << '\n';

    return hwnd;
}


/*
    -------------------------------------------------------------
    Tensor pipeline
    -------------------------------------------------------------
*/

static bool initializePipelineForSource(
    EasuTensorPipeline& pipeline,
    int sourceWidth,
    int sourceHeight,
    int outputWidth,
    int outputHeight,
    ID3D11Texture2D* outputTexture)
{
    destroyEasuTensorPipeline(
        pipeline);

    if (!initEasuTensorPipeline(
            pipeline,
            sourceWidth,
            sourceHeight,
            outputWidth,
            outputHeight))
    {
        return false;
    }

    if (!attachEasuTensorOutputTexture(
            pipeline,
            outputTexture))
    {
        destroyEasuTensorPipeline(
            pipeline);

        return false;
    }

    return true;
}


/*
    -------------------------------------------------------------
    Runtime
    -------------------------------------------------------------
*/

int runVoltaDLSSRuntime(
    int requestedOutputWidth,
    int requestedOutputHeight,
    QualityPreset preset)
{
    configureRuntimeScheduling();

    preset =
        getStartupQualityPreset(
            preset);

    MonitorOutputInfo monitor{};

    if (!getPrimaryMonitor(
            monitor))
    {
        std::cerr
            << "Failed to determine primary monitor.\n";

        return 1;
    }

    int outputWidth =
        requestedOutputWidth;

    int outputHeight =
        requestedOutputHeight;

    if (outputWidth <= 0 ||
        outputHeight <= 0)
    {
        outputWidth =
            monitor.width;

        outputHeight =
            monitor.height;
    }

    int desiredRenderWidth = 0;
    int desiredRenderHeight = 0;

    getQualityResolution(
        preset,
        outputWidth,
        outputHeight,
        desiredRenderWidth,
        desiredRenderHeight);

    std::cout
        << "=====================================\n"
        << "          VoltaDLSS RENDER TEST\n"
        << "=====================================\n\n";

    std::cout
        << "Preset:          "
        << getQualityPresetName(preset)
        << '\n';

    std::cout
        << "Scale:           "
        << getQualityScale(preset) *
               100.0f
        << "%\n";

    std::cout
        << "Desired render:  "
        << desiredRenderWidth
        << " x "
        << desiredRenderHeight
        << '\n';

    std::cout
        << "Output:          "
        << outputWidth
        << " x "
        << outputHeight
        << '\n';

    std::cout
        << "Window mode:     borderless fullscreen\n";

    std::cout
        << "Presentation:    normal activatable\n";

    std::cout
        << "Input:           VoltaDLSS foreground\n";

    std::cout
        << "Virtual input:   will be enabled\n";

    std::cout
        << "ESC exit:        removed\n";

    HWND targetWindow =
        findTargetWindow();

    if (!targetWindow)
        return 1;

    gTargetWindow =
        targetWindow;

    DesktopCapture capture;

    if (!capture.initialize(
            targetWindow))
    {
        std::cerr
            << "Failed to initialize WGC capture.\n";

        return 1;
    }

    std::cout
        << "WGC capture ready.\n";

    if (!capture.waitForFrame(
            2000))
    {
        std::cerr
            << "Timed out waiting for first WGC frame.\n";

        capture.shutdown();

        return 1;
    }

    ID3D11Texture2D*
        firstTexture =
            nullptr;

    int sourceWidth = 0;
    int sourceHeight = 0;

    if (!capture.captureTexture(
            &firstTexture,
            sourceWidth,
            sourceHeight))
    {
        std::cerr
            << "Failed to acquire first captured texture.\n";

        capture.shutdown();

        return 1;
    }

    if (!firstTexture)
    {
        capture.shutdown();

        return 1;
    }

    std::cout
        << "Actual source:   "
        << sourceWidth
        << " x "
        << sourceHeight
        << '\n';

    if (sourceWidth !=
            desiredRenderWidth ||
        sourceHeight !=
            desiredRenderHeight)
    {
        std::cout
            << "Preset target:   "
            << desiredRenderWidth
            << " x "
            << desiredRenderHeight
            << '\n';

        std::cout
            << "Note: game is not currently rendering at the "
               "preset target.\n";
    }
    else
    {
        std::cout
            << "Preset source target matched.\n";
    }

    if (!createRuntimeWindow(
            outputWidth,
            outputHeight))
    {
        firstTexture->Release();

        capture.shutdown();

        return 1;
    }

    D3D11Presenter presenter;

    if (!presenter.initialize(
            gWindow,
            outputWidth,
            outputHeight,
            capture.getDevice(),
            capture.getContext()))
    {
        std::cerr
            << "Failed to initialize D3D11 presenter.\n";

        firstTexture->Release();

        presenter.shutdown();
        capture.shutdown();

        DestroyWindow(
            gWindow);

        gWindow =
            nullptr;

        return 1;
    }

    EasuTensorPipeline pipeline;

    if (!initializePipelineForSource(
            pipeline,
            sourceWidth,
            sourceHeight,
            outputWidth,
            outputHeight,
            presenter.getOutputTexture()))
    {
        std::cerr
            << "Failed to initialize Tensor pipeline.\n";

        firstTexture->Release();

        presenter.shutdown();
        capture.shutdown();

        DestroyWindow(
            gWindow);

        gWindow =
            nullptr;

        return 1;
    }

    std::cout
        << "Tensor source:   "
        << sourceWidth
        << " x "
        << sourceHeight
        << '\n';

    std::cout
        << "Tensor output:   "
        << outputWidth
        << " x "
        << outputHeight
        << '\n';

    float kernelMs =
        0.0f;

    if (!upscaleEasuTensorD3D11(
            pipeline,
            firstTexture,
            presenter.getOutputTexture(),
            &kernelMs))
    {
        std::cerr
            << "Failed to process first frame.\n";

        firstTexture->Release();

        destroyEasuTensorPipeline(
            pipeline);

        presenter.shutdown();
        capture.shutdown();

        DestroyWindow(
            gWindow);

        gWindow =
            nullptr;

        return 1;
    }

    firstTexture->Release();

    if (!presenter.present())
    {
        std::cerr
            << "Failed to present first frame.\n";

        destroyEasuTensorPipeline(
            pipeline);

        presenter.shutdown();
        capture.shutdown();

        DestroyWindow(
            gWindow);

        gWindow =
            nullptr;

        return 1;
    }

    if (gWindow &&
        IsWindow(gWindow))
    {
        ShowWindow(
            gWindow,
            SW_SHOW);

        SetWindowPos(
            gWindow,
            HWND_TOP,
            monitor.rect.left,
            monitor.rect.top,
            monitor.width,
            monitor.height,
            SWP_FRAMECHANGED |
            SWP_SHOWWINDOW);

        SetForegroundWindow(
            gWindow);

        SetFocus(
            gWindow);

        std::cout
            << "VoltaDLSS output window is now foreground.\n";
    }

    bool running =
        true;

    std::uint64_t intervalFrames =
        0;

    int captureTimeouts =
        0;

    int processErrors =
        0;

    double captureWaitTotalMs =
        0.0;

    double tensorTotalMs =
        0.0;

    double presentTotalMs =
        0.0;

    auto statsStart =
        std::chrono::steady_clock::now();

    int lastWidth =
        sourceWidth;

    int lastHeight =
        sourceHeight;

    std::cout
        << "\nRuntime started.\n";

    std::cout
        << "Target application = capture target only.\n";

    std::cout
        << "VoltaDLSS = foreground borderless fullscreen window.\n";

    std::cout
        << "Mouse/keyboard = VoltaDLSS foreground.\n";

    std::cout
        << "Virtual input = intended for game forwarding.\n";

    std::cout
        << "ESC exit = removed.\n\n";

    while (running)
    {
        MSG message{};

        while (PeekMessageA(
                   &message,
                   nullptr,
                   0,
                   0,
                   PM_REMOVE))
        {
            if (message.message ==
                WM_QUIT)
            {
                running =
                    false;

                break;
            }

            TranslateMessage(
                &message);

            DispatchMessageA(
                &message);
        }

        if (!running)
            break;

        const auto captureWaitStart =
            std::chrono::steady_clock::now();

        if (!capture.waitForFrame(
                1000))
        {
            ++captureTimeouts;

            continue;
        }

        const auto captureWaitEnd =
            std::chrono::steady_clock::now();

        const double captureMs =
            std::chrono::duration<double,
                std::milli>(
                captureWaitEnd -
                captureWaitStart).count();

        captureWaitTotalMs +=
            captureMs;

        ID3D11Texture2D*
            capturedTexture =
                nullptr;

        int capturedWidth =
            0;

        int capturedHeight =
            0;

        if (!capture.captureTexture(
                &capturedTexture,
                capturedWidth,
                capturedHeight))
        {
            ++processErrors;

            continue;
        }

        if (!capturedTexture)
        {
            ++processErrors;

            continue;
        }

        if (capturedWidth !=
                lastWidth ||
            capturedHeight !=
                lastHeight)
        {
            std::cout
                << "\nSource changed: "
                << lastWidth
                << "x"
                << lastHeight
                << " -> "
                << capturedWidth
                << "x"
                << capturedHeight
                << '\n';

            if (!initializePipelineForSource(
                    pipeline,
                    capturedWidth,
                    capturedHeight,
                    outputWidth,
                    outputHeight,
                    presenter.getOutputTexture()))
            {
                capturedTexture->Release();

                ++processErrors;

                running =
                    false;

                continue;
            }

            lastWidth =
                capturedWidth;

            lastHeight =
                capturedHeight;

            std::cout
                << "Tensor pipeline: "
                << lastWidth
                << "x"
                << lastHeight
                << " -> "
                << outputWidth
                << "x"
                << outputHeight
                << '\n';
        }

        const auto tensorStart =
            std::chrono::steady_clock::now();

        const bool processed =
            upscaleEasuTensorD3D11(
                pipeline,
                capturedTexture,
                presenter.getOutputTexture(),
                &kernelMs);

        const auto tensorEnd =
            std::chrono::steady_clock::now();

        capturedTexture->Release();

        const double upscaleMs =
            std::chrono::duration<double,
                std::milli>(
                tensorEnd -
                tensorStart).count();

        tensorTotalMs +=
            upscaleMs;

        if (!processed)
        {
            ++processErrors;

            continue;
        }

        const auto presentStart =
            std::chrono::steady_clock::now();

        const bool presented =
            presenter.present();

        const auto presentEnd =
            std::chrono::steady_clock::now();

        const double presentMs =
            std::chrono::duration<double,
                std::milli>(
                presentEnd -
                presentStart).count();

        presentTotalMs +=
            presentMs;

        if (!presented)
        {
            ++processErrors;

            continue;
        }

        ++intervalFrames;

        const auto now =
            std::chrono::steady_clock::now();

        const double elapsed =
            std::chrono::duration<double>(
                now -
                statsStart).count();

        if (elapsed >= 1.0)
        {
            const double fps =
                static_cast<double>(
                    intervalFrames) /
                elapsed;

            const double frameTimeMs =
                fps > 0.0
                    ? 1000.0 / fps
                    : 0.0;

            const double avgCapture =
                intervalFrames > 0
                    ? captureWaitTotalMs /
                      static_cast<double>(
                          intervalFrames)
                    : 0.0;

            const double avgUpscale =
                intervalFrames > 0
                    ? tensorTotalMs /
                      static_cast<double>(
                          intervalFrames)
                    : 0.0;

            const double avgPresent =
                intervalFrames > 0
                    ? presentTotalMs /
                      static_cast<double>(
                          intervalFrames)
                    : 0.0;

            /*
                Feed the exact runtime measurements into
                the telemetry system.

                Hardware metrics are updated by presenter.present().
            */

            /*
                The presenter owns the Telemetry object, so the
                frame statistics are passed through its public
                telemetry update path below.
            */

            presenter.setFrameStats(
                fps,
                frameTimeMs,
                avgCapture,
                avgUpscale,
                avgPresent);

            std::cout
                << "\rFPS: "
                << fps
                << " | Source: "
                << lastWidth
                << "x"
                << lastHeight
                << " | Output: "
                << outputWidth
                << "x"
                << outputHeight
                << " | Preset: "
                << getQualityPresetName(preset)
                << " | CAP: "
                << avgCapture
                << " ms"
                << " | UPS: "
                << avgUpscale
                << " ms"
                << " | Kernel: "
                << kernelMs
                << " ms"
                << " | PRE: "
                << avgPresent
                << " ms"
                << " | Timeouts: "
                << captureTimeouts
                << " | Errors: "
                << processErrors
                << "          "
                << std::flush;

            intervalFrames =
                0;

            captureTimeouts =
                0;

            processErrors =
                0;

            captureWaitTotalMs =
                0.0;

            tensorTotalMs =
                0.0;

            presentTotalMs =
                0.0;

            statsStart =
                now;
        }
    }

    std::cout
        << '\n';

    destroyEasuTensorPipeline(
        pipeline);

    presenter.shutdown();

    capture.shutdown();

    if (gWindow)
    {
        DestroyWindow(
            gWindow);

        gWindow =
            nullptr;
    }

    gTargetWindow =
        nullptr;

    return 0;
}
