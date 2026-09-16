#pragma once

#include <cstdint>

struct TelemetrySnapshot
{
    double fps = 0.0;
    double frameTimeMs = 0.0;

    double captureMs = 0.0;
    double upscaleMs = 0.0;
    double presentMs = 0.0;

    unsigned int gpuUtilization = 0;
    unsigned int tensorUtilization = 0;
    unsigned int temperatureC = 0;

    std::uint64_t vramUsedBytes = 0;
    std::uint64_t vramTotalBytes = 0;

    bool nvmlAvailable = false;

    // True when the current upscale timing represents
    // the known Tensor EASU workload.
    bool tensorPathActive = false;
};

class Telemetry
{
public:
    Telemetry();
    ~Telemetry();

    bool initialize();
    void shutdown();

    void updateHardware();

    void setTensorOutputResolution(
        int width,
        int height);

    void setFrameStats(
        double fps,
        double frameTimeMs,
        double captureMs,
        double upscaleMs,
        double presentMs);

    const TelemetrySnapshot& getSnapshot() const;

private:
    struct Impl;

    Impl* impl = nullptr;

    TelemetrySnapshot snapshot{};
};
