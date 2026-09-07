#pragma once
#include "Vec3.h"

/**
    How a grain moves for the duration of its (short) life. See
    GrainCloud::updateGrain() for the actual integration per mode.
*/
enum class GrainMovementMode
{
    RandomWalk,          // smoothed random-direction drift
    Bounce,               // bounces inside a small sphere around its spawn position
    RadialExplosion,      // flies outward from the parent's position at spawn time
    OrbitAroundParent,    // circles the parent's CURRENT (possibly moving) position
    AttractRepelSiblings  // n-body force from other active grains in the same cloud only
};

/** Grain amplitude envelope shape. Only Hann for now, deliberately extensible. */
enum class GrainWindowShape
{
    Hann
};

/**
    How a grain's start point is drawn from within [grainReadDepthRangeMin,
    grainReadDepthRangeMax] (see GrainCloudSettings) -- deliberately just
    three simple presets rather than a general parametric distribution
    model, since that's what actually gets used in practice for this kind
    of control (no user-facing need for e.g. a tunable skew exponent).
*/
enum class GrainReadDepthDistribution
{
    Uniform,              // every depth in the range equally likely
    WeightedTowardRecent, // biased toward the MIN end (shallower into the past)
    WeightedTowardOld     // biased toward the MAX end (deeper into the past)
};

/**
    How GrainCloudSettings::pitchJitter is turned into a per-grain
    playbackRate at spawn time (see GrainCloud::spawnGrain()). Random is
    the original behavior (continuous, uniformly random +/- deviation);
    Scale instead quantizes to the nearest degree(s) of a chosen
    PitchQuantizeScale, letting a grain cloud stay musically "in tune"
    with itself (or with a chosen overtone/interval series) rather than
    detuning arbitrarily.
*/
enum class PitchJitterMode
{
    Random,
    Scale
};

/**
    Interval sets (semitone offsets within one octave, 0 = the grain's own
    natural/unshifted pitch, always included) used by PitchJitterMode::Scale
    -- see GrainCloud.cpp's scaleSemitones() for the actual values and
    pickQuantizedSemitoneOffset() for how a grain's final semitone offset is
    chosen from them. Deliberately no separate "root note"/key picker: the
    grain's own natural pitch (ratio 1.0, i.e. offset 0) IS the root, so
    these are all just interval sets relative to whatever the source
    material's own pitch already is, not absolute pitches.

    Octaves/Fifths are bare intervals rather than full scales, included
    because they're useful "sparse" quantizers in their own right (e.g.
    Octaves alone removes all pitch variation except octave jumps).
    Acoustic is the "overtone scale"/Lydian Dominant -- the closest
    standard 12-tone-equal-tempered scale to the (inharmonic, continuously
    spaced) natural overtone series, included specifically to cover a
    "tune grain pitch jitter to the overtone series" request; the real
    harmonic series itself has no exact equal-tempered representation, so
    this is a practical approximation, same spirit as this project's other
    disclosed simplified models (e.g. PropagationProcessor's air
    absorption).
*/
enum class PitchQuantizeScale
{
    Octaves,
    Fifths,
    MajorTriad,
    MinorTriad,
    MajorScale,
    Dorian,
    Lydian,
    Mixolydian,
    Aeolian,
    WholeTone,
    Octatonic,
    Hexatonic,
    Acoustic
};

/**
    LEGACY ONLY -- superseded by dedicated per-parameter jitter fields on
    GrainCloudSettings (grainRateJitter, grainDurationJitter,
    initialSpeedJitter, boundaryRadiusJitter, orbitRadiusJitter), which can
    all be used at once instead of this single mutually-exclusive choice.
    Kept only so PresetManager can still read old presets that saved a
    "jitterTarget"/"jitterRange" pair and migrate them onto the matching
    new field -- see PresetManager.cpp's grainCloudSettingsFromVar().
*/
enum class GrainJitterTarget
{
    None,
    InitialSpeed,     // RadialExplosion
    Lifetime,         // all modes (== grainDuration for that grain)
    BoundaryRadius,   // Bounce
    OrbitRadius        // OrbitAroundParent
};

