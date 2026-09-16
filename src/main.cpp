#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "scaler.h"
#include "tensor_scaler.h"
#include "adaptive_scaler.h"
#include "easu_reference.h"
#include "easu_tensor.h"
#include "quality.h"

#include <cuda_runtime.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#define CUDA_CHECK(call)                                                   \
    do                                                                     \
    {                                                                      \
        cudaError_t err = (call);                                          \
        if (err != cudaSuccess)                                            \
        {                                                                   \
            std::cerr << "CUDA error: "                                      \
                      << cudaGetErrorString(err)                            \
                      << " at " << __FILE__                                 \
                      << ":" << __LINE__ << '\n';                            \
            std::exit(EXIT_FAILURE);                                        \
        }                                                                   \
    } while (0)

float runTensorBenchmark();

bool savePPM(
    const Image& image,
    const char* filename)
{
    std::ofstream file(
        filename,
        std::ios::binary);

    if (!file)
        return false;

    file << "P6\n"
         << image.width
         << ' '
         << image.height
         << "\n255\n";

    for (size_t i = 0;
         i < image.pixels.size();
         i += 4)
    {
        file.put(
            static_cast<char>(
                image.pixels[i + 0]));

        file.put(
            static_cast<char>(
                image.pixels[i + 1]));

        file.put(
            static_cast<char>(
                image.pixels[i + 2]));
    }

    return true;
}

struct Metrics
{
    double mae = 0.0;
    double mse = 0.0;
    double psnr = 0.0;
    int maxError = 0;
};

Metrics compareAgainstGroundTruth(
    const Image& result,
    const Image& groundTruth)
{
    Metrics metrics;

    if (result.width != groundTruth.width ||
        result.height != groundTruth.height ||
        result.pixels.size() != groundTruth.pixels.size())
    {
        metrics.maxError = 255;
        return metrics;
    }

    double totalAbs = 0.0;
    double totalSquared = 0.0;
    size_t samples = 0;

    for (size_t i = 0;
         i < result.pixels.size();
         i += 4)
    {
        for (int c = 0; c < 3; ++c)
        {
            const int a =
                static_cast<int>(
                    result.pixels[i + c]);

            const int b =
                static_cast<int>(
                    groundTruth.pixels[i + c]);

            const int error =
                std::abs(a - b);

            metrics.maxError =
                std::max(
                    metrics.maxError,
                    error);

            totalAbs +=
                static_cast<double>(error);

            totalSquared +=
                static_cast<double>(error) *
                static_cast<double>(error);

            ++samples;
        }
    }

    metrics.mae =
        totalAbs /
        static_cast<double>(samples);

    metrics.mse =
        totalSquared /
        static_cast<double>(samples);

    if (metrics.mse > 0.0)
    {
        metrics.psnr =
            10.0 *
            std::log10(
                (255.0 * 255.0) /
                metrics.mse);
    }
    else
    {
        metrics.psnr = 999.0;
    }

    return metrics;
}

const char* patternName(
    TestPattern pattern)
{
    switch (pattern)
    {
    case TestPattern::Gradient:
        return "gradient";

    case TestPattern::Checkerboard:
        return "checkerboard";

    case TestPattern::VerticalEdge:
        return "vertical_edge";

    case TestPattern::HorizontalEdge:
        return "horizontal_edge";

    case TestPattern::DiagonalEdge:
        return "diagonal_edge";

    case TestPattern::Noise:
        return "noise";
    }

    return "unknown";
}

Image createThinLineImage(
    int width,
    int height)
{
    Image image;

    image.width = width;
    image.height = height;

    image.pixels.resize(
        static_cast<size_t>(width) *
        static_cast<size_t>(height) *
        4);

    for (int y = 0;
         y < height;
         ++y)
    {
        for (int x = 0;
             x < width;
             ++x)
        {
            const bool vertical =
                (x % 16 == 0);

            const bool horizontal =
                (y % 16 == 0);

            const bool diagonal =
                ((x - y) % 32 == 0);

            const bool line =
                vertical ||
                horizontal ||
                diagonal;

            const uint8_t value =
                line ? 255 : 16;

            const size_t index =
                (static_cast<size_t>(y) *
                 static_cast<size_t>(width) +
                 static_cast<size_t>(x)) * 4;

            image.pixels[index + 0] = value;
            image.pixels[index + 1] = value;
            image.pixels[index + 2] = value;
            image.pixels[index + 3] = 255;
        }
    }

    return image;
}

void printMetrics(
    const char* name,
    const Metrics& metrics)
{
    std::cout
        << name
        << ":\n";

    std::cout
        << "  MAE:   "
        << metrics.mae
        << '\n';

    std::cout
        << "  PSNR:  "
        << metrics.psnr
        << " dB\n";

    std::cout
        << "  Max:   "
        << metrics.maxError
        << '\n';
}

