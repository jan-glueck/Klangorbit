#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include "AmbisonicsEncoder.h"
#include "Vec3.h"
#include "VBAP.h"

/**
    Decodes the summed Ambisonics B-format signal (from AmbisonicsEncoder)
    down to one of a fixed set of target output formats -- NOT a general
    "decode to any loudspeaker array" system, only the modes listed in
    Mode below (see the project's own scoping decision on this).

    Three different strategies, chosen per mode:
    - AmbisonicsRawOrder1/2/3/4/5: no decoding at all -- the caller should
      check isRawPassthrough() and, if true, skip this class entirely and
      encode straight into the output buffer exactly as before this class
      existed (zero overhead, per the original requirement). Orders 4/5
      exist purely for higher precision when decoding externally via a
      third-party tool (IEM Plugin Suite, SPARTA) -- nothing about this
      class's own decode paths uses them.
    - Stereo: a plain 2-point Ambisonics decode (SH sampling decoder at
      +-30 degrees) -- deliberately NOT the same code path as Binaural
      (no HRTF involved at all, see the class-level note in
      PluginProcessor about why the two are kept conceptually separate).
      The one mode still decoded via a shared-bus decodeMatrix (see
      decode()) -- every other real-speaker format below bypasses that
      shared bus entirely.
    - Every other mode (Quad/Octophonic/CircularArray/Surround5_1/
      Surround7_1/Atmos*) -- see usesDirectPan(): a DIRECT, per-object VBAP
      pan straight to the real speakers (Pulkki 1997, see VBAP.h),
      computed per source direction via computeDirectPanGains(), NOT
      decoded from a shared Ambisonics bus at all. This replaced an
      earlier two-stage AllRAD (Zotter & Frank 2012) / plain SH-sampling
      approach (decode a dense virtual array from the shared bus, then
      remap that onto the real speakers) -- reported and confirmed as too
      diffuse: even a mathematically correct decode of the shared bus's
      fixed, low (order-3) Ambisonics order has a real, inherent beamwidth
      (~35-40 degrees) that spreads meaningful energy across every real
      speaker once there are only a handful of them, regardless of how
      correct the two-stage remap itself is. Panning each object directly
      (bypassing the shared bus, and the order-3 ceiling, entirely) gives
      genuinely sharp, point-source-style localization instead, at the
      cost of these formats no longer being "true" diffuse-field
      Ambisonics decodes the way AllRAD was. PluginProcessor calls
      computeDirectPanGains() once per object/grain per block and mixes
      with AmbisonicsEncoder::panDirectBlock() directly into the real
      output buffer -- see both of their own comments.
    - Binaural: this class only represents it as a selectable format
      (channel count, bus layout, ambisonics order) -- setMode() stores
      the mode and clears decodeMatrix but never calls decode() for it;
      the actual HRTF decode lives entirely in BinauralDecoder (see that
      class's own comment and the note below).

    Binaural is NOT part of this class -- it needs an HRTF dataset and
    partitioned convolution, handled by a separate module so this one
    stays free of that dependency (see the project's own architecture
    notes on keeping AU-reusable decoder modules unentangled from
    VST3-specific or licensing-specific concerns).
*/
class AmbisonicsDecoder
{
public:
    // Declaration order here matches the Output Format combo's own display
    // order exactly (see OutputPanel.cpp's addItem() calls, and
    // isOutputModeAvailable()'s/that combo's shared position-based
    // id-to-Mode mapping) -- everywhere else in this codebase refers to
    // modes by symbolic name, not ordinal value, so this order is free to
    // change without touching any switch statement.
    enum class Mode
    {
        Stereo,
        Binaural,
        Quad,
        Octophonic,    // fixed 8-channel regular circular array, see SpeakerLayouts::octophonic()
        CircularArray, // generic N-channel regular circular array, see setCircularArraySpeakerCount()
        Surround5_1,
        Surround7_1,
        Atmos5_1_2,
        Atmos5_1_4,
        Atmos7_1_2,
        Atmos7_1_4,
        AmbisonicsRawOrder1,
        AmbisonicsRawOrder2,
        AmbisonicsRawOrder3,
        AmbisonicsRawOrder4, // 25ch -- higher precision for external decoding (IEM/SPARTA/etc.), see the class comment
        AmbisonicsRawOrder5, // 36ch
    };

    // Total number of Mode values (16) -- single source of truth for
    // anything that needs to iterate every mode by its 0-based position
    // (e.g. OutputPanel's combo item IDs, which are position+1; see
    // KlangorbitProcessor::isOutputModeAvailable()/OutputPanel's own
    // graying loop). Kept here, not re-derived, so a future added/removed
    // Mode can't silently desync from a hardcoded count elsewhere.
    static constexpr int numModes = 16;

