#include "tensor_scaler.h"

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
            std::cerr << "CUDA error: "                                        \
                      << cudaGetErrorString(err)                           \
                      << " at " << __FILE__                                \
                      << ":" << __LINE__ << '\n';                           \
            std::exit(EXIT_FAILURE);                                        \
        }                                                                   \
    } while (0)

constexpr int TILE_SRC = 10;
constexpr int THREADS = 32;

__device__
uint8_t tensorFloatToByte(float value)
{
    value = fminf(fmaxf(value, 0.0f), 255.0f);
    return static_cast<uint8_t>(value + 0.5f);
}

__device__
int clampInt(
    int value,
    int low,
    int high)
{
    return max(low, min(high, value));
}

__global__
void tensorBilinear2xKernel(
    const uint8_t* input,
    int inputWidth,
    int inputHeight,
    uint8_t* output,
    int outputWidth,
    int outputHeight)
{
    const int tileX = blockIdx.x;
    const int tileY = blockIdx.y;

    const int baseX = tileX * 16;
    const int baseY = tileY * 16;

    // Source patch origins are chosen so every source sample needed
    // by the 16x16 output tile is actually inside the 10x10 patch.
    const int patchX0 =
        clampInt(
            baseX / 2 - 1,
            0,
            inputWidth - TILE_SRC);

    const int patchY0 =
        clampInt(
            baseY / 2 - 1,
            0,
            inputHeight - TILE_SRC);

    __shared__ half sVertical[16 * 16];
    __shared__ half sHorizontalT[16 * 16];
    __shared__ half sInput[16 * 16];

    __shared__ float sIntermediate[16 * 16];
    __shared__ half sIntermediateHalf[16 * 16];
    __shared__ float sOutput[16 * 16];

    const int tid = threadIdx.x;

    // ------------------------------------------------------------
    // Build vertical interpolation matrix.
    //
    // Each output row corresponds to one output pixel row.
    // The two source samples are mapped into the actual patch.
    // ------------------------------------------------------------

    for (int i = tid; i < 16 * 16; i += THREADS)
    {
        const int row = i / 16;
        const int col = i % 16;

        float weight = 0.0f;

        if (col < TILE_SRC)
        {
            const int outputY =
                baseY + row;

            const float srcY =
                (static_cast<float>(outputY) + 0.5f) * 0.5f
                - 0.5f;

            const int yFloor =
                static_cast<int>(floorf(srcY));

            const float fy =
                srcY - static_cast<float>(yFloor);

            const int y0 =
                clampInt(
                    yFloor,
                    0,
                    inputHeight - 1);

            const int y1 =
                clampInt(
                    yFloor + 1,
                    0,
                    inputHeight - 1);

            const int patchY =
                patchY0 + col;

            if (patchY == y0)
                weight += 1.0f - fy;

            if (patchY == y1)
                weight += fy;
        }

        sVertical[i] =
            __float2half(weight);
    }

    // ------------------------------------------------------------
    // Build horizontal interpolation matrix.
    //
    // This is the transpose of the horizontal weight matrix.
    // ------------------------------------------------------------

    for (int i = tid; i < 16 * 16; i += THREADS)
    {
        const int row = i / 16;
        const int col = i % 16;

        float weight = 0.0f;

        if (row < TILE_SRC)
        {
            const int outputX =
                baseX + col;

            const float srcX =
                (static_cast<float>(outputX) + 0.5f) * 0.5f
                - 0.5f;

            const int xFloor =
                static_cast<int>(floorf(srcX));

            const float fx =
                srcX - static_cast<float>(xFloor);

            const int x0 =
                clampInt(
                    xFloor,
                    0,
                    inputWidth - 1);

            const int x1 =
                clampInt(
                    xFloor + 1,
                    0,
                    inputWidth - 1);

            const int patchX =
                patchX0 + row;

            if (patchX == x0)
                weight += 1.0f - fx;

            if (patchX == x1)
                weight += fx;
        }

        sHorizontalT[i] =
            __float2half(weight);
    }

    __syncthreads();

    // ------------------------------------------------------------
    // Process RGB channels independently.
    // ------------------------------------------------------------

    for (int channel = 0; channel < 3; ++channel)
    {
        for (int i = tid; i < 16 * 16; i += THREADS)
        {
            const int row = i / 16;
            const int col = i % 16;

            sInput[i] =
                __float2half(0.0f);

            if (row < TILE_SRC &&
                col < TILE_SRC)
            {
                const int sx =
                    patchX0 + col;

                const int sy =
                    patchY0 + row;

                const size_t index =
                    (static_cast<size_t>(sy) *
                     static_cast<size_t>(inputWidth) +
                     static_cast<size_t>(sx)) * 4 +
                    static_cast<size_t>(channel);

                sInput[i] =
                    __uint2half_rn(input[index]);
            }
        }

        __syncthreads();

        // --------------------------------------------------------
        // Tensor Core pass 1:
        //
        // vertical weights × source patch
        // --------------------------------------------------------

        wmma::fragment<
            wmma::matrix_a,
            16,
            16,
            16,
            half,
            wmma::row_major
        > aFrag;

        wmma::fragment<
            wmma::matrix_b,
            16,
            16,
            16,
            half,
            wmma::row_major
        > bFrag;

        wmma::fragment<
            wmma::accumulator,
            16,
            16,
            16,
            float
        > cFrag;

        wmma::fill_fragment(
            cFrag,
            0.0f);

        wmma::load_matrix_sync(
            aFrag,
            sVertical,
            16);

        wmma::load_matrix_sync(
            bFrag,
            sInput,
            16);

        wmma::mma_sync(
            cFrag,
            aFrag,
            bFrag,
            cFrag);

        wmma::store_matrix_sync(
            sIntermediate,
            cFrag,
            16,
            wmma::mem_row_major);

        __syncthreads();

        for (int i = tid; i < 16 * 16; i += THREADS)
        {
            sIntermediateHalf[i] =
                __float2half(sIntermediate[i]);
        }

        __syncthreads();

        // --------------------------------------------------------
        // Tensor Core pass 2:
        //
        // intermediate × horizontal weights
        // --------------------------------------------------------

        wmma::fragment<
            wmma::matrix_a,
            16,
            16,
            16,
            half,
            wmma::row_major
        > aFrag2;

        wmma::fragment<
            wmma::matrix_b,
            16,
            16,
            16,
            half,
            wmma::row_major
        > bFrag2;

        wmma::fragment<
            wmma::accumulator,
            16,
            16,
            16,
            float
        > cFrag2;

        wmma::fill_fragment(
            cFrag2,
            0.0f);

        wmma::load_matrix_sync(
            aFrag2,
            sIntermediateHalf,
            16);

        wmma::load_matrix_sync(
            bFrag2,
            sHorizontalT,
            16);

        wmma::mma_sync(
            cFrag2,
            aFrag2,
            bFrag2,
            cFrag2);

        wmma::store_matrix_sync(
            sOutput,
            cFrag2,
            16,
            wmma::mem_row_major);

        __syncthreads();

        for (int i = tid; i < 16 * 16; i += THREADS)
        {
            const int localX = i % 16;
            const int localY = i / 16;

            const int x =
                baseX + localX;

            const int y =
                baseY + localY;

            if (x < outputWidth &&
                y < outputHeight)
            {
                const size_t dst =
                    (static_cast<size_t>(y) *
                     static_cast<size_t>(outputWidth) +
                     static_cast<size_t>(x)) * 4 +
                    static_cast<size_t>(channel);

                output[dst] =
                    tensorFloatToByte(sOutput[i]);
            }
        }

        __syncthreads();
    }

    // Alpha channel.
    for (int i = tid; i < 16 * 16; i += THREADS)
    {
        const int localX = i % 16;
        const int localY = i / 16;

        const int x =
            baseX + localX;

        const int y =
            baseY + localY;

        if (x < outputWidth &&
            y < outputHeight)
        {
            const size_t dst =
                (static_cast<size_t>(y) *
                 static_cast<size_t>(outputWidth) +
                 static_cast<size_t>(x)) * 4 +
                3;

            output[dst] = 255;
        }
    }
}

