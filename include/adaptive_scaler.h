#pragma once

#include "scaler.h"

Image upscaleAdaptiveReference(
    const Image& input,
    int outputWidth,
    int outputHeight);

Image upscaleAdaptiveTensor(
    const Image& input,
    int outputWidth,
    int outputHeight,
    float* kernelMs);
