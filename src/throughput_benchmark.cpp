#define NOMINMAX

#include "capture.h"
#include "easu_tensor.h"
#include "presenter.h"

#include <windows.h>

#include <cuda_runtime.h>

#include <chrono>
#include <cstdlib>
#include <iostream>

static HWND findTargetWindow()
{
    const char* title =
        std::getenv(
            "VOLTADLSS_TARGET");

    if (!title ||
        title[0] == '\0')
    {
        std::cerr
            << "VOLTADLSS_TARGET is not set.\n"
            << "Example:\n"
            << "  $env:VOLTADLSS_TARGET=\"DSCF0236.MOV - PotPlayer\"\n";

        return nullptr;
    }

    HWND hwnd =
        FindWindowA(
            nullptr,
            title);

    if (!hwnd)
    {
        std::cerr
            << "Could not find target window:\n"
            << "  "
            << title
            << '\n';

        return nullptr;
    }

    if (!IsWindowVisible(hwnd))
    {
        std::cerr
            << "Target window is not visible.\n";

        return nullptr;
    }

    std::cout
        << "Target window: "
        << title
        << '\n';

    return hwnd;
}

int main()
{
    constexpr int inputWidth =
        960;

    constexpr int inputHeight =
        540;

    constexpr int outputWidth =
        1920;

    constexpr int outputHeight =
        1080;

    constexpr int warmupIterations =
        30;

    constexpr int benchmarkIterations =
        300;

    std::cout
        << "=====================================\n"
        << "     VoltaDLSS GPU Throughput Test\n"
        << "=====================================\n\n";

    std::cout
        << "Input:              "
        << inputWidth
        << " x "
        << inputHeight
        << '\n';

    std::cout
        << "Output:             "
        << outputWidth
        << " x "
        << outputHeight
        << '\n';

    std::cout
        << "Warmup iterations:  "
        << warmupIterations
        << '\n';

    std::cout
        << "Benchmark frames:   "
        << benchmarkIterations
        << "\n\n";

    HWND targetWindow =
        findTargetWindow();

    if (!targetWindow)
        return 1;

    DesktopCapture capture;

    if (!capture.initialize(
            targetWindow))
    {
        std::cerr
            << "Failed to initialize WGC capture.\n";

        return 1;
    }

    std::cout
        << "WGC initialized.\n";

    /*
        Wait for one real captured frame.
    */
    if (!capture.waitForFrame(
            2000))
    {
        std::cerr
            << "Timed out waiting for first WGC frame.\n";

        capture.shutdown();

        return 1;
    }

    ID3D11Texture2D*
        capturedTexture =
            nullptr;

    int capturedWidth = 0;
    int capturedHeight = 0;

    if (!capture.captureTexture(
            &capturedTexture,
            capturedWidth,
            capturedHeight))
    {
        std::cerr
            << "Failed to acquire first capture texture.\n";

        capture.shutdown();

        return 1;
    }

    if (!capturedTexture)
    {
        std::cerr
            << "Captured texture is null.\n";

        capture.shutdown();

        return 1;
    }

    std::cout
        << "Captured: "
        << capturedWidth
        << " x "
        << capturedHeight
        << '\n';

    if (capturedWidth != inputWidth ||
        capturedHeight != inputHeight)
    {
        std::cerr
            << "Unexpected capture resolution.\n"
            << "Expected: "
            << inputWidth
            << " x "
            << inputHeight
            << '\n';

        capturedTexture->Release();

        capture.shutdown();

        return 1;
    }

    /*
        We only need a GPU output texture here.
        No visible presentation window.
    */
    D3D11Presenter presenter;

    if (!presenter.initializeTextureOnly(
            outputWidth,
            outputHeight))
    {
        std::cerr
            << "Failed to initialize texture-only presenter.\n";

        capturedTexture->Release();
        capture.shutdown();

        return 1;
    }

    EasuTensorPipeline pipeline;

    if (!initEasuTensorPipeline(
            pipeline,
            inputWidth,
            inputHeight,
            outputWidth,
            outputHeight))
    {
        std::cerr
            << "Failed to initialize Tensor pipeline.\n";

        capturedTexture->Release();

        presenter.shutdown();
        capture.shutdown();

        return 1;
    }

    if (!attachEasuTensorOutputTexture(
            pipeline,
            presenter.getOutputTexture()))
    {
        std::cerr
            << "Failed to attach output texture.\n";

        capturedTexture->Release();

        destroyEasuTensorPipeline(
            pipeline);

        presenter.shutdown();
        capture.shutdown();

        return 1;
    }

    float kernelMs =
        0.0f;

    /*
        First GPU pass.

        This also registers the WGC texture with CUDA.
    */
    std::cout
        << "Initializing GPU path...\n";

    if (!upscaleEasuTensorD3D11(
            pipeline,
            capturedTexture,
            presenter.getOutputTexture(),
            &kernelMs))
    {
        std::cerr
            << "Initial GPU processing failed.\n";

        capturedTexture->Release();

        destroyEasuTensorPipeline(
            pipeline);

        presenter.shutdown();
        capture.shutdown();

        return 1;
    }

    std::cout
        << "GPU path initialized.\n";

    /*
        Warmup.
    */
    std::cout
        << "Warming up...\n";

    for (int i = 0;
         i < warmupIterations;
         ++i)
    {
        if (!upscaleEasuTensorD3D11(
                pipeline,
                capturedTexture,
                presenter.getOutputTexture(),
                &kernelMs))
        {
            std::cerr
                << "Warmup failed at iteration "
                << i
                << ".\n";

            capturedTexture->Release();

            destroyEasuTensorPipeline(
                pipeline);

            presenter.shutdown();
            capture.shutdown();

            return 1;
        }
    }

    /*
        Wait until all GPU work is complete before
        starting the benchmark clock.
    */
    cudaError_t cudaError =
        cudaDeviceSynchronize();

    if (cudaError != cudaSuccess)
    {
        std::cerr
            << "cudaDeviceSynchronize failed before benchmark: "
            << cudaGetErrorString(
                cudaError)
            << '\n';

        capturedTexture->Release();

        destroyEasuTensorPipeline(
            pipeline);

        presenter.shutdown();
        capture.shutdown();

        return 1;
    }

    std::cout
        << "Running benchmark...\n\n";

    const auto start =
        std::chrono::high_resolution_clock::now();

    double totalKernelMs =
        0.0;

    for (int i = 0;
         i < benchmarkIterations;
         ++i)
    {
        float currentKernelMs =
            0.0f;

        if (!upscaleEasuTensorD3D11(
                pipeline,
                capturedTexture,
                presenter.getOutputTexture(),
                &currentKernelMs))
        {
            std::cerr
                << "Benchmark failed at iteration "
                << i
                << ".\n";

            capturedTexture->Release();

            destroyEasuTensorPipeline(
                pipeline);

            presenter.shutdown();
            capture.shutdown();

            return 1;
        }

        totalKernelMs +=
            static_cast<double>(
                currentKernelMs);
    }

    cudaError =
        cudaDeviceSynchronize();

    if (cudaError != cudaSuccess)
    {
        std::cerr
            << "cudaDeviceSynchronize failed after benchmark: "
            << cudaGetErrorString(
                cudaError)
            << '\n';

        capturedTexture->Release();

        destroyEasuTensorPipeline(
            pipeline);

        presenter.shutdown();
        capture.shutdown();

        return 1;
    }

    const auto end =
        std::chrono::high_resolution_clock::now();

    const double totalMs =
        std::chrono::duration<double,
            std::milli>(
            end - start).count();

    const double averageFrameMs =
        totalMs /
        static_cast<double>(
            benchmarkIterations);

    const double throughputFps =
        1000.0 /
        averageFrameMs;

    const double averageKernelMs =
        totalKernelMs /
        static_cast<double>(
            benchmarkIterations);

    const double interopAndOtherMs =
        averageFrameMs -
        averageKernelMs;

    std::cout
        << "=====================================\n"
        << "              RESULTS\n"
        << "=====================================\n\n";

    std::cout
        << "Frames:              "
        << benchmarkIterations
        << '\n';

    std::cout
        << "Total time:          "
        << totalMs
        << " ms\n";

    std::cout
        << "Average frame:       "
        << averageFrameMs
        << " ms\n";

    std::cout
        << "EASU kernel:         "
        << averageKernelMs
        << " ms\n";

    std::cout
        << "Interop/other:       "
        << interopAndOtherMs
        << " ms\n";

    std::cout
        << "GPU throughput:      "
        << throughputFps
        << " FPS\n\n";

    std::cout
        << "WGC frame arrival:    NOT measured\n";

    std::cout
        << "CPU pixel readback:    NOT used\n";

    std::cout
        << "CPU output conversion: NOT used\n";

    std::cout
        << "Swapchain Present:     NOT measured\n";

    capturedTexture->Release();

    destroyEasuTensorPipeline(
        pipeline);

    presenter.shutdown();
    capture.shutdown();

    return 0;
}