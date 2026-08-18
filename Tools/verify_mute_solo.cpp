#include <cstdio>
#include "../Source/MuteSoloLogic.h"

namespace
{
    int g_failures = 0;

    void check (bool condition, const char* description)
    {
        std::printf ("%s  %s\n", condition ? "PASS" : "FAIL", description);
        if (! condition)
            ++g_failures;
    }
}

int main()
{
    using MuteSoloLogic::isEffectivelyMuted;

    // --- No solo anywhere: only this object's own `muted` matters ---
    check (! isEffectivelyMuted (false, false, false), "unmuted, not soloed, nothing soloed anywhere: audible");
    check (isEffectivelyMuted (true, false, false), "muted, not soloed, nothing soloed anywhere: silent (own mute)");
    check (! isEffectivelyMuted (false, true, false), "not muted, soloed, but anySoloed=false is inconsistent input -- still just reflects muted=false: audible");

    // --- Some other object is soloed (anySoloed=true), this one is not ---
    check (isEffectivelyMuted (false, false, true), "unmuted, not soloed, but another object IS soloed: silent (solo silences the rest)");
    check (! isEffectivelyMuted (false, true, true), "unmuted, soloed, and it's part of the soloed set: stays audible");

    // --- Own mute always wins, even while soloed ---
    check (isEffectivelyMuted (true, true, true), "muted AND soloed: still silent -- own mute takes precedence over solo");
    check (isEffectivelyMuted (true, true, false), "muted AND soloed, even with no other solo active: still silent (own mute)");

    // --- Multiple simultaneous solos: classic non-exclusive DAW solo ---
    // (This isn't directly expressible with a single call since
    // isEffectivelyMuted only sees one object's own soloed flag plus the
    // pre-computed anySoloed -- but the two objects below, evaluated
    // independently with the same anySoloed=true, both being soloed
    // themselves, both stay audible, confirming solo isn't an exclusive
    // radio button.)
    check (! isEffectivelyMuted (false, true, true) && ! isEffectivelyMuted (false, true, true),
           "two independently-soloed objects (same anySoloed=true) both stay audible -- non-exclusive solo");

    std::printf ("\n%s (%d failure%s)\n", g_failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED",
                 g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
