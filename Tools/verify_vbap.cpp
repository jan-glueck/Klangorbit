#include <cmath>
#include <cstdio>
#include <juce_core/juce_core.h>
#include "../Source/VBAP.h"
#include "../Source/SpeakerLayouts.h"

namespace
{
    int g_failures = 0;

    void check (bool condition, const char* description)
    {
        std::printf ("%s  %s\n", condition ? "PASS" : "FAIL", description);
        if (! condition)
            ++g_failures;
    }

    bool approxEqual (float a, float b, float tolerance = 1.0e-3f)
    {
        return std::abs (a - b) <= tolerance;
    }

    std::vector<Vec3> directionsOf (const std::vector<SpeakerLayouts::Speaker>& speakers)
    {
        std::vector<Vec3> dirs;
        for (auto& s : speakers)
            if (! s.isLfe)
                dirs.push_back (s.direction);
        return dirs;
    }

    float sumOfSquares (const std::vector<float>& gains)
    {
        float total = 0.0f;
        for (float g : gains) total += g * g;
        return total;
    }
}

int main()
{
    // --- Quad (horizontal-ring / 2-speaker-pair case) --------------------
    {
        const auto dirs = directionsOf (SpeakerLayouts::quad());
        check (dirs.size() == 4, "Quad: LFE-free direction list has 4 entries");

        const auto regions = VBAP::triangulate (dirs);
        check (regions.size() == 4, "Quad: horizontal-ring triangulation produces 4 adjacent pairs (not triangles)");
        bool allPairs = true;
        for (auto& r : regions) if (r.c >= 0) allPairs = false;
        check (allPairs, "Quad: every region is a 2-speaker pair (c == -1), not a 3D triangle");

        // Exactly at speaker 0's own direction: that speaker should get
        // (near-)full gain, everyone else (near-)zero.
        const auto gainsAtSpeaker0 = VBAP::computeGains (dirs[0], dirs, regions);
        check (approxEqual (gainsAtSpeaker0[0], 1.0f, 0.01f), "Quad: gain at a speaker's own direction is ~1.0 for that speaker");
        for (size_t i = 1; i < gainsAtSpeaker0.size(); ++i)
            check (gainsAtSpeaker0[i] < 0.05f, "Quad: gain at a speaker's own direction is ~0 for the other speakers");

        // Exactly between speakers 0 and 1 (their direction average,
        // renormalized): both should get equal, energy-normalized gain.
        Vec3 mid = dirs[0] + dirs[1];
        mid = mid / mid.length();
        const auto gainsAtMid = VBAP::computeGains (mid, dirs, regions);
        const float sumSq = sumOfSquares (gainsAtMid);
        check (approxEqual (sumSq, 1.0f, 0.01f), "Quad: gains at the midpoint between two speakers are energy-normalized (sum of squares == 1)");
        check (approxEqual (gainsAtMid[0], gainsAtMid[1], 0.05f), "Quad: gains at the midpoint between two speakers are equal for those two");
        check (gainsAtMid[2] < 1.0e-3f && gainsAtMid[3] < 1.0e-3f, "Quad: gains at the midpoint between two speakers are exactly 0 for the other two");
    }

    // --- 5.1.4 (3D hull case, has height speakers) ------------------------
    {
        const auto dirs = directionsOf (SpeakerLayouts::atmos5point1point4());
        check (dirs.size() == 9, "5.1.4: LFE-free direction list has 9 entries (10 channels minus the LFE)");

        const auto regions = VBAP::triangulate (dirs);
        check (regions.size() > 0, "5.1.4: 3D hull triangulation produces at least one face");
        bool anyTriangle = false;
        for (auto& r : regions) if (r.c >= 0) anyTriangle = true;
        check (anyTriangle, "5.1.4: at least one region is a proper 3D triangle (has height speakers, not a flat ring)");

        // Every direction used to build the hull must itself be covered by
        // some region with that speaker at (near-)full gain -- otherwise
        // the hull doesn't actually enclose all the speakers it was built from.
        bool allSpeakersCovered = true;
        for (size_t i = 0; i < dirs.size(); ++i)
        {
            const auto gains = VBAP::computeGains (dirs[i], dirs, regions);
            if (gains[i] < 0.9f)
                allSpeakersCovered = false;
        }
        check (allSpeakersCovered, "5.1.4: panning directly at each speaker's own position gives that speaker (near-)full gain");

        // A direction straight up should draw energy mostly from the top
        // speakers (indices 6-9: top-front L/R, top-rear L/R), not the
        // ear-level bed.
        const Vec3 straightUp { 0.0f, 0.0f, 1.0f };
        const auto gainsUp = VBAP::computeGains (straightUp, dirs, regions);
        float topEnergy = 0.0f, bedEnergy = 0.0f;
        for (size_t i = 0; i < gainsUp.size(); ++i)
        {
            if (i >= 6) topEnergy += gainsUp[i] * gainsUp[i];
            else        bedEnergy += gainsUp[i] * gainsUp[i];
        }
        std::printf ("       straight-up energy split: top=%.3f, bed=%.3f\n", topEnergy, bedEnergy);
        check (topEnergy > bedEnergy, "5.1.4: panning straight up draws more energy from the top/height speakers than the ear-level bed");
    }

    // --- General sanity: gains stay energy-normalized across many random-ish directions ---
    {
        const auto dirs = directionsOf (SpeakerLayouts::atmos7point1point4());
        const auto regions = VBAP::triangulate (dirs);
        bool allNormalized = true;
        for (int i = 0; i < 64; ++i)
        {
            const float az = (float) i * 0.31f;
            const float el = std::sin ((float) i * 0.7f) * 1.2f; // sweeps through some out-of-range values too
            const Vec3 dir = SpeakerLayouts::directionFromAngles (az, juce::jlimit (-1.5f, 1.5f, el));
            const auto gains = VBAP::computeGains (dir, dirs, regions);
            const float sumSq = sumOfSquares (gains);
            if (! approxEqual (sumSq, 1.0f, 0.02f))
                allNormalized = false;
        }
        check (allNormalized, "7.1.4: gains stay energy-normalized (sum of squares ~1) across a sweep of directions");
    }

    std::printf ("\n%s (%d failure%s)\n", g_failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED",
                 g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
