#include "quality.h"
#include "runtime.h"

#include <string>

int main(
    int argc,
    char* argv[])
{
    QualityPreset preset =
        QualityPreset::Quality;

    for (int i = 1;
         i < argc;
         ++i)
    {
        if (std::string(argv[i]) ==
                "--quality" &&
            i + 1 < argc)
        {
            if (!parseQualityPreset(
                    argv[i + 1],
                    preset))
            {
                return 1;
            }

            ++i;
        }
    }

    return runVoltaDLSSRuntime(
        1920,
        1080,
        preset);
}
