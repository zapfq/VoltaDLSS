#include "adaptive_scaler.h"

#include <cuda_runtime.h>
#include <cuda_fp16.h>
#include <mma.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace nvcuda;

#define CUDA_CHECK(call)                                                   \
    do                                                                     \
    {                                                                      \
        cudaError_t err = (call);                                          \
        if (err != cudaSuccess)                                            \
        {                                                                   \
            std::cerr << "CUDA error: "                                         \
                      << cudaGetErrorString(err)                            \
                      << " at " << __FILE__                                 \
                      << ":" << __LINE__ << '\n';                            \
            std::exit(EXIT_FAILURE);                                        \
        }                                                                   \
    } while (0)

constexpr int TILE = 16;
constexpr int TAPS = 9;

// ------------------------------------------------------------
// Common mathematical filter.
// ------------------------------------------------------------

struct FilterWeights
{
    float w[9];
};

__host__ __device__
int adaptiveClampInt(
    int x,
    int lo,
    int hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

__host__ __device__
float luma(
    float r,
    float g,
    float b)
{
    return
        0.299f * r +
        0.587f * g +
        0.114f * b;
}

__host__ __device__
float gaussian(float distance)
{
    // sigma ~= 0.8
    return expf(
        -(distance * distance) /
        (2.0f * 0.8f * 0.8f));
}

template <typename Reader>
__host__ __device__
FilterWeights makeWeights(
    Reader readLuma,
    float srcX,
    float srcY)
{
    FilterWeights result{};

    const int cx =
        static_cast<int>(floorf(srcX));

    const int cy =
        static_cast<int>(floorf(srcY));

    const float center =
        readLuma(cx, cy);

    // Simple edge-strength estimate around the source point.
    const float gx =
        fabsf(
            readLuma(cx + 1, cy) -
            readLuma(cx - 1, cy));

    const float gy =
        fabsf(
            readLuma(cx, cy + 1) -
            readLuma(cx, cy - 1));

    const float edge =
        sqrtf(
            gx * gx +
            gy * gy) / 255.0f;

    // Stronger edge => stronger preference for similar samples.
    const float edgeStrength =
        0.5f + 2.5f * edge;

    float sum = 0.0f;

    int tap = 0;

    for (int dy = -1; dy <= 1; ++dy)
    {
        for (int dx = -1; dx <= 1; ++dx)
        {
            const float sampleX =
                static_cast<float>(cx + dx);

            const float sampleY =
                static_cast<float>(cy + dy);

            const float distanceX =
                sampleX - srcX;

            const float distanceY =
                sampleY - srcY;

            const float spatial =
                gaussian(
                    sqrtf(
                        distanceX * distanceX +
                        distanceY * distanceY));

            const float sample =
                readLuma(
                    cx + dx,
                    cy + dy);

            const float difference =
                fabsf(sample - center) / 255.0f;

            const float edgeSimilarity =
                1.0f /
                (1.0f +
                 edgeStrength * difference);

            const float weight =
                spatial *
                edgeSimilarity;

            result.w[tap++] = weight;
            sum += weight;
        }
    }

    if (sum > 1.0e-8f)
    {
        for (int i = 0; i < 9; ++i)
            result.w[i] /= sum;
    }

    return result;
}

// ------------------------------------------------------------
// CPU reference.
// ------------------------------------------------------------

struct HostReader
{
    const Image& image;

    float operator()(
        int x,
        int y) const
    {
        x = adaptiveClampInt(
            x,
            0,
            image.width - 1);

        y = adaptiveClampInt(
            y,
            0,
            image.height - 1);

        const size_t index =
            (static_cast<size_t>(y) *
             static_cast<size_t>(image.width) +
             static_cast<size_t>(x)) * 4;

        return luma(
            static_cast<float>(image.pixels[index + 0]),
            static_cast<float>(image.pixels[index + 1]),
            static_cast<float>(image.pixels[index + 2]));
    }
};

Image upscaleAdaptiveReference(
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

    HostReader reader{input};

    for (int y = 0;
         y < outputHeight;
         ++y)
    {
        const float srcY =
            (static_cast<float>(y) + 0.5f) *
            static_cast<float>(input.height) /
            static_cast<float>(outputHeight) -
            0.5f;

        const float clampedSrcY =
            std::max(
                0.0f,
                std::min(
                    srcY,
                    static_cast<float>(input.height - 1)));

        for (int x = 0;
             x < outputWidth;
             ++x)
        {
            const float srcX =
                (static_cast<float>(x) + 0.5f) *
                static_cast<float>(input.width) /
                static_cast<float>(outputWidth) -
                0.5f;

            const float clampedSrcX =
                std::max(
                    0.0f,
                    std::min(
                        srcX,
                        static_cast<float>(input.width - 1)));

            const FilterWeights weights =
                makeWeights(
                    reader,
                    clampedSrcX,
                    clampedSrcY);

            const int cx =
                static_cast<int>(
                    floorf(clampedSrcX));

            const int cy =
                static_cast<int>(
                    floorf(clampedSrcY));

            for (int channel = 0;
                 channel < 3;
                 ++channel)
            {
                float value = 0.0f;

                int i = 0;

                for (int dy = -1; dy <= 1; ++dy)
                {
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        const int sx =
                            adaptiveClampInt(
                                cx + dx,
                                0,
                                input.width - 1);

                        const int sy =
                            adaptiveClampInt(
                                cy + dy,
                                0,
                                input.height - 1);

                        const size_t index =
                            (static_cast<size_t>(sy) *
                             static_cast<size_t>(input.width) +
                             static_cast<size_t>(sx)) * 4;

                        value +=
                            weights.w[i++] *
                            static_cast<float>(
                                input.pixels[index + channel]);
                    }
                }

                const size_t dst =
                    (static_cast<size_t>(y) *
                     static_cast<size_t>(outputWidth) +
                     static_cast<size_t>(x)) * 4 +
                    static_cast<size_t>(channel);

                output.pixels[dst] =
                    static_cast<uint8_t>(
                        std::max(
                            0.0f,
                            std::min(
                                255.0f,
                                value)) +
                        0.5f);
            }

            const size_t alpha =
                (static_cast<size_t>(y) *
                 static_cast<size_t>(outputWidth) +
                 static_cast<size_t>(x)) * 4 + 3;

            output.pixels[alpha] = 255;
        }
    }

    return output;
}

