#define NOMINMAX

#include "telemetry.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <iostream>

namespace
{
    using NvmlReturn = int;

    constexpr NvmlReturn NVML_SUCCESS_VALUE = 0;

    /*
        Titan V theoretical Tensor throughput.

        NVIDIA Titan V:
            640 Tensor Cores
            ~110 TFLOPS FP16 Tensor throughput

        This is a theoretical ceiling, not a measured sustained
        application throughput.
    */
    constexpr double kTitanVPeakTensorTFLOPS =
        110.0;

    /*
        The EASU Tensor kernel performs, per 16-pixel block:

            2 WMMA 16x16x16 MMAs per channel
            3 channels

        Therefore:

            6 × (16 × 16 × 16 × 2)
            = 49,152 FLOPs per output block

        For arbitrary output dimensions, calculate the number
        of 16-pixel blocks from the output resolution.
    */
    constexpr double kFLOPsPerTensorBlock =
        49152.0;


    struct NvmlUtilization
    {
        unsigned int gpu;
        unsigned int memory;
    };


    struct NvmlMemory
    {
        unsigned long long total;
        unsigned long long free;
        unsigned long long used;
    };


    using NvmlInitFn =
        NvmlReturn(*)(void);

    using NvmlShutdownFn =
        NvmlReturn(*)(void);

    using NvmlDeviceGetHandleByIndexFn =
        NvmlReturn(*)(unsigned int, void**);

    using NvmlDeviceGetUtilizationRatesFn =
        NvmlReturn(*)(void*, NvmlUtilization*);

    using NvmlDeviceGetTemperatureFn =
        NvmlReturn(*)(void*, unsigned int, unsigned int*);

    using NvmlDeviceGetMemoryInfoFn =
        NvmlReturn(*)(void*, NvmlMemory*);


    static HMODULE gNvmlModule = nullptr;

    static NvmlInitFn gNvmlInit = nullptr;
    static NvmlShutdownFn gNvmlShutdown = nullptr;
    static NvmlDeviceGetHandleByIndexFn gNvmlDeviceGetHandleByIndex = nullptr;
    static NvmlDeviceGetUtilizationRatesFn gNvmlDeviceGetUtilizationRates = nullptr;
    static NvmlDeviceGetTemperatureFn gNvmlDeviceGetTemperature = nullptr;
    static NvmlDeviceGetMemoryInfoFn gNvmlDeviceGetMemoryInfo = nullptr;


    static bool loadNvml()
    {
        if (gNvmlModule)
            return true;

        gNvmlModule =
            LoadLibraryA("nvml.dll");

        if (!gNvmlModule)
            return false;

        gNvmlInit =
            reinterpret_cast<NvmlInitFn>(
                GetProcAddress(
                    gNvmlModule,
                    "nvmlInit_v2"));

        gNvmlShutdown =
            reinterpret_cast<NvmlShutdownFn>(
                GetProcAddress(
                    gNvmlModule,
                    "nvmlShutdown"));

        gNvmlDeviceGetHandleByIndex =
            reinterpret_cast<NvmlDeviceGetHandleByIndexFn>(
                GetProcAddress(
                    gNvmlModule,
                    "nvmlDeviceGetHandleByIndex_v2"));

        gNvmlDeviceGetUtilizationRates =
            reinterpret_cast<NvmlDeviceGetUtilizationRatesFn>(
                GetProcAddress(
                    gNvmlModule,
                    "nvmlDeviceGetUtilizationRates"));

        gNvmlDeviceGetTemperature =
            reinterpret_cast<NvmlDeviceGetTemperatureFn>(
                GetProcAddress(
                    gNvmlModule,
                    "nvmlDeviceGetTemperature"));

        gNvmlDeviceGetMemoryInfo =
            reinterpret_cast<NvmlDeviceGetMemoryInfoFn>(
                GetProcAddress(
                    gNvmlModule,
                    "nvmlDeviceGetMemoryInfo"));

        if (!gNvmlInit ||
            !gNvmlShutdown ||
            !gNvmlDeviceGetHandleByIndex ||
            !gNvmlDeviceGetUtilizationRates ||
            !gNvmlDeviceGetTemperature ||
            !gNvmlDeviceGetMemoryInfo)
        {
            FreeLibrary(
                gNvmlModule);

            gNvmlModule = nullptr;

            gNvmlInit = nullptr;
            gNvmlShutdown = nullptr;
            gNvmlDeviceGetHandleByIndex = nullptr;
            gNvmlDeviceGetUtilizationRates = nullptr;
            gNvmlDeviceGetTemperature = nullptr;
            gNvmlDeviceGetMemoryInfo = nullptr;

            return false;
        }

        return true;
    }
}


struct Telemetry::Impl
{
    bool nvmlInitialized = false;

    void* nvmlDevice = nullptr;

    int tensorOutputWidth = 1920;
    int tensorOutputHeight = 1080;
};


Telemetry::Telemetry()
{
    impl =
        new Impl();
}


Telemetry::~Telemetry()
{
    shutdown();

    delete impl;

    impl =
        nullptr;
}


