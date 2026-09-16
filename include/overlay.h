#pragma once

#include <d3d11.h>
#include <windows.h>

#include "telemetry.h"

class VoltaOverlay
{
public:
    VoltaOverlay();
    ~VoltaOverlay();

    bool initialize(
        ID3D11Texture2D* backBuffer,
        int width,
        int height);

    void shutdown();

    bool draw(
        const TelemetrySnapshot& telemetry);

private:
    struct Impl;

    Impl* impl = nullptr;
};