/**
    Central limits shared by the UI (ParameterPanel's slider ranges) and
    the audio-side ring buffer allocation (PluginProcessor) -- kept in one
    place so the buffer can't silently become too small again if a range
    changes without the other being reconsidered. This is a real,
    concrete risk here: a grain can need to look back into its parent's
    ring buffer by up to positionJitterInBuffer, then read forward through
    up to grainDuration seconds of output time -- consuming up to
    maxPitchJitterPlaybackRate times that much SOURCE material if pitched
    up. Both ends of that reach must fit within the buffer.
*/
namespace GrainLimits
{
    constexpr float maxGrainDuration = 5.0f;             // seconds, ParameterPanel's upper slider bound
    constexpr float maxGrainRate = 500.0f;                // grains/sec, ParameterPanel's upper slider bound
    constexpr float maxPositionJitterInBuffer = 1.5f;    // seconds, ParameterPanel's upper slider bound

    // See GrainCloud::spawnGrain(): playbackRate = 1 + pitchJitter*(-1..1),
    // clamped to a minimum of 0.1; pitchJitter's own max is 1.0, so the
    // resulting rate range is [0.1, 2.0]. 2.0 is the relevant bound here
    // (it's the direction that consumes MORE source material per second
    // of output).
    constexpr float maxPitchJitterPlaybackRate = 2.0f;

    // ParameterPanel's upper slider bound for grainReadDepthRangeMin/Max --
    // also the mechanism that keeps the UI from ever letting the user
    // configure a depth beyond what requiredRingBufferSeconds below
    // actually allocates: the slider simply can't go higher, rather than
    // silently clamping or failing at spawn time.
    constexpr float maxGrainReadDepthRange = 10.0f;      // seconds

    // Longest possible look-back a single grain can ever need, given the
    // limits above -- the ring buffer must be at least this long. The
    // depth-range offset (grainReadDepthRange) and the pre-existing
    // positionJitterInBuffer are sampled independently and ADD together
    // (see GrainCloud::spawnGrain()), so both maxima are summed here.
    constexpr float requiredRingBufferSeconds = maxPositionJitterInBuffer + maxGrainReadDepthRange + maxGrainDuration * maxPitchJitterPlaybackRate;
}

/**
    Cloud-wide parameters: one set applies to the whole cloud (all grains
    spawned by it), not per grain -- see ParameterPanel, there is no
    per-grain UI.

    A grain's audio envelope and its movement lifetime are the SAME
    duration (grainDuration): a grain is a single spawn-move-fade event,
    not a persistent point that repeatedly re-triggers audio. grainRate
    ("Spawn Rate" in the UI) is how often NEW grains are spawned (classic
    granular-synthesis "density" parameter) -- deliberately independent of
    grainDuration at the scheduling level (see GrainCloud::update(): the
    spawn-interval timer only ever reads grainRate, never grainDuration),
    so a long grainDuration does not, by itself, throttle how often new
    grains start. Grains overlap whenever grainRate * grainDuration > 1
    (that's the intended, expected way to get a dense/continuous texture,
    not a side effect).

    The one place these two DO interact: maxConcurrentGrains below is a
    hard ceiling on how many of this cloud's grains may be alive at once
    (a real CPU cost limit -- every active grain is a full Ambisonics
    encode pass, see PluginProcessor). Sustaining grainRate * grainDuration
    overlapping grains needs maxConcurrentGrains to be at least that
    large. Earlier versions of this cloud simply stopped spawning once the
    ceiling was hit, waiting for an existing grain's grainDuration to
    elapse -- audibly throttling/stuttering the spawn rate whenever
    grainRate * grainDuration exceeded the cap. GrainCloud::update() now
    instead does voice stealing: the OLDEST active grain is given a short
    (10ms) forced fade-out and its slot reused immediately, so grainRate's
    spawn schedule is honored continuously regardless of grainDuration --
    the trade-off is that individual grains can end up shorter than
    configured once oversubscribed, rather than new grains simply not
    starting on time. See GrainCloud::beginVoiceSteal().
*/
struct GrainCloudSettings
{
    bool enabled = false;

    // Mutes the parent SoundObject's own dry/unGranulated audio while
    // leaving its grains completely unaffected -- lets the grains be
    // heard in isolation, independent of SoundObject::muted/soloed
    // (which silence BOTH the source and its grains together, see
    // MuteSoloLogic.h). Read directly from PluginProcessor::processBlock's
    // main-object rendering pass only; the separate grain-rendering pass
    // deliberately never looks at this field, only at muted/soloed.
    bool sourceMuted = false;

