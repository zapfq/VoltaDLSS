#pragma once

#include "scaler.h"

Image upscaleEasuReference(
    const Image& input,
    int outputWidth,
    int outputHeight);

Image upscaleEasuCuda(
    const Image& input,
    int outputWidth,
    int outputHeight,
    float* kernelMs);