// ------------------------------------------------------------
// GPU helpers.
// ------------------------------------------------------------

__device__
float readLumaDevice(
    const uint8_t* input,
    int width,
    int height,
    int x,
    int y)
{
    x = adaptiveClampInt(
        x,
        0,
        width - 1);

    y = adaptiveClampInt(
        y,
        0,
        height - 1);

    const size_t index =
        (static_cast<size_t>(y) *
         static_cast<size_t>(width) +
         static_cast<size_t>(x)) * 4;

    return luma(
        static_cast<float>(input[index + 0]),
        static_cast<float>(input[index + 1]),
        static_cast<float>(input[index + 2]));
}

__device__
FilterWeights makeWeightsDevice(
    const uint8_t* input,
    int width,
    int height,
    float srcX,
    float srcY)
{
    return makeWeights(
        [&](int x, int y)
        {
            return readLumaDevice(
                input,
                width,
                height,
                x,
                y);
        },
        srcX,
        srcY);
}

__global__
void adaptiveTensorKernel(
    const uint8_t* input,
    int inputWidth,
    int inputHeight,
    uint8_t* output,
    int outputWidth,
    int outputHeight)
{
    // One warp owns 16 neighbouring output pixels.
    const int row =
        blockIdx.x;

    const int y =
        blockIdx.y;

    const int xStart =
        row * 16;

    if (y >= outputHeight ||
        xStart >= outputWidth)
        return;

    __shared__ half sWeights[16 * 16];
    __shared__ half sSamples[16 * 16];
    __shared__ float sResult[16 * 16];

    const int lane =
        threadIdx.x;

    // ------------------------------------------------------------
    // Boundary pixels use the scalar 3x3 filter.
    //
    // The WMMA path is used for interior pixels. This keeps the
    // boundary convention explicit and avoids relying on padded
    // matrix entries for clamped edge samples.
    // ------------------------------------------------------------

    if (lane < 16)
    {
        const int x =
            blockIdx.x * 16 + lane;

        if (x < outputWidth &&
            (y < outputHeight) &&
            (x == 0 ||
             y == 0 ||
             x == outputWidth - 1 ||
             y == outputHeight - 1))
        {
            const float srcX =
                (static_cast<float>(x) + 0.5f) *
                static_cast<float>(inputWidth) /
                static_cast<float>(outputWidth) -
                0.5f;

            const float srcY =
                (static_cast<float>(y) + 0.5f) *
                static_cast<float>(inputHeight) /
                static_cast<float>(outputHeight) -
                0.5f;

            const float clampedSrcX =
                fminf(
                    fmaxf(srcX, 0.0f),
                    static_cast<float>(inputWidth - 1));

            const float clampedSrcY =
                fminf(
                    fmaxf(srcY, 0.0f),
                    static_cast<float>(inputHeight - 1));

            const FilterWeights weights =
                makeWeightsDevice(
                    input,
                    inputWidth,
                    inputHeight,
                    clampedSrcX,
                    clampedSrcY);

            const int cx =
                static_cast<int>(
                    floorf(clampedSrcX));

            const int cy =
                static_cast<int>(
                    floorf(clampedSrcY));

            for (int channel = 0;
                 channel < 3;
                 ++channel)
            {
                float value = 0.0f;

                int tap = 0;

                for (int dy = -1;
                     dy <= 1;
                     ++dy)
                {
                    for (int dx = -1;
                         dx <= 1;
                         ++dx)
                    {
                        const int sx =
                            adaptiveClampInt(
                                cx + dx,
                                0,
                                inputWidth - 1);

                        const int sy =
                            adaptiveClampInt(
                                cy + dy,
                                0,
                                inputHeight - 1);

                        const size_t index =
                            (static_cast<size_t>(sy) *
                             static_cast<size_t>(inputWidth) +
                             static_cast<size_t>(sx)) * 4;

                        value +=
                            weights.w[tap++] *
                            static_cast<float>(
                                input[index + channel]);
                    }
                }

                const size_t dst =
                    (static_cast<size_t>(y) *
                     static_cast<size_t>(outputWidth) +
                     static_cast<size_t>(x)) * 4 +
                    static_cast<size_t>(channel);

                const float clamped =
                    fminf(
                        fmaxf(value, 0.0f),
                        255.0f);

                output[dst] =
                    static_cast<uint8_t>(
                        clamped + 0.5f);
            }

            const size_t alpha =
                (static_cast<size_t>(y) *
                 static_cast<size_t>(outputWidth) +
                 static_cast<size_t>(x)) * 4 + 3;

            output[alpha] = 255;
        }
    }

    // ------------------------------------------------------------
    // Build 16 independent 9-tap filters.
    //
    // Matrix A:
    //   row = output pixel
    //   col = tap
    //
    // Only columns 0..8 contain actual coefficients.
    // ------------------------------------------------------------

    for (int i = lane;
         i < 16 * 16;
         i += 32)
    {
        sWeights[i] =
            __float2half(0.0f);
    }

    __syncthreads();

    if (lane < 16)
    {
        const int x =
            xStart + lane;

        if (x < outputWidth)
        {
            const float srcX =
                (static_cast<float>(x) + 0.5f) *
                static_cast<float>(inputWidth) /
                static_cast<float>(outputWidth) -
                0.5f;

            const float srcY =
                (static_cast<float>(y) + 0.5f) *
                static_cast<float>(inputHeight) /
                static_cast<float>(outputHeight) -
                0.5f;

            const float clampedSrcX =
                fminf(
                    fmaxf(srcX, 0.0f),
                    static_cast<float>(inputWidth - 1));

            const float clampedSrcY =
                fminf(
                    fmaxf(srcY, 0.0f),
                    static_cast<float>(inputHeight - 1));

            const FilterWeights weights =
                makeWeightsDevice(
                    input,
                    inputWidth,
                    inputHeight,
                    clampedSrcX,
                    clampedSrcY);

            for (int i = 0; i < 9; ++i)
            {
                sWeights[
                    lane * 16 + i] =
                    __float2half(
                        weights.w[i]);
            }
        }
    }

    __syncthreads();

    // ------------------------------------------------------------
    // Matrix B:
    //
    // row 0..8 = one source tap
    // column = one output pixel
    //
    // Therefore C = A * B and C[i][i] is the desired
    // weighted sum for output pixel i.
    // ------------------------------------------------------------

    for (int i = lane;
         i < 16 * 16;
         i += 32)
    {
        sSamples[i] =
            __float2half(0.0f);
    }

    __syncthreads();

    if (lane < 16)
    {
        const int x =
            xStart + lane;

        if (x < outputWidth)
        {
            const float srcX =
                (static_cast<float>(x) + 0.5f) *
                static_cast<float>(inputWidth) /
                static_cast<float>(outputWidth) -
                0.5f;

            const float srcY =
                (static_cast<float>(y) + 0.5f) *
                static_cast<float>(inputHeight) /
                static_cast<float>(outputHeight) -
                0.5f;

            const float clampedSrcX =
                fminf(
                    fmaxf(srcX, 0.0f),
                    static_cast<float>(inputWidth - 1));

            const float clampedSrcY =
                fminf(
                    fmaxf(srcY, 0.0f),
                    static_cast<float>(inputHeight - 1));

            const int cx =
                static_cast<int>(
                    floorf(clampedSrcX));

            const int cy =
                static_cast<int>(
                    floorf(clampedSrcY));

            int tap = 0;

            for (int dy = -1; dy <= 1; ++dy)
            {
                for (int dx = -1; dx <= 1; ++dx)
                {
                    const int sx =
                        adaptiveClampInt(
                            cx + dx,
                            0,
                            inputWidth - 1);

                    const int sy =
                        adaptiveClampInt(
                            cy + dy,
                            0,
                            inputHeight - 1);

                    const size_t index =
                        (static_cast<size_t>(sy) *
                         static_cast<size_t>(inputWidth) +
                         static_cast<size_t>(sx)) * 4;

                    // We process one channel at a time.
                    // B gets filled below.
                    for (int c = 0; c < 3; ++c)
                    {
                        const int matrixIndex =
                            tap * 16 + lane;

                        // Only RGB is meaningful here.
                        // Store red initially; other channels
                        // are generated in separate passes.
                        if (c == 0)
                        {
                            sSamples[matrixIndex] =
                                __uint2half_rn(
                                    input[index + 0]);
                        }
                    }

                    ++tap;
                }
            }
        }
    }

    __syncthreads();

    // ------------------------------------------------------------
    // Process RGB independently.
    // ------------------------------------------------------------

    for (int channel = 0;
         channel < 3;
         ++channel)
    {
        if (lane < 16)
        {
            const int x =
                xStart + lane;

            if (x < outputWidth)
            {
                const float srcX =
                    (static_cast<float>(x) + 0.5f) *
                    static_cast<float>(inputWidth) /
                    static_cast<float>(outputWidth) -
                    0.5f;

                const float srcY =
                    (static_cast<float>(y) + 0.5f) *
                    static_cast<float>(inputHeight) /
                    static_cast<float>(outputHeight) -
                    0.5f;

                const int cx =
                    static_cast<int>(
                        floorf(srcX));

                const int cy =
                    static_cast<int>(
                        floorf(srcY));

                int tap = 0;

                for (int dy = -1; dy <= 1; ++dy)
                {
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        const int sx =
                            adaptiveClampInt(
                                cx + dx,
                                0,
                                inputWidth - 1);

                        const int sy =
                            adaptiveClampInt(
                                cy + dy,
                                0,
                                inputHeight - 1);

                        const size_t index =
                            (static_cast<size_t>(sy) *
                             static_cast<size_t>(inputWidth) +
                             static_cast<size_t>(sx)) * 4;

                        sSamples[
                            tap * 16 + lane] =
                            __uint2half_rn(
                                input[
                                    index +
                                    channel]);

                        ++tap;
                    }
                }
            }
        }

        __syncthreads();

        wmma::fragment<
            wmma::matrix_a,
            16, 16, 16,
            half,
            wmma::row_major
        > a;

        wmma::fragment<
            wmma::matrix_b,
            16, 16, 16,
            half,
            wmma::row_major
        > b;

        wmma::fragment<
            wmma::accumulator,
            16, 16, 16,
            float
        > c;

        wmma::fill_fragment(
            c,
            0.0f);

        wmma::load_matrix_sync(
            a,
            sWeights,
            16);

        wmma::load_matrix_sync(
            b,
            sSamples,
            16);

        wmma::mma_sync(
            c,
            a,
            b,
            c);

        wmma::store_matrix_sync(
            sResult,
            c,
            16,
            wmma::mem_row_major);

        __syncthreads();

        if (lane < 16)
        {
            const int x =
                xStart + lane;

            if (x < outputWidth &&
                x > 0 &&
                y > 0 &&
                x < outputWidth - 1 &&
                y < outputHeight - 1)
            {
                const float value =
                    sResult[
                        lane * 16 + lane];

                const size_t dst =
                    (static_cast<size_t>(y) *
                     static_cast<size_t>(outputWidth) +
                     static_cast<size_t>(x)) * 4 +
                    static_cast<size_t>(channel);

                const float clamped =
                    fminf(
                        fmaxf(
                            value,
                            0.0f),
                        255.0f);

                output[dst] =
                    static_cast<uint8_t>(
                        clamped + 0.5f);
            }
        }

        __syncthreads();
    }

    if (lane < 16)
    {
        const int x =
            xStart + lane;

        if (x < outputWidth)
        {
            const size_t dst =
                (static_cast<size_t>(y) *
                 static_cast<size_t>(outputWidth) +
                 static_cast<size_t>(x)) * 4 + 3;

            output[dst] = 255;
        }
    }
}