    // Bounds for CircularArray's speaker count -- below 4 isn't a
    // meaningful "array" (2-3 points aren't circular so much as
    // degenerate VBAP-pair cases already covered by Stereo/other modes);
    // above 24 starts running into the same discreteChannels()-bus
    // practicality limits as any other very-wide-bus mode, with no
    // established use case in this project to justify going further.
    static constexpr int minCircularSpeakers = 4;
    static constexpr int maxCircularSpeakers = 24;

    // Number of physical output channels for a mode (matches
    // outputChannelSetFor(mode).size()). circularSpeakerCount is used
    // ONLY for Mode::CircularArray (clamped to [minCircularSpeakers,
    // maxCircularSpeakers]); ignored for every other mode, including
    // Octophonic, whose channel count is always fixed at 8.
    static int numOutputChannels (Mode mode, int circularSpeakerCount = 8);

    // Which Ambisonics order AmbisonicsEncoder should be set to while this
    // mode is active: matches the mode itself for the raw passthrough
    // modes (order N in, order N out, no decoding), and the fixed
    // internal maximum (3) for every decoded mode, since a higher-order
    // source signal always gives AllRAD/the Stereo decoder more spatial
    // detail to work with regardless of how few physical speakers the
    // target format has.
    static int ambisonicsOrderFor (Mode mode);

    // The JUCE bus layout this mode should declare as the plugin's output
    // -- see PluginProcessor's isBusesLayoutSupported()/makeBusLayout().
    // Uses JUCE's own named layouts (ambisonic(), quadraphonic(),
    // create5point1(), create7point1point4(), etc.) wherever they exist,
    // specifically so hosts that understand named layouts (most do, more
    // reliably than arbitrary discreteChannels()) can show a sensible
    // label instead of just a channel count. JUCE has no named "regular
    // circular array" layout, so Octophonic/CircularArray both use
    // discreteChannels() -- meaning Octophonic and CircularArray with
    // circularSpeakerCount==8 declare the IDENTICAL bus layout. This is a
    // real, accepted ambiguity (a host can't tell which of the two a
    // given 8-channel bus "means" from the layout alone), not a bug --
    // which one is actually active is the plugin's own Mode state
    // (chosen via the Output Format control), independent of the bus.
    // circularSpeakerCount: see numOutputChannels()'s own comment.
    static juce::AudioChannelSet outputChannelSetFor (Mode mode, int circularSpeakerCount = 8);

    // True for the five raw modes -- callers must skip this class
    // entirely in that case (see the class comment).
    static bool isRawPassthrough (Mode mode);

    // True for every mode that bypasses the shared Ambisonics bus entirely
    // in favor of a direct, per-object VBAP pan straight to the real
    // speakers -- Quad, Octophonic, CircularArray, Surround5_1,
    // Surround7_1, and all four Atmos-bed modes. False for Stereo (still
    // its own simple decodeMatrix-based decode), Binaural (HRTF, entirely
    // separate class), and every raw passthrough mode. See the class
    // comment for why.
    static bool usesDirectPan (Mode mode);

    // Physical output channel index carrying LFE for this mode, or -1 if
    // the mode has no LFE channel (Quad, Stereo, the raw Ambisonics modes).
    static int lfeChannelIndexFor (Mode mode);

    void prepare (double sampleRate);

    // (Re)builds the decode matrix for the given mode -- cheap enough to
    // call on every mode change (a handful of milliseconds at most for
    // the largest AllRAD case), but deliberately NOT called per audio
    // block. No-op (beyond storing the mode) for raw passthrough modes.
    void setMode (Mode newMode);
    Mode getMode() const { return mode; }

    // Only meaningful while getMode() == CircularArray (silently ignored
    // by every other mode, including Octophonic -- always fixed at 8).
    // Clamped to [minCircularSpeakers, maxCircularSpeakers]. Rebuilds the
    // decode matrix immediately if CircularArray is already the active
    // mode; the caller (KlangorbitProcessor) is still responsible for the
    // matching best-effort output-bus renegotiation, same as setMode()'s
    // own caller-side responsibility for a mode change.
    void setCircularArraySpeakerCount (int n);
    int getCircularArraySpeakerCount() const { return circularSpeakerCount; }

    // See GrainCloudSettings-style "off by default" reasoning: an LFE
    // channel synthesized from a low-passed W is a real, audible addition
    // to what was mixed, so it defaults to off (silent LFE) rather than
    // silently guessing the user wants it. See decode()'s own comment for
    // the actual filter.
    void setBassManagementEnabled (bool shouldBeEnabled) { bassManagementEnabled = shouldBeEnabled; }
    bool isBassManagementEnabled() const { return bassManagementEnabled; }

    // ambiBuffer must have at least ambisonicsOrderFor(getMode())'s
    // channel count; destBuffer must have at least numOutputChannels(getMode()).
    // OVERWRITES destBuffer's channels (this runs once per block, after
    // every source has already been summed into ambiBuffer -- no
    // accumulation needed here, unlike AmbisonicsEncoder::encodeBlock()).
    // Must not be called when isRawPassthrough(getMode()) is true, or when
    // getMode() == Mode::Binaural (decodeMatrix is empty in both cases --
    // the caller (KlangorbitProcessor) calls BinauralDecoder::decode()
    // instead for that mode, see the class comment).
    void decode (const juce::AudioBuffer<float>& ambiBuffer, juce::AudioBuffer<float>& destBuffer, int numSamples);

