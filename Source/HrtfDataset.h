#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include "Vec3.h"

// Forward-declared, not #include "mysofa.h" here -- keeps libmysofa's C
// API (and its own transitive includes) out of every file that includes
// this header, the same PIMPL-for-a-third-party-dependency precedent
// already used by GamepadBridge.h for GameController/GCController.
struct MYSOFA_EASY;

/**
    Thin RAII wrapper around libmysofa's C API (see CMakeLists.txt's own
    FetchContent_Declare(mysofa ...) comment for why this project depends
    on it) for reading AES69/SOFA-format HRTF files -- used identically
    for the two bundled default datasets (KEMAR, SADIE II D1) and for a
    user-supplied custom SOFA file (see OutputPanel's "Custom SOFA
    file..." import), one code path either way.

    Coordinate convention: getFilter()'s `direction` is a unit-length Vec3
    in this project's own front(+x)/left(+y)/up(+z) convention (see
    SoundObject.h) -- passed straight through to libmysofa's
    mysofa_getfilter_float() as Cartesian x/y/z with NO remapping, because
    SOFA's own coordinate convention (confirmed directly against
    libmysofa's own coordinates.c: azimuth = atan2(y, x), matching this
    project's cartesianToSpherical() exactly) already matches this
    project's convention. See SphericalHarmonicsUtils.h's own comment for
    the same fact from BinauralDecoder's side.

    Interaural delay: mysofa_getfilter_float() returns each ear's filter
    (already resampled to the requested rate) AND a separate per-ear
    delay-in-samples value, since the library's own FIR extraction
    strips each impulse response down to its own onset rather than
    keeping every direction time-aligned to a single common reference.
    getFilter() below returns both delays (outDelayLeftSamples/
    outDelayRightSamples) for a possible future enhancement, but
    BinauralDecoder's own v1 implementation does NOT apply them (a
    disclosed, deliberate scope decision, not an oversight -- see its own
    class comment) -- the FIR itself still carries a good deal of usable
    interaural timing information from the relative onset positions
    within its own window, just not the full-precision reconstruction a
    dedicated fractional delay line per ear would give.
*/
class HrtfDataset
{
public:
    HrtfDataset();
    ~HrtfDataset();

    HrtfDataset (const HrtfDataset&) = delete;
    HrtfDataset& operator= (const HrtfDataset&) = delete;

    // Loads/parses sofaFile at the given sample rate -- libmysofa
    // resamples the stored impulse responses internally to match, so
    // every getFilter() result afterward is already at the right rate
    // for direct use, no separate resampling step needed by the caller.
    // Never throws; returns false and fills outError on any failure
    // (a malformed/unsupported file, wrong path, etc.) -- safe to call
    // with an arbitrary user-supplied file. Replaces any previously
    // loaded dataset in this same object either way (success or failure
    // clears the old one first, so a failed reload never silently keeps
    // serving stale filters from a different dataset).
    bool load (const juce::File& sofaFile, double sampleRate, juce::String& outError);

    bool isLoaded() const { return easy != nullptr; }

    // Filter length (samples) of every getFilter() result -- fixed once
    // a file is loaded (0 if nothing is loaded yet).
    int getFilterLength() const { return filterLength; }

    // See the class comment for the coordinate convention and the
    // interaural-delay caveat. Resizes outLeft/outRight to
    // getFilterLength() and fills them with the (interpolated, already
    // sample-rate-matched) impulse response for `direction`. Returns
    // false (leaving the out-params untouched) if nothing is loaded.
    bool getFilter (Vec3 direction, std::vector<float>& outLeft, std::vector<float>& outRight,
                     float& outDelayLeftSamples, float& outDelayRightSamples) const;

private:
    void close();

    MYSOFA_EASY* easy = nullptr;
    int filterLength = 0;
};
