#pragma once

/**
    Pure decision logic for SoundObject::muted/soloed (see its class
    comment), shared between PluginProcessor's two per-object loops (main
    object + its GrainCloud's grains) and Tools/verify_mute_solo.cpp --
    same "shared, not reimplemented" principle as SlingGesture.h/
    OrbitMath.h/GrainDoppler.h elsewhere in this codebase.

    Rules:
    - An object's own `muted` always wins, regardless of solo state --
      an object can never be simultaneously "definitely silent" and
      "definitely audible".
    - Otherwise, if ANY object is soloed, every object that is NOT itself
      soloed goes silent -- classic non-exclusive DAW solo: several
      objects can be soloed together and all stay audible, this is not a
      single-object radio button.
    - If nothing is soloed, only `muted` matters.
*/
namespace MuteSoloLogic
{
    inline bool isEffectivelyMuted (bool objectMuted, bool objectSoloed, bool anyObjectSoloed)
    {
        if (objectMuted)
            return true;
        return anyObjectSoloed && ! objectSoloed;
    }
}
