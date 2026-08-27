#pragma once
#include <juce_core/juce_core.h>
#include "Vec3.h"

/**
    State of a single sound object in space.

    Position in Cartesian coordinates (meters, right-handed):
        x = front/back (front positive)
        y = left/right (left positive)
        z = up/down    (up positive)

    Updated by TrajectoryEngine (control rate, ~60-120 Hz) and read by
    AmbisonicsEncoder (audio rate). Not audio data itself, just metadata --
    the actual signal comes separately via the assigned input channel index
    (inputChannel).
*/
struct SoundObject
{
    int id = -1;

    // Which live input channel (0-based) feeds this object.
    // -1 = no input assigned (object silent / just a placeholder).
    int inputChannel = -1;

    Vec3 position   { 1.0f, 0.0f, 0.0f }; // starting position: 1m in front
    Vec3 velocity   { 0.0f, 0.0f, 0.0f };
    float mass = 1.0f; // for n-body attraction/repulsion

    // Motion mode, evaluated by TrajectoryEngine
    enum class Mode
    {
        Static,       // stays at position (e.g. dragged by mouse)
        Manual,       // currently being moved live via mouse/MIDI/gamepad rate-control, no physics
        Orbit,        // circles around orbitCenter with orbitRadius/orbitSpeed
        Impulse,      // was "thrown", moves freely under velocity + force field
        Attracted     // subject to n-body forces from other objects/points
    };
    Mode mode = Mode::Static;

    // True while a rate-control driver (a gamepad, see GamepadDriver) is
    // actively setting manualVelocity below every tick -- lets
    // TrajectoryEngine::integrate() distinguish gamepad rate-control from
    // a plain mouse drag (TrajectoryEngine::dragTo()), which sets position
    // (and its own rough velocity estimate, for Doppler) directly and
    // must NOT have that overwritten here. Mouse dragging never touches
    // this flag, so it stays false throughout a pure mouse interaction --
    // integrate()'s Manual case is then a complete no-op, exactly as
    // before this field existed. Runtime state, reset on preset load.
    bool manualVelocityActive = false;
    // Manual mode's own per-tick velocity target, continuously set by
    // whatever is currently driving it while manualVelocityActive is true.
    // TrajectoryEngine::integrate()'s Manual case integrates position from
    // this every tick and then holds it exactly at 0 the instant the
    // driver stops setting a nonzero value -- unlike Impulse mode, there
    // is deliberately no damping/momentum here: this IS the "position
    // holds exactly where released, no drift" rate-control behavior, not
    // an approximation of it. Runtime state, not a preset-authored
    // starting parameter -- reset on preset load like orbitPhase/velocity.
    Vec3 manualVelocity { 0.0f, 0.0f, 0.0f };

    // Orbit mode parameters
    Vec3 orbitCenter { 0.0f, 0.0f, 0.0f };
    float orbitRadius = 1.0f;
    float orbitAngularSpeed = 1.0f; // rad/s
    float orbitPhase = 0.0f;        // current angle, advanced over time

    // For attraction/repulsion: strength, negative sign = repulsive.
    // Applies when THIS object acts as a source on others (see also
    // forceExponent/minDistance/maxRange/attractionPulse* below -- all of
    // them are also properties of the source, not of the attracted object).
    float attractionStrength = 0.0f;

    // Friction/damping for Impulse mode, 0 = no damping, 1 = stops instantly.
    // Simple multiplicative decay per simulation step (cheap, but step-rate
    // dependent). For a more physically consistent, velocity-proportional
    // brake, see dragCoefficient. Ignored (along with dragCoefficient)
    // while slingshotTargetId below is active -- see
    // TrajectoryEngine::integrate()'s Impulse case for why.
    float damping = 0.02f;

    float gain = 1.0f; // manual per-object gain, in addition to distance attenuation

    // --- Solo/mute (mixing, not physics) --------------------------------
    // Own mute always wins over solo (see below) -- an object can never be
    // simultaneously "definitely silent" and "definitely audible", so a
    // contradictory muted=true + soloed=true state can't happen in
    // practice. Applies to this object AND all of its GrainCloud's grains
    // -- see PluginProcessor::processBlock().
    bool muted = false;
    // If ANY active object is soloed, every object that is NOT soloed
    // goes silent (regardless of its own `muted`), while every soloed
    // object stays audible -- classic non-exclusive DAW solo, not a
    // single-object radio-button. See PluginProcessor::processBlock().
    bool soloed = false;