int main(
    int argc,
    char* argv[])
{
    int deviceCount = 0;

    CUDA_CHECK(
        cudaGetDeviceCount(
            &deviceCount));

    if (deviceCount == 0)
    {
        std::cerr
            << "No CUDA GPU found.\n";

        return EXIT_FAILURE;
    }

    CUDA_CHECK(
        cudaSetDevice(0));

    QualityPreset currentPreset =
        QualityPreset::Quality;

    for (int i = 1;
         i < argc;
         ++i)
    {
        const std::string argument =
            argv[i];

        if (argument == "--quality" &&
            i + 1 < argc)
        {
            QualityPreset requestedPreset;

            if (!parseQualityPreset(
                    argv[i + 1],
                    requestedPreset))
            {
                std::cerr
                    << "Unknown quality preset: "
                    << argv[i + 1]
                    << '\n'
                    << "Valid presets: "
                    << "ultra-performance, "
                    << "performance, "
                    << "balanced, "
                    << "quality, "
                    << "native\n";

                return EXIT_FAILURE;
            }

            currentPreset =
                requestedPreset;

            ++i;
        }
    }

    cudaDeviceProp prop{};

    CUDA_CHECK(
        cudaGetDeviceProperties(
            &prop,
            0));

    std::cout
        << "=====================================\n"
        << "          VoltaDLSS v0.6\n"
        << "=====================================\n\n";

    std::cout
        << "GPU: "
        << prop.name
        << '\n';

    std::cout
        << "Compute Capability: "
        << prop.major
        << '.'
        << prop.minor
        << '\n';

    const QualitySettings currentSettings =
        getQualitySettings(
            currentPreset);

    std::cout
        << "Quality preset: "
        << currentSettings.name
        << '\n';

    std::cout
        << '\n';

    std::cout
        << "--- Tensor Core benchmark ---\n\n";

    runTensorBenchmark();

    constexpr int outputWidth = 1920;
    constexpr int outputHeight = 1080;

    constexpr int testInputWidth = 1280;
    constexpr int testInputHeight = 720;

    const TestPattern patterns[] =
    {
        TestPattern::Gradient,
        TestPattern::Checkerboard,
        TestPattern::VerticalEdge,
        TestPattern::HorizontalEdge,
        TestPattern::DiagonalEdge,
        TestPattern::Noise
    };

    constexpr int patternCount =
        static_cast<int>(
            sizeof(patterns) /
            sizeof(patterns[0]));

    double bilinearPSNR = 0.0;
    double adaptivePSNR = 0.0;

    // ------------------------------------------------------------
    // Existing reconstruction validation
    // ------------------------------------------------------------

    std::cout
        << "\n=====================================\n"
        << "Ground-truth reconstruction test\n"
        << "=====================================\n";

    std::cout
        << "\nGround truth: "
        << outputWidth
        << " x "
        << outputHeight
        << '\n';

    std::cout
        << "Input:        "
        << testInputWidth
        << " x "
        << testInputHeight
        << '\n';

    std::cout
        << "Scale:        1.5x\n";

    for (int i = 0;
         i < patternCount;
         ++i)
    {
        const TestPattern pattern =
            patterns[i];

        const char* name =
            patternName(pattern);

        std::cout
            << "\n-------------------------------------\n"
            << name
            << '\n'
            << "-------------------------------------\n";

        Image groundTruth =
            createTestImage(
                outputWidth,
                outputHeight,
                pattern);

        Image input =
            upscaleBilinear(
                groundTruth,
                testInputWidth,
                testInputHeight);

        Image bilinear =
            upscaleBilinear(
                input,
                outputWidth,
                outputHeight);

        float adaptiveMs = 0.0f;

        Image adaptive =
            upscaleAdaptiveTensor(
                input,
                outputWidth,
                outputHeight,
                &adaptiveMs);

        Metrics bilinearMetrics =
            compareAgainstGroundTruth(
                bilinear,
                groundTruth);

        Metrics adaptiveMetrics =
            compareAgainstGroundTruth(
                adaptive,
                groundTruth);

        std::cout
            << "Adaptive kernel: "
            << adaptiveMs
            << " ms\n\n";

        printMetrics(
            "Bilinear",
            bilinearMetrics);

        printMetrics(
            "VoltaDLSS",
            adaptiveMetrics);

        const double improvement =
            bilinearMetrics.mae > 0.0
                ? (1.0 -
                   adaptiveMetrics.mae /
                   bilinearMetrics.mae) *
                  100.0
                : 0.0;

        std::cout
            << "MAE improvement: "
            << improvement
            << "%\n";

        bilinearPSNR +=
            bilinearMetrics.psnr;

        adaptivePSNR +=
            adaptiveMetrics.psnr;

        const std::string base =
            "volta_v06_" +
            std::string(name);

        savePPM(
            groundTruth,
            (base + "_groundtruth.ppm").c_str());

        savePPM(
            bilinear,
            (base + "_bilinear.ppm").c_str());

        savePPM(
            adaptive,
            (base + "_volta.ppm").c_str());
    }

    // Thin line test.
    std::cout
        << "\n-------------------------------------\n"
        << "thin_lines\n"
        << "-------------------------------------\n";

    Image lineGroundTruth =
        createThinLineImage(
            outputWidth,
            outputHeight);

    Image lineInput =
        upscaleBilinear(
            lineGroundTruth,
            testInputWidth,
            testInputHeight);

    Image lineBilinear =
        upscaleBilinear(
            lineGroundTruth,
            testInputWidth,
            testInputHeight);

    lineBilinear =
        upscaleBilinear(
            lineBilinear,
            outputWidth,
            outputHeight);

    float lineKernelMs = 0.0f;

    Image lineAdaptive =
        upscaleAdaptiveTensor(
            lineInput,
            outputWidth,
            outputHeight,
            &lineKernelMs);

    Metrics lineBilinearMetrics =
        compareAgainstGroundTruth(
            lineBilinear,
            lineGroundTruth);

    Metrics lineAdaptiveMetrics =
        compareAgainstGroundTruth(
            lineAdaptive,
            lineGroundTruth);

    std::cout
        << "Adaptive kernel: "
        << lineKernelMs
        << " ms\n\n";

    printMetrics(
        "Bilinear",
        lineBilinearMetrics);

    printMetrics(
        "VoltaDLSS",
        lineAdaptiveMetrics);

    const double lineImprovement =
        lineBilinearMetrics.mae > 0.0
            ? (1.0 -
               lineAdaptiveMetrics.mae /
               lineBilinearMetrics.mae) *
              100.0
            : 0.0;

    std::cout
        << "MAE improvement: "
        << lineImprovement
        << "%\n";

    savePPM(
        lineGroundTruth,
        "volta_v06_thin_lines_groundtruth.ppm");

    savePPM(
        lineBilinear,
        "volta_v06_thin_lines_bilinear.ppm");

    savePPM(
        lineAdaptive,
        "volta_v06_thin_lines_volta.ppm");

    bilinearPSNR /=
        static_cast<double>(
            patternCount);

    adaptivePSNR /=
        static_cast<double>(
            patternCount);

    std::cout
        << "\n=====================================\n"
        << "v0.6 quality validation\n"
        << "=====================================\n";

    std::cout
        << "Average pattern PSNR:\n";

    std::cout
        << "  Bilinear: "
        << bilinearPSNR
        << " dB\n";

    std::cout
        << "  VoltaDLSS: "
        << adaptivePSNR
        << " dB\n";

    std::cout
        << "\nThe higher PSNR / lower MAE result is "
        << "the better reconstruction.\n";

    std::cout
        << "=====================================\n";

    // ------------------------------------------------------------
    // EASU CUDA reference test
    // ------------------------------------------------------------

    std::cout
        << "\n=====================================\n"
        << "EASU CUDA reference test\n"
        << "=====================================\n";

    Image easuInput =
        upscaleBilinear(
            lineGroundTruth,
            testInputWidth,
            testInputHeight);

    float easuMs = 0.0f;

    Image easuOutput =
        upscaleEasuCuda(
            easuInput,
            outputWidth,
            outputHeight,
            &easuMs);

    Image easuReference =
        upscaleEasuReference(
            easuInput,
            outputWidth,
            outputHeight);

    std::cout
        << "Input:  "
        << testInputWidth
        << " x "
        << testInputHeight
        << '\n';

    std::cout
        << "Output: "
        << outputWidth
        << " x "
        << outputHeight
        << '\n';

    std::cout
        << "CUDA EASU kernel: "
        << easuMs
        << " ms\n";

    savePPM(
        easuReference,
        "volta_easu_reference.ppm");

    savePPM(
        easuOutput,
        "volta_easu_cuda.ppm");

    std::cout
        << "\n--- CUDA EASU vs CPU EASU ---\n";

    int maxError = 0;
    double totalError = 0.0;
    size_t sampleCount = 0;

    int worstX = 0;
    int worstY = 0;
    int worstChannel = 0;
    int worstReference = 0;
    int worstCuda = 0;

    for (size_t i = 0;
         i < easuReference.pixels.size();
         i += 4)
    {
        const int pixel =
            static_cast<int>(
                i / 4);

        const int x =
            pixel % easuReference.width;

        const int y =
            pixel / easuReference.width;

        for (int c = 0;
             c < 3;
             ++c)
        {
            const int reference =
                static_cast<int>(
                    easuReference.pixels[i + c]);

            const int cudaValue =
                static_cast<int>(
                    easuOutput.pixels[i + c]);

            const int error =
                std::abs(
                    reference -
                    cudaValue);

            if (error > maxError)
            {
                maxError = error;
                worstX = x;
                worstY = y;
                worstChannel = c;
                worstReference = reference;
                worstCuda = cudaValue;
            }

            totalError +=
                static_cast<double>(error);

            ++sampleCount;
        }
    }

    std::cout
        << "Maximum RGB error: "
        << maxError
        << '\n';

    std::cout
        << "Average RGB error: "
        << totalError /
           static_cast<double>(
               sampleCount)
        << '\n';

    if (maxError > 0)
    {
        std::cout
            << "Worst pixel: ("
            << worstX
            << ", "
            << worstY
            << ") channel "
            << worstChannel
            << " reference="
            << worstReference
            << " cuda="
            << worstCuda
            << '\n';
    }

    // ------------------------------------------------------------
    // Tensor Core EASU validation
    // ------------------------------------------------------------

    std::cout
        << "\n=====================================\n"
        << "EASU Tensor Core test\n"
        << "=====================================\n";

    EasuTensorPipeline tensorPipeline;

    if (!initEasuTensorPipeline(
            tensorPipeline,
            testInputWidth,
            testInputHeight,
            outputWidth,
            outputHeight))
    {
        std::cerr
            << "Failed to initialize EASU Tensor pipeline.\n";

        return EXIT_FAILURE;
    }

    float tensorEasuMs = 0.0f;

    Image tensorEasu =
        upscaleEasuTensor(
            tensorPipeline,
            easuInput,
            &tensorEasuMs);

    savePPM(
        tensorEasu,
        "volta_easu_tensor.ppm");

    std::cout
        << "Tensor EASU kernel: "
        << tensorEasuMs
        << " ms\n";

    std::cout
        << "\n--- Tensor EASU vs CUDA EASU ---\n";

    int easuTensorMaxError = 0;
    double easuTensorTotalError = 0.0;
    size_t easuTensorSamples = 0;

    for (size_t i = 0;
         i < easuReference.pixels.size();
         i += 4)
    {
        for (int c = 0;
             c < 3;
             ++c)
        {
            const int reference =
                static_cast<int>(
                    easuReference.pixels[i + c]);

            const int tensorValue =
                static_cast<int>(
                    tensorEasu.pixels[i + c]);

            const int error =
                std::abs(
                    reference -
                    tensorValue);

            easuTensorMaxError =
                std::max(
                    easuTensorMaxError,
                    error);

            easuTensorTotalError +=
                static_cast<double>(error);

            ++easuTensorSamples;
        }
    }

    std::cout
        << "Maximum RGB error: "
        << easuTensorMaxError
        << '\n';

    std::cout
        << "Average RGB error: "
        << easuTensorTotalError /
           static_cast<double>(
               easuTensorSamples)
        << '\n';

    // ------------------------------------------------------------
    // Active VoltaDLSS quality preset
    // ------------------------------------------------------------

    std::cout
        << "\n=====================================\n"
        << "VoltaDLSS active preset\n"
        << "=====================================\n";

    const int presetWidth =
        getRenderWidth(
            currentPreset,
            outputWidth);

    const int presetHeight =
        getRenderHeight(
            currentPreset,
            outputHeight);

    std::cout
        << "Preset: "
        << currentSettings.name
        << '\n';

    std::cout
        << "Render: "
        << presetWidth
        << " x "
        << presetHeight
        << '\n';

    std::cout
        << "Output: "
        << outputWidth
        << " x "
        << outputHeight
        << '\n';

    Image presetInput =
        upscaleBilinear(
            lineGroundTruth,
            presetWidth,
            presetHeight);

    float presetMs = 0.0f;

    Image presetOutput;

    if (currentPreset ==
        QualityPreset::Native)
    {
        presetOutput =
            presetInput;

        presetMs = 0.0f;
    }
    else
    {
        presetOutput =
            upscaleEasuTensor(
                tensorPipeline,
                presetInput,
                &presetMs);
    }

    Metrics presetMetrics =
        compareAgainstGroundTruth(
            presetOutput,
            lineGroundTruth);

    std::cout
        << "Tensor EASU: "
        << presetMs
        << " ms\n";

    printMetrics(
        "Active preset",
        presetMetrics);

    savePPM(
        presetOutput,
        "volta_v06_active_preset.ppm");

    destroyEasuTensorPipeline(
        tensorPipeline);

    return EXIT_SUCCESS;
}