Image upscaleAdaptiveTensor(
    const Image& input,
    int outputWidth,
    int outputHeight,
    float* kernelMs)
{
    Image output;

    output.width = outputWidth;
    output.height = outputHeight;

    output.pixels.resize(
        static_cast<size_t>(outputWidth) *
        static_cast<size_t>(outputHeight) *
        4);

    uint8_t* dInput = nullptr;
    uint8_t* dOutput = nullptr;

    CUDA_CHECK(
        cudaMalloc(
            &dInput,
            input.pixels.size()));

    CUDA_CHECK(
        cudaMalloc(
            &dOutput,
            output.pixels.size()));

    CUDA_CHECK(
        cudaMemcpy(
            dInput,
            input.pixels.data(),
            input.pixels.size(),
            cudaMemcpyHostToDevice));

    dim3 blocks(
        (outputWidth + 15) / 16,
        outputHeight);

    dim3 threads(32);

    for (int i = 0;
         i < 5;
         ++i)
    {
        adaptiveTensorKernel<<<
            blocks,
            threads
        >>>(
            dInput,
            input.width,
            input.height,
            dOutput,
            outputWidth,
            outputHeight);
    }

    CUDA_CHECK(
        cudaGetLastError());

    CUDA_CHECK(
        cudaDeviceSynchronize());

    cudaEvent_t start;
    cudaEvent_t stop;

    CUDA_CHECK(
        cudaEventCreate(&start));

    CUDA_CHECK(
        cudaEventCreate(&stop));

    constexpr int iterations = 50;

    CUDA_CHECK(
        cudaEventRecord(start));

    for (int i = 0;
         i < iterations;
         ++i)
    {
        adaptiveTensorKernel<<<
            blocks,
            threads
        >>>(
            dInput,
            input.width,
            input.height,
            dOutput,
            outputWidth,
            outputHeight);
    }

    CUDA_CHECK(
        cudaEventRecord(stop));

    CUDA_CHECK(
        cudaEventSynchronize(stop));

    float elapsed = 0.0f;

    CUDA_CHECK(
        cudaEventElapsedTime(
            &elapsed,
            start,
            stop));

    *kernelMs =
        elapsed /
        static_cast<float>(
            iterations);

    CUDA_CHECK(
        cudaMemcpy(
            output.pixels.data(),
            dOutput,
            output.pixels.size(),
            cudaMemcpyDeviceToHost));

    CUDA_CHECK(
        cudaEventDestroy(start));

    CUDA_CHECK(
        cudaEventDestroy(stop));

    CUDA_CHECK(
        cudaFree(dInput));

    CUDA_CHECK(
        cudaFree(dOutput));

    return output;
}