    // --- Inertia / motion limits ---------------------------------------
    // <= 0 = unlimited.
    float maxVelocity = 6.0f;
    // Real, velocity-proportional braking force (F = -dragCoefficient * velocity),
    // in addition to damping. 0 = off.
    float dragCoefficient = 0.0f;
    // Elasticity when bouncing off the room boundary (SceneSettings::roomSize,
    // Reflect mode). 0 = the outward-facing velocity component is removed
    // (object at most slides tangentially along the wall), 1 = perfectly
    // elastic bounce.
    float restitution = 0.6f;
    // Velocities below this magnitude are hard-snapped to 0.
    // Without this, a damped object only approaches rest asymptotically
    // (never mathematically comes to a full stop).
    float velocitySnapThreshold = 0.01f;

    // --- n-body refinement (applies when this object acts as a source) -----
    // Exponent in the force law, 2 = classic inverse-square law
    // (default behavior, unchanged from earlier versions).
    float forceExponent = 2.0f;
    // Softening radius, prevents hard force spikes at very small
    // distances (replaces the previous global constant of the same name).
    float minDistance = 0.05f;
    // Cutoff radius beyond which this source no longer exerts any force.
    // <= 0 = unlimited range.
    float maxRange = 0.0f;
    // Periodic modulation of attractionStrength: effective strength =
    // attractionStrength * (1 + attractionPulseDepth * sin(phase)).
    // attractionPulseRate = 0 (default) => no modulation.
    float attractionPulseRate = 0.0f;  // Hz
    float attractionPulseDepth = 0.0f; // 0..1
    float attractionPulsePhase = 0.0f; // runtime state, not a starting parameter

    // --- Orbit extensions ------------------------------------------------
    // Normal vector of the orbit plane, default {0,0,1} = previous
    // behavior (circle/ellipse in the x/y plane).
    Vec3 orbitPlaneNormal { 0.0f, 0.0f, 1.0f };
    // 0 = circular orbit, <1 = ellipse. Simplified approximation (fixed
    // semi-axes orbitRadius/orbitRadius*(1-e), not a focus-based Kepler
    // orbit with variable angular speed) -- deliberately kept simple for
    // the POC.
    float orbitEccentricity = 0.0f;
    // Rotation (radians) of the ellipse's major axis within the orbit
    // plane, around orbitPlaneNormal. Irrelevant when orbitEccentricity is
    // 0 (a circle has no distinguishable axis). Default 0 reproduces the
    // orientation the ellipse formula already used before this field
    // existed (major axis along the plane's default reference direction).
    float orbitOrientation = 0.0f;
    // Radius change per second while in Orbit mode, 0 = stable orbit.
    // Deterministic, one-directional drift -- kept as-is (unchanged
    // behavior) alongside the mean-reverting alternative below; combining
    // both is possible (the drift keeps pulling, the reversion keeps
    // resisting) but not a typical use case.
    float orbitDecay = 0.0f;
    // --- Mean-reverting orbit radius (Ornstein-Uhlenbeck process) --------
    // Alternative to orbitDecay for a "living, breathing" orbit that
    // wanders around a baseline without ever drifting away permanently --
    // unlike orbitDecay, which only ever moves in one direction.
    // 0 for both orbitRadiusReversionRate and orbitRadiusNoiseAmplitude
    // (the default) disables this entirely, so existing behavior/presets
    // are unaffected. See TrajectoryEngine::integrate()'s Orbit case for
    // the actual discrete-time update.
    //
    //   radius(t+dt) = radius(t)
    //                  + reversionRate * (baseline - radius(t)) * dt
    //                  + noiseAmplitude * sqrt(dt) * gaussianRandom()
    //
    // Target value the radius wanders around and keeps returning to.
    float orbitRadiusBaseline = 1.0f;
    // How strongly/quickly the radius is pulled back toward the baseline
    // -- 0 = no pull (a pure, unbounded random walk if noiseAmplitude is
    // also nonzero); higher = a "tighter tether" that snaps back faster
    // and wanders less far before reversion dominates.
    float orbitRadiusReversionRate = 0.0f;
    // Strength of the random perturbation applied per update, in
    // meters/sqrt(second) (standard Wiener-process scaling, hence the
    // sqrt(dt) in the update above -- keeps the noise's statistical
    // properties independent of the actual update rate).
    float orbitRadiusNoiseAmplitude = 0.0f;
    // Time constant (seconds), low-pass-filters the raw per-tick Gaussian
    // noise sample itself (one-pole, same exp(-dt/tau) idiom as
    // PropagationProcessor's dopplerSmoothing) before it's scaled into the
    // radius update below -- turns a jagged white-noise wander into a
    // smoother, more "breathing" one. 0 (default) = no smoothing, i.e. the
    // raw sample is used directly, identical to this field never having
    // existed. See TrajectoryEngine::integrate()'s Orbit case.
    float orbitRadiusNoiseSmoothing = 0.0f;
    // Runtime state for the filter above (not a starting parameter --
    // reset on preset load like orbitPhase/attractionPulsePhase).
    float orbitRadiusNoiseSmoothed = 0.0f;
    // -1 = orbitCenter is a fixed point (previous behavior). Otherwise the
    // id of another SoundObject to orbit around (e.g. moon-around-planet
    // hierarchies).
    int orbitReferenceObjectId = -1;