    // Real per-speaker VBAP gains for a single source at `direction` (need
    // NOT be unit-length -- normalized internally; a zero-length vector is
    // defensively treated as straight ahead), for whichever mode is
    // CURRENTLY active -- only meaningful while usesDirectPan(getMode())
    // is true (returns an all-zero vector sized numOutputChannels(getMode())
    // otherwise, i.e. it's always safe to call, just inert). The LFE
    // channel (if any) is always exactly 0 here -- LFE has no direction,
    // see applyLfeFilterDirect() for how it's actually populated. Callers
    // (PluginProcessor) pass the result straight into
    // AmbisonicsEncoder::panDirectBlock().
    std::vector<float> computeDirectPanGains (Vec3 direction) const;

    // Same one-pole ~120Hz low-pass Bass Management already applies inside
    // decode() (see that method's own comment) -- shares this object's own
    // lfeFilterState/isBassManagementEnabled(), so switching between a
    // direct-pan and a decodeMatrix-based mode never resets or duplicates
    // filter state. For usesDirectPan() modes, which have no shared
    // Ambisonics-bus W channel to derive LFE from the way decode() does --
    // PluginProcessor instead sums every active object/grain's own dry
    // signal into a small mono scratch buffer and feeds it here. Writes
    // silence (and leaves lfeFilterState untouched) if bass management is
    // currently disabled, same as decode()'s own behavior.
    void applyLfeFilterDirect (const float* monoInput, float* lfeOutput, int numSamples);

private:
    void buildStereoMatrix();

    // (Re)builds the cached VBAP triangulation for `newMode`'s own real
    // speaker layout (directPanRealDirs/directPanRealDirToFullIndex/
    // directPanRegions/directPanNumFullChannels below) -- cheap, called
    // once per setMode()/setCircularArraySpeakerCount() call (mirroring
    // the old buildAllRadMatrix()'s own "not per audio block" cost model),
    // never from the audio thread. Clears all four (leaving
    // computeDirectPanGains() correctly inert) if ! usesDirectPan(newMode).
    void setupDirectPan (Mode newMode);

    // Shared one-pole ~120Hz low-pass implementation for both decode()'s
    // own W-channel-derived LFE and applyLfeFilterDirect()'s dry-sum-
    // derived one -- one filter, one lfeFilterState, regardless of which
    // path is feeding it. Does NOT check isBassManagementEnabled() itself
    // -- both call sites already gate that themselves (decode() explicitly
    // fills silence when disabled instead of calling this at all).
    void applyLfeLowPass (const float* monoInput, float* lfeOutput, int numSamples);

    // Scales decodeMatrix so a test plane wave from many directions
    // averages to roughly unit output energy -- a practical calibration,
    // not a strict SAD-theory derivation. numAmbiCh must match
    // decodeMatrix's current row width. Only buildStereoMatrix() still
    // uses this (every direct-pan mode calibrates itself differently --
    // VBAP's own energy-normalized gains, see VBAP.h).
    void calibrateDecodeMatrix (int numAmbiCh);

    Mode mode = Mode::AmbisonicsRawOrder3;
    double sampleRate = 48000.0;

    // See setCircularArraySpeakerCount(). Default 8 so a fresh instance
    // that's switched straight to CircularArray without an explicit count
    // first gives a sensible, alias-free starting point (see
    // SpeakerLayouts::circularArray()'s own comment).
    int circularSpeakerCount = 8;

    // [outputChannel][ambiChannel] -- empty for raw passthrough modes,
    // Binaural, and every usesDirectPan() mode (see setMode()).
    std::vector<std::vector<float>> decodeMatrix;

    // See setupDirectPan()/computeDirectPanGains() above. LFE-excluded;
    // directPanRealDirToFullIndex[i] is directPanRealDirs[i]'s real output-
    // channel index (mirrors the old buildAllRadMatrix()'s own
    // realDirs/realDirToFullIndex pattern). directPanNumFullChannels is the
    // full (LFE-inclusive) channel count computeDirectPanGains() sizes its
    // result to.
    std::vector<Vec3> directPanRealDirs;
    std::vector<int> directPanRealDirToFullIndex;
    std::vector<VBAP::Region> directPanRegions;
    int directPanNumFullChannels = 0;

    bool bassManagementEnabled = false;
    // Simple one-pole low-pass state for the synthesized LFE signal (see
    // applyLfeLowPass()) -- persistent across blocks like
    // PropagationProcessor's own air-absorption filter, reset in
    // prepare()/setMode().
    float lfeFilterState = 0.0f;
};
