#pragma once

#include <cstdint>
#include <vector>

struct Image
{
    int width = 0;
    int height = 0;

    // RGBA8, 4 bytes per pixel.
    std::vector<uint8_t> pixels;
};

enum class TestPattern
{
    Gradient,
    Checkerboard,
    VerticalEdge,
    HorizontalEdge,
    DiagonalEdge,
    Noise
};

Image createTestImage(
    int width,
    int height,
    TestPattern pattern);

Image upscaleBilinear(
    const Image& input,
    int outputWidth,
    int outputHeight);
