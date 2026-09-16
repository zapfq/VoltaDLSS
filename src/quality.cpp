#include "quality.h"

#include <algorithm>
#include <cmath>
#include <cstring>

const char* getQualityPresetName(QualityPreset preset)
{
    switch (preset)
    {
    case QualityPreset::UltraPerformance:
        return "Ultra Performance";

    case QualityPreset::Performance:
        return "Performance";

    case QualityPreset::Balanced:
        return "Balanced";

    case QualityPreset::Quality:
        return "Quality";

    case QualityPreset::Native:
        return "Native";

    default:
        return "Balanced";
    }
}

const char* getQualityPresetDescription(QualityPreset preset)
{
    switch (preset)
    {
    case QualityPreset::UltraPerformance:
        return "33.3% render resolution";

    case QualityPreset::Performance:
        return "44.4% render resolution";

    case QualityPreset::Balanced:
        return "50.0% render resolution";

    case QualityPreset::Quality:
        return "66.7% render resolution";

    case QualityPreset::Native:
        return "100% native resolution";

    default:
        return "50.0% render resolution";
    }
}

bool parseQualityPreset(
    const char* value,
    QualityPreset& preset)
{
    if (!value || value[0] == '\0')
        return false;

    if (_stricmp(value, "ultra-performance") == 0 ||
        _stricmp(value, "ultra_performance") == 0 ||
        _stricmp(value, "ultra") == 0 ||
        std::strcmp(value, "1") == 0)
    {
        preset = QualityPreset::UltraPerformance;
        return true;
    }

    if (_stricmp(value, "performance") == 0 ||
        std::strcmp(value, "2") == 0)
    {
        preset = QualityPreset::Performance;
        return true;
    }

    if (_stricmp(value, "balanced") == 0 ||
        std::strcmp(value, "3") == 0)
    {
        preset = QualityPreset::Balanced;
        return true;
    }

    if (_stricmp(value, "quality") == 0 ||
        std::strcmp(value, "4") == 0)
    {
        preset = QualityPreset::Quality;
        return true;
    }

    if (_stricmp(value, "native") == 0 ||
        _stricmp(value, "native-resolution") == 0 ||
        std::strcmp(value, "5") == 0)
    {
        preset = QualityPreset::Native;
        return true;
    }

    return false;
}

float getQualityScale(QualityPreset preset)
{
    switch (preset)
    {
    case QualityPreset::UltraPerformance:
        return 0.33333334f;

    case QualityPreset::Performance:
        return 0.44444445f;

    case QualityPreset::Balanced:
        return 0.5f;

    case QualityPreset::Quality:
        return 0.66666669f;

    case QualityPreset::Native:
        return 1.0f;

    default:
        return 0.5f;
    }
}

void getQualityResolution(
    QualityPreset preset,
    int outputWidth,
    int outputHeight,
    int& renderWidth,
    int& renderHeight)
{
    if (outputWidth <= 0 || outputHeight <= 0)
    {
        renderWidth = 0;
        renderHeight = 0;
        return;
    }

    const float scale =
        getQualityScale(preset);

    renderWidth =
        std::max(
            1,
            static_cast<int>(
                std::round(
                    static_cast<float>(outputWidth) *
                    scale)));

    renderHeight =
        std::max(
            1,
            static_cast<int>(
                std::round(
                    static_cast<float>(outputHeight) *
                    scale)));
}
