#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include "AmbisonicsEncoder.h"

/**
    Decodes the summed Ambisonics B-format signal (from AmbisonicsEncoder)
    down to one of a fixed set of target output formats -- NOT a general
    "decode to any loudspeaker array" system, only the modes listed in
    Mode below (see the project's own scoping decision on this).

    Four different decoding strategies, chosen per mode:
    - AmbisonicsRawOrder1/2/3: no decoding at all -- the caller should
      check isRawPassthrough() and, if true, skip this class entirely and
      encode straight into the output buffer exactly as before this class
      existed (zero overhead, per the original requirement).
    - Stereo: a plain 2-point Ambisonics decode (SH sampling decoder at
      +-30 degrees) -- deliberately NOT the same code path as Binaural
      (no HRTF involved at all, see the class-level note in
      PluginProcessor about why the two are kept conceptually separate).
    - Binaural: this class only represents it as a selectable format
      (channel count, bus layout, ambisonics order) -- setMode() stores
      the mode and clears decodeMatrix but never calls decode() for it;
      the actual HRTF decode lives entirely in BinauralDecoder (see that
      class's own comment and the note below).
    - Quad/Surround5_1/Surround7_1/Atmos*: AllRAD (All-Round Ambisonic
      Decoding, Zotter & Frank 2012) -- decode to a large, densely and
      uniformly distributed VIRTUAL loudspeaker array first (a plain SH
      sampling decode, reusing AmbisonicsEncoder::computeShCoefficients()
      since encode/decode coefficients are the same real SH basis), then
      remap that virtual array onto the real (sparse, irregular) target
      layout via VBAP (see VBAP.h). This is the standard, correct approach
      for irregular arrays -- a direct pseudo-inverse decode straight to a
      handful of unevenly-spaced real speakers tends to produce uneven,
      direction-dependent coloration that AllRAD's two-stage approach
      avoids. The combined ambisonics -> real-speakers matrix is built
      ONCE per setMode() call (not per audio block) and calibrated so a
      test plane wave from many directions averages to roughly unit
      output energy (a practical calibration, not a strict SAD-theory
      derivation -- see buildAllRadMatrix()'s own comment).
    - Octophonic/CircularArray: a plain mode-matching decode straight to
      the real (regular, evenly-spaced, horizontal-only) speaker ring --
      deliberately NOT AllRAD/VBAP. AllRAD's virtual-array-plus-remap
      exists specifically to avoid coloration on an IRREGULAR array; a
      regular ring has no such irregularity to correct for, and a plain
      SH-sampling decode straight to its own (already evenly-spaced) real
      speakers is the standard, simpler, and at least as accurate choice
      for this specific case -- see buildCircularMatrix()'s own comment,
      including the alias-free speaker-count threshold for the fixed
      internal order. Both are horizontal-only (0 elevation for every
      speaker) -- a circular array cannot reproduce height/elevation at
      all, a property of this array type, not a decoder limitation.

    Binaural is NOT part of this class -- it needs an HRTF dataset and
    partitioned convolution, handled by a separate module so this one
    stays free of that dependency (see the project's own architecture
    notes on keeping AU-reusable decoder modules unentangled from
    VST3-specific or licensing-specific concerns).
*/
class AmbisonicsDecoder
{
public:
    enum class Mode
    {
        AmbisonicsRawOrder1,
        AmbisonicsRawOrder2,
        AmbisonicsRawOrder3,
        Stereo,
        Binaural,
        Quad,
        Surround5_1,
        Surround7_1,
        Atmos5_1_2,
        Atmos5_1_4,
        Atmos7_1_2,
        Atmos7_1_4,
        Octophonic,   // fixed 8-channel regular circular array, see SpeakerLayouts::octophonic()
        CircularArray, // generic N-channel regular circular array, see setCircularArraySpeakerCount()
    };

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

    // True for the three raw modes -- callers must skip this class
    // entirely in that case (see the class comment).
    static bool isRawPassthrough (Mode mode);

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

private:
    void buildStereoMatrix();
    void buildAllRadMatrix (Mode targetMode);
    void buildCircularMatrix (Mode targetMode);

    // Scales decodeMatrix so a test plane wave from many directions
    // averages to roughly unit output energy -- a practical calibration,
    // not a strict SAD-theory derivation, see buildAllRadMatrix()'s own
    // comment. numAmbiCh must match decodeMatrix's current row width.
    void calibrateDecodeMatrix (int numAmbiCh);

    Mode mode = Mode::AmbisonicsRawOrder3;
    double sampleRate = 48000.0;

    // See setCircularArraySpeakerCount(). Default 8 so a fresh instance
    // that's switched straight to CircularArray without an explicit count
    // first gives a sensible, alias-free (see buildCircularMatrix())
    // starting point.
    int circularSpeakerCount = 8;

    // [outputChannel][ambiChannel] -- empty for raw passthrough modes.
    std::vector<std::vector<float>> decodeMatrix;

    bool bassManagementEnabled = false;
    // Simple one-pole low-pass state for the synthesized LFE signal (see
    // decode()) -- persistent across blocks like PropagationProcessor's
    // own air-absorption filter, reset in prepare()/setMode().
    float lfeFilterState = 0.0f;
};
