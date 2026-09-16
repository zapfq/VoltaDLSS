#include <cuda_runtime.h>
#include <cuda_fp16.h>
#include <mma.h>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace nvcuda;

#define CUDA_CHECK(call)                                                   \
    do                                                                     \
    {                                                                      \
        cudaError_t err = (call);                                          \
        if (err != cudaSuccess)                                            \
        {                                                                   \
            std::cerr << "CUDA error: " << cudaGetErrorString(err)         \
                      << " at " << __FILE__ << ":" << __LINE__ << '\n';    \
            std::exit(EXIT_FAILURE);                                       \
        }                                                                   \
    } while (0)

constexpr int M = 4096;
constexpr int N = 4096;
constexpr int K = 4096;

constexpr int WMMA_M = 16;
constexpr int WMMA_N = 16;
constexpr int WMMA_K = 16;

constexpr int THREADS_PER_BLOCK = 128;

__global__
void wmmaGemmKernel(
    const half* A,
    const half* B,
    float* C)
{
    int warpId =
        (blockIdx.x * blockDim.x + threadIdx.x) / 32;

    int warpsPerRow = N / WMMA_N;

    int tileRow = warpId / warpsPerRow;
    int tileCol = warpId % warpsPerRow;

    if (tileRow >= M / WMMA_M)
        return;

    if (tileCol >= N / WMMA_N)
        return;

    wmma::fragment<
        wmma::matrix_a,
        WMMA_M,
        WMMA_N,
        WMMA_K,
        half,
        wmma::row_major
    > a_frag;

    wmma::fragment<
        wmma::matrix_b,
        WMMA_M,
        WMMA_N,
        WMMA_K,
        half,
        wmma::row_major
    > b_frag;

    wmma::fragment<
        wmma::accumulator,
        WMMA_M,
        WMMA_N,
        WMMA_K,
        float
    > c_frag;

    wmma::fill_fragment(c_frag, 0.0f);

    int aRow = tileRow * WMMA_M;
    int bCol = tileCol * WMMA_N;

    for (int k = 0; k < K; k += WMMA_K)
    {
        const half* tileA =
            A + aRow * K + k;

        const half* tileB =
            B + k * N + bCol;

        wmma::load_matrix_sync(
            a_frag,
            tileA,
            K
        );

        wmma::load_matrix_sync(
            b_frag,
            tileB,
            N
        );

        wmma::mma_sync(
            c_frag,
            a_frag,
            b_frag,
            c_frag
        );
    }

    float* tileC =
        C + aRow * N + bCol;

    wmma::store_matrix_sync(
        tileC,
        c_frag,
        N,
        wmma::mem_row_major
    );
}

void initializeMatrix(
    std::vector<half>& matrix,
    float value)
{
    half halfValue = __float2half(value);

    for (auto& element : matrix)
        element = halfValue;
}

bool validateResult(
    const std::vector<float>& C,
    float expected)
{
    constexpr int samples = 10;

    float maxError = 0.0f;

    for (int i = 0; i < samples; ++i)
    {
        int index =
            (i * (M * N / samples));

        float error =
            std::fabs(C[index] - expected);

        maxError =
            std::max(maxError, error);
    }

    std::cout << "Expected C value: "
              << expected << '\n';

    std::cout << "Maximum sampled error: "
              << maxError << '\n';

    return maxError < 0.01f;
}

