#pragma once

#include "scaler.h"

#include <d3d11.h>
#include <windows.h>

class DesktopCapture
{
public:
    struct Impl;

    DesktopCapture();
    ~DesktopCapture();

    bool initialize(
        HWND targetWindow);

    /*
        Wait until WGC reports that a new frame is available.
        Returns false on timeout/error.
    */
    bool waitForFrame(
        int timeoutMs);

    /*
        Get the newest WGC frame as a GPU-resident
        D3D11 texture.

        The caller owns one reference to the returned
        texture and must call Release().
    */
    bool captureTexture(
        ID3D11Texture2D** texture,
        int& width,
        int& height);

    /*
        Return the D3D11 device used by WGC capture.

        Borrowed pointer. Remains valid until shutdown().
    */
    ID3D11Device* getDevice() const;

    /*
        Return the D3D11 immediate context used by WGC capture.

        Borrowed pointer. Remains valid until shutdown().
    */
    ID3D11DeviceContext* getContext() const;

    /*
        Legacy CPU compatibility path.
        Not used by the GPU-native runtime.
    */
    bool capture(
        Image& image);

    void shutdown();

private:
    Impl* impl = nullptr;
};