    // --- Audio side ------------------------------------------------------
    float grainRate = 10.0f;                  // "Grain Rate (Spawn Rate)" in the UI; grains/sec spawned while enabled, independent of grainDuration -- see this struct's own comment
    // 0..1, fractional +/- randomization applied to the spawn INTERVAL
    // each time a grain is about to spawn (see GrainCloud::update()) --
    // humanizes the cadence away from a perfectly metronomic beat. Same
    // applyJitter() shape as pitchJitter below, just applied to timing
    // instead of playback rate.
    float grainRateJitter = 0.0f;
    float grainDuration = 0.15f;              // seconds; also the grain's movement lifetime
    // 0..1, fractional +/- randomization applied to grainDuration at
    // spawn time (this grain's own lifetime only, doesn't affect any
    // other already-active grain).
    float grainDurationJitter = 0.0f;
    float pitchJitter = 0.0f;                 // 0..1, reach of the per-grain pitch deviation -- see pitchJitterMode for how it's applied
    // Random (default): pitchJitter directly scales a continuous, uniformly
    // random +/- playback-rate deviation (unchanged original behavior).
    // Scale: pitchJitter instead scales how far (in semitones, capped at
    // one octave -- see GrainCloud.cpp's maxScaleReachSemitones) a randomly
    // picked degree of pitchQuantizeScale may sit from the grain's own
    // natural pitch, and the grain is quantized exactly onto that degree.
    PitchJitterMode pitchJitterMode = PitchJitterMode::Random;
    PitchQuantizeScale pitchQuantizeScale = PitchQuantizeScale::MajorScale; // only used while pitchJitterMode == Scale
    float positionJitterInBuffer = 0.05f;     // seconds, random look-back offset into the ring buffer per grain
    // Per-cloud local cap (on top of the global cap, see PluginProcessor).
    // Raised over time from an initial default of 8 -- comfortably covers
    // common grainRate*grainDuration combinations without needing voice
    // stealing (see this struct's own comment) at all; matches the global
    // cap (256, PluginProcessor::maxConcurrentGrainsGlobal) shared across
    // every object's cloud, so a single object granulating alone can use
    // the whole budget if nothing else is competing for it.
    int maxConcurrentGrains = 256;
    GrainWindowShape windowShape = GrainWindowShape::Hann;
    // Per-grain Doppler pitch shift, based on each grain's own velocity
    // relative to the listener at the origin -- separate from and default
    // OFF unlike SoundObject::dopplerFactor (main objects), since grains
    // deliberately skip PropagationProcessor entirely (no delay line, no
    // per-sample cost) and this adds a small but nonzero per-grain,
    // per-BLOCK computation instead (see GrainDoppler.h) -- opt-in rather
    // than silently changing existing grain-cloud sound. When enabled, the
    // parent SoundObject's own dopplerFactor still scales the effect's
    // strength (0 = no shift even if this is on, 1 = physical, >1 =
    // exaggerated) -- one familiar knob, not a second one.
    bool dopplerEnabled = false;

    // How far into the ring buffer's PAST a grain's start point may be
    // drawn from, independent of (and additive with) positionJitterInBuffer
    // above -- that field is a small amount of "de-clicking" randomization
    // near the current write head, this is a deliberate, potentially much
    // larger reach back into history (e.g. to grab material from seconds
    // ago instead of only the last ~50ms). Both 0.0f by default = disabled
    // (grain start = write head, same as before this field existed).
    float grainReadDepthRangeMin = 0.0f;      // seconds
    float grainReadDepthRangeMax = 0.0f;      // seconds
    GrainReadDepthDistribution grainReadDepthDistribution = GrainReadDepthDistribution::Uniform;

    // --- Movement side ----------------------------------------------------
    GrainMovementMode movementMode = GrainMovementMode::RandomWalk;

    float randomWalkSpeed = 1.0f;             // m/s, RandomWalk

    float boundaryRadius = 1.0f;              // m, Bounce -- sphere around the grain's spawn position
    float boundaryRadiusJitter = 0.0f;        // 0..1, +/- randomization of boundaryRadius at spawn time (Bounce)
    float restitution = 0.6f;                 // Bounce, 0..1