Image upscaleTensorBilinear2x(
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

    const size_t inputBytes =
        input.pixels.size();

    const size_t outputBytes =
        output.pixels.size();

    uint8_t* dInput = nullptr;
    uint8_t* dOutput = nullptr;

    CUDA_CHECK(cudaMalloc(
        &dInput,
        inputBytes));

    CUDA_CHECK(cudaMalloc(
        &dOutput,
        outputBytes));

    CUDA_CHECK(cudaMemcpy(
        dInput,
        input.pixels.data(),
        inputBytes,
        cudaMemcpyHostToDevice));

    dim3 blocks(
        (outputWidth + 15) / 16,
        (outputHeight + 15) / 16);

    dim3 threads(THREADS);

    for (int i = 0; i < 5; ++i)
    {
        tensorBilinear2xKernel<<<
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

    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());

    cudaEvent_t start;
    cudaEvent_t stop;

    CUDA_CHECK(cudaEventCreate(&start));
    CUDA_CHECK(cudaEventCreate(&stop));

    constexpr int iterations = 50;

    CUDA_CHECK(cudaEventRecord(start));

    for (int i = 0; i < iterations; ++i)
    {
        tensorBilinear2xKernel<<<
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

    CUDA_CHECK(cudaEventRecord(stop));
    CUDA_CHECK(cudaEventSynchronize(stop));

    float elapsed = 0.0f;

    CUDA_CHECK(cudaEventElapsedTime(
        &elapsed,
        start,
        stop));

    *kernelMs =
        elapsed /
        static_cast<float>(iterations);

    CUDA_CHECK(cudaMemcpy(
        output.pixels.data(),
        dOutput,
        outputBytes,
        cudaMemcpyDeviceToHost));

    CUDA_CHECK(cudaEventDestroy(start));
    CUDA_CHECK(cudaEventDestroy(stop));

    CUDA_CHECK(cudaFree(dInput));
    CUDA_CHECK(cudaFree(dOutput));

    return output;
}
