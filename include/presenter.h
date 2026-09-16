#pragma once

#include <d3d11.h>
#include <windows.h>

class D3D11Presenter
{
public:
    struct Impl;

    D3D11Presenter();
    ~D3D11Presenter();

    bool initialize(
        HWND window,
        int width,
        int height,
        ID3D11Device* device,
        ID3D11DeviceContext* context);

    bool initializeTextureOnly(
        int width,
        int height);

    bool initializeTextureOnly(
        int width,
        int height,
        ID3D11Device* device,
        ID3D11DeviceContext* context);

    void shutdown();

    bool present();

    void setFrameStats(
        double fps,
        double frameTimeMs,
        double captureMs,
        double upscaleMs,
        double presentMs);

    ID3D11Texture2D* getOutputTexture() const;

private:
    Impl* impl = nullptr;
};
