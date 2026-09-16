#include "scaler.h"

#include <cuda_runtime.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <random>

#define CUDA_CHECK(call)                                                   \
    do                                                                     \
    {                                                                      \
        cudaError_t err = (call);                                          \
        if (err != cudaSuccess)                                            \
        {                                                                   \
            std::cerr << "CUDA error: "                                            \
                      << cudaGetErrorString(err)                           \
                      << " at " << __FILE__                                \
                      << ":" << __LINE__ << '\n';                           \
            std::exit(EXIT_FAILURE);                                        \
        }                                                                   \
    } while (0)

__device__
uint8_t floatToByte(float value)
{
    value = fminf(fmaxf(value, 0.0f), 255.0f);
    return static_cast<uint8_t>(value + 0.5f);
}

__global__
void bilinearKernel(
    const uint8_t* input,
    int inputWidth,
    int inputHeight,
    uint8_t* output,
    int outputWidth,
    int outputHeight)
{
    const int x =
        blockIdx.x * blockDim.x + threadIdx.x;

    const int y =
        blockIdx.y * blockDim.y + threadIdx.y;

    if (x >= outputWidth || y >= outputHeight)
        return;

    float srcX =
        (static_cast<float>(x) + 0.5f) *
        static_cast<float>(inputWidth) /
        static_cast<float>(outputWidth) - 0.5f;

    float srcY =
        (static_cast<float>(y) + 0.5f) *
        static_cast<float>(inputHeight) /
        static_cast<float>(outputHeight) - 0.5f;

    srcX = fminf(
        fmaxf(srcX, 0.0f),
        static_cast<float>(inputWidth - 1));

    srcY = fminf(
        fmaxf(srcY, 0.0f),
        static_cast<float>(inputHeight - 1));

    const float srcXFloor = floorf(srcX);
    const float srcYFloor = floorf(srcY);

    const int x0 =
        static_cast<int>(srcXFloor);

    const int y0 =
        static_cast<int>(srcYFloor);

    const int x1 =
        min(inputWidth - 1, x0 + 1);

    const int y1 =
        min(inputHeight - 1, y0 + 1);

    const float fx =
        srcX - srcXFloor;

    const float fy =
        srcY - srcYFloor;

    const size_t p00 =
        (static_cast<size_t>(y0) * inputWidth + x0) * 4;

    const size_t p10 =
        (static_cast<size_t>(y0) * inputWidth + x1) * 4;

    const size_t p01 =
        (static_cast<size_t>(y1) * inputWidth + x0) * 4;

    const size_t p11 =
        (static_cast<size_t>(y1) * inputWidth + x1) * 4;

    const size_t dst =
        (static_cast<size_t>(y) * outputWidth + x) * 4;

    for (int channel = 0; channel < 4; ++channel)
    {
        const float v00 =
            static_cast<float>(input[p00 + channel]);

        const float v10 =
            static_cast<float>(input[p10 + channel]);

        const float v01 =
            static_cast<float>(input[p01 + channel]);

        const float v11 =
            static_cast<float>(input[p11 + channel]);

        const float top =
            v00 + (v10 - v00) * fx;

        const float bottom =
            v01 + (v11 - v01) * fx;

        const float value =
            top + (bottom - top) * fy;

        output[dst + channel] =
            floatToByte(value);
    }
}

Image createTestImage(
    int width,
    int height,
    TestPattern pattern)
{
    Image image;

    image.width = width;
    image.height = height;

    image.pixels.resize(
        static_cast<size_t>(width) *
        static_cast<size_t>(height) *
        4);

    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> noise(0, 255);

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            uint8_t r = 0;
            uint8_t g = 0;
            uint8_t b = 0;

            switch (pattern)
            {
            case TestPattern::Gradient:
            {
                const float fx =
                    static_cast<float>(x) /
                    static_cast<float>(width - 1);

                const float fy =
                    static_cast<float>(y) /
                    static_cast<float>(height - 1);

                r = static_cast<uint8_t>(fx * 255.0f);
                g = static_cast<uint8_t>(fy * 255.0f);
                b = static_cast<uint8_t>(
                    ((fx + fy) * 0.5f) * 255.0f);

                break;
            }

            case TestPattern::Checkerboard:
            {
                const bool checker =
                    ((x / 16) + (y / 16)) % 2 == 0;

                r = checker ? 255 : 0;
                g = checker ? 255 : 0;
                b = checker ? 255 : 0;

                break;
            }

            case TestPattern::VerticalEdge:
            {
                const bool rightSide =
                    x >= width / 2;

                r = rightSide ? 255 : 0;
                g = rightSide ? 32 : 224;
                b = rightSide ? 255 : 0;

                break;
            }

            case TestPattern::HorizontalEdge:
            {
                const bool bottomSide =
                    y >= height / 2;

                r = bottomSide ? 255 : 0;
                g = bottomSide ? 32 : 224;
                b = bottomSide ? 0 : 255;

                break;
            }

            case TestPattern::DiagonalEdge:
            {
                const bool side =
                    x > y;

                r = side ? 255 : 0;
                g = side ? 0 : 255;
                b = 64;

                break;
            }

            case TestPattern::Noise:
            {
                r = static_cast<uint8_t>(noise(rng));
                g = static_cast<uint8_t>(noise(rng));
                b = static_cast<uint8_t>(noise(rng));

                break;
            }
            }

            const size_t index =
                (static_cast<size_t>(y) * width +
                 static_cast<size_t>(x)) * 4;

            image.pixels[index + 0] = r;
            image.pixels[index + 1] = g;
            image.pixels[index + 2] = b;
            image.pixels[index + 3] = 255;
        }
    }

    return image;
}

Image upscaleBilinear(
    const Image& input,
    int outputWidth,
    int outputHeight)
{
    Image output;

    output.width = outputWidth;
    output.height = outputHeight;

    output.pixels.resize(
        static_cast<size_t>(outputWidth) *
        static_cast<size_t>(outputHeight) *
        4);

    const size_t inputBytes =
        input.pixels.size();

    const size_t outputBytes =
        output.pixels.size();

    uint8_t* d_input = nullptr;
    uint8_t* d_output = nullptr;

    CUDA_CHECK(cudaMalloc(
        &d_input,
        inputBytes));

    CUDA_CHECK(cudaMalloc(
        &d_output,
        outputBytes));

    CUDA_CHECK(cudaMemcpy(
        d_input,
        input.pixels.data(),
        inputBytes,
        cudaMemcpyHostToDevice));

    dim3 threads(16, 16);

    dim3 blocks(
        (outputWidth + threads.x - 1) / threads.x,
        (outputHeight + threads.y - 1) / threads.y);

    bilinearKernel<<<blocks, threads>>>(
        d_input,
        input.width,
        input.height,
        d_output,
        outputWidth,
        outputHeight);

    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());

    CUDA_CHECK(cudaMemcpy(
        output.pixels.data(),
        d_output,
        outputBytes,
        cudaMemcpyDeviceToHost));

    CUDA_CHECK(cudaFree(d_input));
    CUDA_CHECK(cudaFree(d_output));

    return output;
}