bool Telemetry::initialize()
{
    if (!impl)
        return false;

    if (!loadNvml())
    {
        std::cerr
            << "[Telemetry] nvml.dll not available.\n";

        return false;
    }

    const NvmlReturn result =
        gNvmlInit();

    if (result !=
        NVML_SUCCESS_VALUE)
    {
        std::cerr
            << "[Telemetry] NVML initialization failed. Code="
            << result
            << '\n';

        return false;
    }

    impl->nvmlInitialized =
        true;

    void* device =
        nullptr;

    if (gNvmlDeviceGetHandleByIndex(
            0,
            &device) !=
        NVML_SUCCESS_VALUE)
    {
        std::cerr
            << "[Telemetry] Failed to get GPU device.\n";

        return false;
    }

    impl->nvmlDevice =
        device;

    snapshot.nvmlAvailable =
        true;

    std::cout
        << "[Telemetry] NVML initialized.\n";

    return true;
}


void Telemetry::shutdown()
{
    if (!impl)
        return;

    if (impl->nvmlInitialized &&
        gNvmlShutdown)
    {
        gNvmlShutdown();
    }

    impl->nvmlInitialized =
        false;

    impl->nvmlDevice =
        nullptr;

    snapshot =
        TelemetrySnapshot{};
}


void Telemetry::setTensorOutputResolution(
    int width,
    int height)
{
    if (!impl)
        return;

    if (width <= 0 ||
        height <= 0)
    {
        return;
    }

    impl->tensorOutputWidth =
        width;

    impl->tensorOutputHeight =
        height;
}


void Telemetry::updateHardware()
{
    if (!impl ||
        !impl->nvmlInitialized ||
        !impl->nvmlDevice)
    {
        return;
    }

    NvmlUtilization utilization{};

    if (gNvmlDeviceGetUtilizationRates(
            impl->nvmlDevice,
            &utilization) ==
        NVML_SUCCESS_VALUE)
    {
        snapshot.gpuUtilization =
            utilization.gpu;
    }

    unsigned int temperature =
        0;

    if (gNvmlDeviceGetTemperature(
            impl->nvmlDevice,
            0,
            &temperature) ==
        NVML_SUCCESS_VALUE)
    {
        snapshot.temperatureC =
            temperature;
    }

    NvmlMemory memory{};

    if (gNvmlDeviceGetMemoryInfo(
            impl->nvmlDevice,
            &memory) ==
        NVML_SUCCESS_VALUE)
    {
        snapshot.vramUsedBytes =
            memory.used;

        snapshot.vramTotalBytes =
            memory.total;
    }
}


void Telemetry::setFrameStats(
    double fps,
    double frameTimeMs,
    double captureMs,
    double upscaleMs,
    double presentMs)
{
    snapshot.fps =
        fps;

    snapshot.frameTimeMs =
        frameTimeMs;

    snapshot.captureMs =
        captureMs;

    snapshot.upscaleMs =
        upscaleMs;

    snapshot.presentMs =
        presentMs;

    /*
        ---------------------------------------------------------
        ESTIMATED TENSOR THROUGHPUT
        ---------------------------------------------------------

        The real EASU Tensor kernel has already been verified to
        compile to Volta HMMA instructions.

        For each 16x16 output block:

            6 WMMA operations
            49,152 FLOPs total

        Estimate achieved Tensor throughput from the amount of
        known Tensor work divided by the measured upscale time.

        Then compare that achieved throughput against the
        Titan V's theoretical ~110 TFLOPS Tensor ceiling.

        IMPORTANT:

        The current presenter supplies the existing UPS timing,
        which includes more than pure kernel execution.

        That makes this a conservative estimate of Tensor
        throughput/headroom.

        It is NOT Tensor pipe occupancy.
    */

    snapshot.tensorPathActive =
        upscaleMs > 0.0;

    if (!snapshot.tensorPathActive)
    {
        snapshot.tensorUtilization = 0;
        return;
    }

    const int blocksX =
        (impl->tensorOutputWidth + 15) / 16;

    const int blocksY =
        impl->tensorOutputHeight;

    const double totalBlocks =
        static_cast<double>(
            blocksX) *
        static_cast<double>(
            blocksY);

    const double totalFLOPs =
        totalBlocks *
        kFLOPsPerTensorBlock;

    const double seconds =
        upscaleMs / 1000.0;

    if (seconds <= 0.0)
    {
        snapshot.tensorUtilization = 0;
        return;
    }

    const double achievedFLOPS =
        totalFLOPs /
        seconds;

    const double achievedTFLOPS =
        achievedFLOPS /
        1.0e12;

    double utilization =
        achievedTFLOPS /
        kTitanVPeakTensorTFLOPS *
        100.0;

    utilization =
        std::clamp(
            utilization,
            0.0,
            100.0);

    snapshot.tensorUtilization =
        static_cast<unsigned int>(
            std::lround(
                utilization));
}


const TelemetrySnapshot&
Telemetry::getSnapshot() const
{
    return snapshot;
}