float runTensorBenchmark()
{
    const size_t elementsA =
        static_cast<size_t>(M) * K;

    const size_t elementsB =
        static_cast<size_t>(K) * N;

    const size_t elementsC =
        static_cast<size_t>(M) * N;

    const size_t bytesA =
        elementsA * sizeof(half);

    const size_t bytesB =
        elementsB * sizeof(half);

    const size_t bytesC =
        elementsC * sizeof(float);

    std::cout << "Matrix A: "
              << M << " x " << K << '\n';

    std::cout << "Matrix B: "
              << K << " x " << N << '\n';

    std::cout << "Matrix C: "
              << M << " x " << N << '\n';

    std::cout << "Precision: FP16\n";
    std::cout << "Accumulation: FP32\n";
    std::cout << "Operation: WMMA 16x16x16\n\n";

    // ------------------------------------------------------------
    // Host matrices
    // ------------------------------------------------------------

    std::vector<half> h_A(elementsA);
    std::vector<half> h_B(elementsB);
    std::vector<float> h_C(elementsC);

    // A = 1.0
    // B = 1.0
    //
    // Therefore:
    //
    // C = A x B
    //
    // Every C element should equal K = 4096.
    initializeMatrix(h_A, 1.0f);
    initializeMatrix(h_B, 1.0f);

    // ------------------------------------------------------------
    // Device memory
    // ------------------------------------------------------------

    half* d_A = nullptr;
    half* d_B = nullptr;
    float* d_C = nullptr;

    CUDA_CHECK(cudaMalloc(&d_A, bytesA));
    CUDA_CHECK(cudaMalloc(&d_B, bytesB));
    CUDA_CHECK(cudaMalloc(&d_C, bytesC));

    CUDA_CHECK(cudaMemcpy(
        d_A,
        h_A.data(),
        bytesA,
        cudaMemcpyHostToDevice));

    CUDA_CHECK(cudaMemcpy(
        d_B,
        h_B.data(),
        bytesB,
        cudaMemcpyHostToDevice));

    CUDA_CHECK(cudaMemset(
        d_C,
        0,
        bytesC));

    int totalWarps =
        (M / WMMA_M) *
        (N / WMMA_N);

    int totalThreads =
        totalWarps * 32;

    int blocks =
        (totalThreads +
         THREADS_PER_BLOCK - 1)
        / THREADS_PER_BLOCK;

    // ------------------------------------------------------------
    // Correctness test
    // ------------------------------------------------------------

    std::cout << "Running correctness test...\n";

    wmmaGemmKernel<<<
        blocks,
        THREADS_PER_BLOCK
    >>>(
        d_A,
        d_B,
        d_C
    );

    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());

    CUDA_CHECK(cudaMemcpy(
        h_C.data(),
        d_C,
        bytesC,
        cudaMemcpyDeviceToHost));

    bool correct =
        validateResult(
            h_C,
            static_cast<float>(K));

    if (!correct)
    {
        std::cerr << "\nERROR: Tensor Core result failed validation.\n";

        CUDA_CHECK(cudaFree(d_A));
        CUDA_CHECK(cudaFree(d_B));
        CUDA_CHECK(cudaFree(d_C));

        return 0.0f;
    }

    std::cout << "Correctness test: PASSED\n\n";

    // ------------------------------------------------------------
    // Warmup
    // ------------------------------------------------------------

    std::cout << "Warmup...\n";

    for (int i = 0; i < 10; ++i)
    {
        wmmaGemmKernel<<<
            blocks,
            THREADS_PER_BLOCK
        >>>(
            d_A,
            d_B,
            d_C
        );
    }

    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());

    // ------------------------------------------------------------
    // Benchmark
    // ------------------------------------------------------------

    constexpr int iterations = 50;

    cudaEvent_t start;
    cudaEvent_t stop;

    CUDA_CHECK(cudaEventCreate(&start));
    CUDA_CHECK(cudaEventCreate(&stop));

    std::cout << "Benchmarking "
              << iterations
              << " iterations...\n";

    CUDA_CHECK(cudaEventRecord(start));

    for (int i = 0; i < iterations; ++i)
    {
        wmmaGemmKernel<<<
            blocks,
            THREADS_PER_BLOCK
        >>>(
            d_A,
            d_B,
            d_C
        );
    }

    CUDA_CHECK(cudaEventRecord(stop));
    CUDA_CHECK(cudaEventSynchronize(stop));

    float milliseconds = 0.0f;

    CUDA_CHECK(cudaEventElapsedTime(
        &milliseconds,
        start,
        stop));

    float averageMs =
        milliseconds / iterations;

    double operations =
        2.0 *
        static_cast<double>(M) *
        static_cast<double>(N) *
        static_cast<double>(K);

    double seconds =
        averageMs / 1000.0;

    double tflops =
        operations /
        seconds /
        1.0e12;

    std::cout << "\nAverage GPU time: "
              << averageMs
              << " ms\n";

    std::cout << "Achieved: "
              << tflops
              << " TFLOPS\n";

    // ------------------------------------------------------------
    // Cleanup
    // ------------------------------------------------------------

    CUDA_CHECK(cudaEventDestroy(start));
    CUDA_CHECK(cudaEventDestroy(stop));

    CUDA_CHECK(cudaFree(d_A));
    CUDA_CHECK(cudaFree(d_B));
    CUDA_CHECK(cudaFree(d_C));

    return static_cast<float>(tflops);
}