    float initialSpeed = 2.0f;                // m/s, RadialExplosion
    float initialSpeedJitter = 0.0f;          // 0..1, +/- randomization of initialSpeed at spawn time (RadialExplosion)
    float acceleration = 0.0f;                // m/s^2 along the explosion direction, RadialExplosion

    float orbitRadius = 0.5f;                 // m, OrbitAroundParent
    float orbitRadiusJitter = 0.0f;           // 0..1, +/- randomization of orbitRadius at spawn time (OrbitAroundParent)
    float orbitAngularSpeed = 2.0f;           // rad/s, OrbitAroundParent
    // 0..1, OrbitAroundParent: blends each grain's own orbit PLANE
    // orientation from flat (0 -- every grain circles in the same
    // horizontal x/y plane, the original/default behavior) toward a
    // uniformly random direction (1 -- each grain's plane is oriented
    // essentially at random, so over many grains and full rotations the
    // swept shape approaches a sphere instead of a flat disc). Picked
    // once per grain at spawn time (GrainCloud::spawnGrain(), stored in
    // Grain::orbitPlaneNormal), not re-randomized per tick -- a given
    // grain keeps circling in its own fixed plane for its whole life,
    // same "decided once, fixed for this grain's life" treatment as
    // RadialExplosion's explosionDirection.
    float orbitSphereSpread = 0.0f;

    float attractionStrength = 1.0f;          // AttractRepelSiblings, negative = repel
};

/**
    Lightweight, pool-managed grain state -- a single spawn-move-fade
    event, not a persistent object. GrainCloud pre-allocates a fixed pool
    and activates/deactivates slots in place; nothing here is ever
    heap-allocated after that (no std::vector members, no pointers).
*/
struct Grain
{
    bool active = false;
    // Set by GrainCloud::beginVoiceSteal() when this grain's slot has been
    // claimed by a pending spawn while the cloud was at
    // GrainCloudSettings::maxConcurrentGrains -- lifetimeSeconds/
    // grainLengthSamples below have already been shortened to fade out
    // quickly. Prevents this same grain from being picked as a steal
    // target a second time while it's already winding down; cleared again
    // in spawnGrain() once this slot is actually reused.
    bool beingStolen = false;
    float age = 0.0f;             // seconds since spawn
    float lifetimeSeconds = 0.0f; // grainDuration at spawn time, +/- jitter if targeted
    // Bumped every time this pool slot is (re)spawned, so the audio thread
    // can detect "this is a new grain, not a continuation" even if the
    // slot never reported inactive in between (see PluginProcessor).
    int spawnGeneration = 0;

    Vec3 position { 0.0f, 0.0f, 0.0f };
    Vec3 velocity { 0.0f, 0.0f, 0.0f };

    // Mode-specific runtime state, only the fields relevant to the cloud's
    // current movementMode are meaningful for a given grain.
    float orbitPhase = 0.0f;                        // OrbitAroundParent
    float currentOrbitRadius = 0.0f;                // OrbitAroundParent, jittered at spawn
    // OrbitAroundParent: normal of the plane this grain circles in, fixed
    // at spawn (see GrainCloudSettings::orbitSphereSpread). {0,0,1}
    // reproduces the original flat x/y-plane orbit exactly.
    Vec3 orbitPlaneNormal { 0.0f, 0.0f, 1.0f };
    Vec3 boundaryCenter { 0.0f, 0.0f, 0.0f };        // Bounce, fixed at spawn position
    float currentBoundaryRadius = 0.0f;             // Bounce, jittered at spawn
    Vec3 explosionDirection { 1.0f, 0.0f, 0.0f };    // RadialExplosion, fixed at spawn
    float currentAcceleration = 0.0f;               // RadialExplosion

    // Audio trigger parameters, captured once at spawn time and used by
    // the audio thread to play back this grain's windowed ring-buffer
    // snippet -- see PluginProcessor.
    int bufferReadStartSample = 0;   // position in the parent's ring buffer at spawn
    float playbackRate = 1.0f;       // pitch ratio (1 +/- pitchJitter deviation)
    int grainLengthSamples = 0;      // grainDuration in samples, fixed at spawn (sample-rate dependent)
};
