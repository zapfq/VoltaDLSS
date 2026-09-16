#pragma once

#include "scaler.h"

Image upscaleTensorBilinear2x(
    const Image& input,
    int outputWidth,
    int outputHeight,
    float* kernelMs);
