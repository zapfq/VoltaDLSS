#pragma once

enum class QualityPreset
{
    UltraPerformance,
    Performance,
    Balanced,
    Quality,
    Native
};

const char* getQualityPresetName(QualityPreset preset);

const char* getQualityPresetDescription(QualityPreset preset);

bool parseQualityPreset(
    const char* value,
    QualityPreset& preset);

float getQualityScale(QualityPreset preset);

void getQualityResolution(
    QualityPreset preset,
    int outputWidth,
    int outputHeight,
    int& renderWidth,
    int& renderHeight);
