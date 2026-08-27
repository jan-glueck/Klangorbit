#include <cstdio>
#include <juce_core/juce_core.h>
#include "../Source/AxisShaping.h"

namespace
{
    int g_failures = 0;

    void check (bool condition, const char* description)
    {
        std::printf ("%s  %s\n", condition ? "PASS" : "FAIL", description);
        if (! condition)
            ++g_failures;
    }

    bool approxEqual (float a, float b, float tolerance = 1.0e-4f)
    {
        return std::abs (a - b) <= tolerance;
    }
}

int main()
{
    // --- Deadzone: exactly-in-deadzone input produces exactly 0 -----------
    {
        check (approxEqual (shapeAxis (0.0f, 0.08f, 2.0f), 0.0f), "shapeAxis: exact center produces exactly 0");
        check (approxEqual (shapeAxis (0.08f, 0.08f, 2.0f), 0.0f), "shapeAxis: input exactly at the deadzone boundary produces 0");
        check (approxEqual (shapeAxis (0.05f, 0.08f, 2.0f), 0.0f), "shapeAxis: input inside the deadzone produces 0");
        check (approxEqual (shapeAxis (-0.05f, 0.08f, 2.0f), 0.0f), "shapeAxis: negative input inside the deadzone produces 0");
    }

    // --- Full-scale endpoints: +-1 input always maps to +-1 output, ------
    //     regardless of deadzone/exponent (both rescale within [0,1] and
    //     pow(1,n) == 1). ---
    {
        check (approxEqual (shapeAxis (1.0f, 0.08f, 2.0f), 1.0f), "shapeAxis: full-scale positive input maps to exactly 1.0");
        check (approxEqual (shapeAxis (-1.0f, 0.08f, 2.0f), -1.0f), "shapeAxis: full-scale negative input maps to exactly -1.0");
        check (approxEqual (shapeAxis (1.0f, 0.0f, 1.0f), 1.0f), "shapeAxis: full-scale input maps to 1.0 with no deadzone/linear curve too");
    }

    // --- No jump at the deadzone boundary: a magnitude just past the ------
    //     deadzone starts near 0 output, not at some nonzero jump. ---
    {
        const float justPast = shapeAxis (0.081f, 0.08f, 2.0f);
        check (justPast >= 0.0f && justPast < 0.01f, "shapeAxis: a magnitude just past the deadzone boundary starts near 0 (no jump)");
    }

    // --- Sign preservation --------------------------------------------------
    {
        check (shapeAxis (0.5f, 0.08f, 2.0f) > 0.0f, "shapeAxis: positive input beyond the deadzone produces positive output");
        check (shapeAxis (-0.5f, 0.08f, 2.0f) < 0.0f, "shapeAxis: negative input beyond the deadzone produces negative output");
        check (approxEqual (shapeAxis (0.5f, 0.08f, 2.0f), -shapeAxis (-0.5f, 0.08f, 2.0f)),
               "shapeAxis: output is symmetric (odd function) around center");
    }

    // --- Exponential curve: at a fixed rescaled input, a higher exponent --
    //     produces a SMALLER output (fine control near center, full speed
    //     reserved for a more deliberate deflection). ---
    {
        // Rescaled value 0.5 (halfway between deadzone and full scale)
        // should shrink as the exponent grows: linear (n=1) > n=2 > n=3.
        const float midpointInput = 0.08f + 0.5f * (1.0f - 0.08f); // rescales to exactly 0.5
        const float linear = shapeAxis (midpointInput, 0.08f, 1.0f);
        const float squared = shapeAxis (midpointInput, 0.08f, 2.0f);
        const float cubed = shapeAxis (midpointInput, 0.08f, 3.0f);
        std::printf ("       midpoint shaped values: linear=%.4f n=2:%.4f n=3:%.4f\n", linear, squared, cubed);
        check (approxEqual (linear, 0.5f), "shapeAxis: linear curve (exponent 1.0) reproduces the rescaled value exactly");
        check (squared < linear, "shapeAxis: a squared curve produces a smaller output than linear at the same input");
        check (cubed < squared, "shapeAxis: a cubed curve produces an even smaller output than squared at the same input");
    }

    // --- Monotonic: shaped output never decreases as input magnitude ------
    //     increases (within one sign), so the mapping is well-behaved
    //     across its whole range, not just at a few sample points. ---
    {
        bool monotonic = true;
        float previous = -1.0f;
        for (int i = 0; i <= 20; ++i)
        {
            const float x = (float) i / 20.0f; // 0..1
            const float y = shapeAxis (x, 0.08f, 2.0f);
            if (y < previous - 1.0e-6f)
                monotonic = false;
            previous = y;
        }
        check (monotonic, "shapeAxis: output is monotonically non-decreasing across the positive input range");
    }

    std::printf ("\n%s (%d failure%s)\n", g_failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED",
                 g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