    // --- Sling gesture: "slingshot" gravity assist ------------------------
    // Runtime state set only by TrajectoryEngine::throwObject() when the
    // sling gesture's Slingshot mode fires (see PluginEditor); not a
    // starting parameter a preset would hand-author, so not serialized by
    // PresetManager (same reasoning as attractionPulsePhase above).
    //
    // -1 = no active pull. >=0 = the object is continuously pulled toward
    // that OTHER object's LIVE position every tick, in Impulse mode, via
    // the same inverse-square force law as the ordinary n-body attraction
    // system (see TrajectoryEngine::computeAttractionForce()) -- but
    // deliberately kept as its own separate mechanism rather than reusing
    // attractionStrength directly, so firing a slingshot never mutates the
    // target object's own, independently-configured Attraction settings.
    // Depending on approach speed/distance/strength, the result emerges
    // naturally from the physics -- a deflected flyby that continues on a
    // new course, or a capture into a bound, looping trajectory -- exactly
    // like a real gravity-assist maneuver, not a scripted outcome.
    int slingshotTargetId = -1;
    // Pull strength for the above, analogous to attractionStrength but
    // private to this one gesture-driven pull. Set once at throw time from
    // SlingGesture::slingshotGravityStrength; 0 whenever slingshotTargetId
    // is -1.
    float slingshotStrength = 0.0f;

    // --- Acoustic propagation (Doppler, directivity) ---------------------
    // On by default. A quick, dial-preserving on/off switch: when false,
    // PropagationProcessor treats dopplerFactor as 0 for this object
    // without touching the stored value itself, so re-enabling restores
    // whatever dopplerFactor was actually dialed in rather than losing it.
    // Mirrors GrainCloudSettings::dopplerEnabled's own existing role for
    // grains (though that one defaults OFF, matching Doppler being an
    // optional add-on for grains specifically, not the object's own
    // default-on Doppler here).
    bool dopplerEnabled = true;
    // See PropagationProcessor for how these are used. 0 = no Doppler
    // pitch shift, 1 = physically correct (given SceneSettings::speedOfSound),
    // >1 = exaggerated. The actual propagation delay (latency) always
    // stays anchored to the true distance regardless of this value -- only
    // the audible pitch-shift component scales with it, so this can't be
    // used to "turn off" the delay itself, only its Doppler side effect
    // (dopplerEnabled above is the actual on/off switch for that).
    float dopplerFactor = 1.0f;
    // Time constant (seconds) smoothing the delay line's rate of change,
    // to avoid pitch/click artifacts on abrupt direction changes (e.g. a
    // bounce off the room boundary).
    float dopplerSmoothing = 0.05f;

    enum class DirectivityPattern { Omni, Cardioid, Figure8 };
    DirectivityPattern directivityPattern = DirectivityPattern::Omni;
    // Direction the object "faces" (world space, listener at origin).
    // Only relevant for Cardioid/Figure8 -- lets an object move and
    // "turn away" independently of each other.
    Vec3 sourceOrientation { 1.0f, 0.0f, 0.0f };
};
