#include "TrajectoryEngine.h"
#include "OrbitMath.h"
#include <cmath>

namespace
{
    constexpr float kMinGaussianInput = 1.0e-6f; // avoids log(0) in the Box-Muller transform below
}

float TrajectoryEngine::nextGaussian()
{
    // Box-Muller transform: two independent uniform(0,1) samples ->
    // one standard-normal sample. Only one of the two values the
    // transform produces is used per call -- simple over maximally
    // efficient, and this is control-rate (~90 Hz per object), not an
    // audio-rate hot path.
    const float u1 = juce::jmax (kMinGaussianInput, orbitNoiseRandom.nextFloat());
    const float u2 = orbitNoiseRandom.nextFloat();
    return std::sqrt (-2.0f * std::log (u1)) * std::cos (juce::MathConstants<float>::twoPi * u2);
}

TrajectoryEngine::TrajectoryEngine (int maxObjects, int grainPoolSizePerCloud)
{
    objects.resize ((size_t) maxObjects);
    for (int i = 0; i < maxObjects; ++i)
        objects[(size_t) i].id = i;

    snapshotBuffer.resize ((size_t) maxObjects);

    grainClouds.reserve ((size_t) maxObjects);
    for (int i = 0; i < maxObjects; ++i)
        grainClouds.push_back (std::make_unique<GrainCloud> (grainPoolSizePerCloud));
}

SoundObject& TrajectoryEngine::getObject (int index)
{
    jassert (juce::isPositiveAndBelow (index, (int) objects.size()));
    return objects[(size_t) index];
}

bool TrajectoryEngine::activateObject (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) objects.size()))
        return false;

    auto& obj = objects[(size_t) index];
    if (obj.inputChannel >= 0)
        return false; // already active

    obj.inputChannel = index;
    // Place it on the reference circle, so newly added objects don't all
    // land on top of each other at the origin.
    const float angle = juce::MathConstants<float>::twoPi * (float) index / (float) objects.size();
    obj.position = { std::cos (angle), std::sin (angle), 0.0f };
    return true;
}

bool TrajectoryEngine::deactivateObject (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) objects.size()))
        return false;

    auto& obj = objects[(size_t) index];
    if (obj.inputChannel < 0)
        return false; // already inactive

    const int id = obj.id;
    obj = SoundObject {};
    obj.id = id;
    grainClouds[(size_t) index]->reset(); // don't let a later reactivation resume with stale settings/grains
    return true;
}

int TrajectoryEngine::getNumActiveObjects() const
{
    int n = 0;
    for (auto& o : objects)
        if (o.inputChannel >= 0)
            ++n;
    return n;
}

int TrajectoryEngine::findNextInactiveObject() const
{
    for (int i = 0; i < (int) objects.size(); ++i)
        if (objects[(size_t) i].inputChannel < 0)
            return i;
    return -1;
}

void TrajectoryEngine::beginDrag (int objectIndex)
{
    getObject (objectIndex).mode = SoundObject::Mode::Manual;
}

void TrajectoryEngine::dragTo (int objectIndex, Vec3 newPosition)
{
    auto& o = getObject (objectIndex);
    o.velocity = (newPosition - o.position); // rough velocity estimate, scaled in update()
    o.position = newPosition;
}

void TrajectoryEngine::endDrag (int objectIndex)
{
    getObject (objectIndex).mode = SoundObject::Mode::Static;
}

void TrajectoryEngine::throwObject (int objectIndex, Vec3 initialVelocity, int slingshotTargetId, float slingshotStrength)
{
    auto& o = getObject (objectIndex);
    o.mode = SoundObject::Mode::Impulse;
    o.velocity = initialVelocity;
    // See this method's own header comment: -1 (the default) always wins
    // and clears any leftover pull from a previous throw.
    o.slingshotTargetId = (slingshotTargetId >= 0 && slingshotTargetId != o.id) ? slingshotTargetId : -1;
    o.slingshotStrength = (o.slingshotTargetId >= 0) ? slingshotStrength : 0.0f;
}

void TrajectoryEngine::startOrbit (int objectIndex, Vec3 center, float semiMajorAxis, float angularSpeed,
                                    float eccentricity, float orientation, int referenceObjectId)
{
    auto& o = getObject (objectIndex);
    o.mode = SoundObject::Mode::Orbit;
    o.orbitCenter = center;
    // referenceObjectId == -1 (the default, and the double-click orbit
    // gesture's only-ever value) reproduces the previous unconditional
    // behavior exactly: any leftover orbitReferenceObjectId from earlier
    // ParameterPanel editing is reset, so `center` above actually takes
    // effect instead of being silently overridden by integrate() (which
    // prefers a valid reference object's live position over orbitCenter).
    // >=0 (currently only the sling gesture's "slingshot" mode) does the
    // opposite on purpose: target that object's live, possibly-moving
    // position every tick instead of the fixed `center` point.
    o.orbitReferenceObjectId = (referenceObjectId >= 0 && referenceObjectId != o.id) ? referenceObjectId : -1;
    o.orbitRadius = semiMajorAxis;
    o.orbitAngularSpeed = angularSpeed;
    o.orbitEccentricity = eccentricity;
    o.orbitOrientation = orientation;
    o.orbitPhase = 0.0f;
}

void TrajectoryEngine::setAttraction (int objectIndex, float strength)
{
    auto& o = getObject (objectIndex);
    o.attractionStrength = strength;
    if (o.mode == SoundObject::Mode::Static)
        o.mode = SoundObject::Mode::Attracted;
}

Vec3 TrajectoryEngine::computeAttractionForce (const SoundObject& obj) const
{
    Vec3 force { 0.0f, 0.0f, 0.0f };

    for (auto& other : objects)
    {
        if (other.id == obj.id) continue;
        if (std::abs (other.attractionStrength) < 1.0e-6f) continue;

        auto diff = other.position - obj.position;
        float dist = diff.length();

        if (other.maxRange > 0.0f && dist > other.maxRange)
            continue;

        dist = juce::jmax (dist, other.minDistance);

        // Periodic modulation of the source strength (attractionPulseRate == 0 => no change).
        const float effectiveStrength = other.attractionStrength
            * (1.0f + other.attractionPulseDepth * std::sin (other.attractionPulsePhase));

        // Generalized force law: forceExponent == 2 => classic
        // inverse-square law (previous behavior).
        const float magnitude = gravityLikeConstant * effectiveStrength * other.mass
            / std::pow (dist, other.forceExponent);
        auto dir = diff / dist;
        force += dir * magnitude;
    }

    // Sling gesture's one-off "slingshot" gravity assist (see
    // SoundObject::slingshotTargetId/slingshotStrength's own comment) --
    // same inverse-square law and gravityLikeConstant as the loop above,
    // fixed forceExponent (2) and a fixed small softening floor rather
    // than exposing them for this one gesture-driven pull -- deliberately
    // NOT folded into the loop above: this pull's source is obj's own
    // slingshotTargetId, not another object's attractionStrength, so it
    // isn't "another object acting as a source" in the same sense, and
    // keeping it separate means firing a slingshot never touches the
    // target object's own, independently-configured Attraction settings.
    if (juce::isPositiveAndBelow (obj.slingshotTargetId, (int) objects.size()) && obj.slingshotTargetId != obj.id)
    {
        const auto& target = objects[(size_t) obj.slingshotTargetId];
        const auto diff = target.position - obj.position;
        // Bigger than SoundObject::minDistance's default (0.05) on purpose:
        // this pull has no damping to bleed off a fast, close pass (see
        // integrate()'s Impulse case below), so at the fixed control-rate
        // timestep a too-small softening floor lets the force spike hard
        // enough in a single step to overshoot past the target and bounce
        // back and forth ("jitter") instead of swinging smoothly by it.
        constexpr float softening = 0.3f; // keep in sync with SlingGesture::simulateSlingshotPreview()
        const float dist = juce::jmax (diff.length(), softening);
        const float magnitude = gravityLikeConstant * obj.slingshotStrength * target.mass / (dist * dist);
        force += (diff / dist) * magnitude;
    }

    return force;
}

void TrajectoryEngine::applyBoundary (SoundObject& obj)
{
    // While the user is actively dragging the object with the mouse, don't
    // clamp -- that would feel like resistance/jitter against the mouse.
    if (obj.mode == SoundObject::Mode::Manual)
        return;
    if (sceneSettings.roomSize <= 0.0f)
        return;

    const float dist = obj.position.length();
    if (dist <= sceneSettings.roomSize)
        return;

    const auto normal = obj.position / juce::jmax (dist, 1.0e-6f);

    switch (sceneSettings.boundaryBehavior)
    {
        case SceneSettings::BoundaryBehavior::Reflect:
        {
            obj.position = normal * sceneSettings.roomSize;

            const float vDotN = obj.velocity.dot (normal);
            if (vDotN > 0.0f) // only reflect if the object is actually moving outward
                obj.velocity -= normal * (vDotN * (1.0f + obj.restitution));
            break;
        }

        case SceneSettings::BoundaryBehavior::Wrap:
            obj.position = normal * -sceneSettings.roomSize; // opposite side
            break;

        case SceneSettings::BoundaryBehavior::Absorb:
            obj.position = normal * sceneSettings.roomSize;
            obj.velocity = {};
            obj.mode = SoundObject::Mode::Static;
            obj.gain = 0.0f; // silences it immediately -- a soft fade would be a later enhancement
            break;
    }
}

void TrajectoryEngine::integrate (SoundObject& obj, double dt)
{
    const float fdt = (float) dt;

    switch (obj.mode)
    {
        case SoundObject::Mode::Static:
            // Position is set externally (dragTo), nothing to do here.
            break;

        case SoundObject::Mode::Manual:
            // Two different external drivers share this one mode: mouse
            // dragging (TrajectoryEngine::dragTo()) sets obj.position (and
            // its own rough velocity estimate) directly every call and
            // leaves manualVelocityActive false; a gamepad's rate-control
            // movement (see GamepadDriver) sets manualVelocityActive true
            // for as long as it's driving this object and continuously
            // updates manualVelocity, letting THIS integration step move
            // it instead -- deliberately simple Euler integration with no
            // damping/momentum of its own, see SoundObject::
            // manualVelocity's own comment for why that's the actual
            // intended rate-control behavior, not a simplification of it.
            if (obj.manualVelocityActive)
            {
                obj.position += obj.manualVelocity * fdt;
                obj.velocity = obj.manualVelocity; // so Doppler/velocity-dependent effects react correctly, same reasoning as Orbit/Impulse below
            }
            // else: position (and velocity, for Doppler) set externally by
            // a mouse drag -- nothing to do here, unchanged prior behavior.
            break;

        case SoundObject::Mode::Orbit:
        {
            obj.orbitPhase += obj.orbitAngularSpeed * fdt;

            if (obj.orbitDecay != 0.0f)
                obj.orbitRadius = juce::jmax (0.05f, obj.orbitRadius + obj.orbitDecay * fdt);

            // Ornstein-Uhlenbeck mean-reverting radius: wanders around
            // orbitRadiusBaseline instead of drifting away permanently
            // like orbitDecay. Both default to 0 (disabled), so this is a
            // no-op unless explicitly configured. See SoundObject.h for
            // the field docs and the exact update formula.
            if (obj.orbitRadiusReversionRate != 0.0f || obj.orbitRadiusNoiseAmplitude != 0.0f)
            {
                const float reversion = obj.orbitRadiusReversionRate * (obj.orbitRadiusBaseline - obj.orbitRadius) * fdt;

                // Low-pass the raw Gaussian sample itself (one-pole,
                // exp(-dt/tau)) before scaling it into the radius update --
                // see SoundObject::orbitRadiusNoiseSmoothing's own comment.
                // smoothing<=0 => coeff 0 => smoothed == raw every tick,
                // exactly reproducing the pre-existing unsmoothed behavior.
                const float rawNoise = nextGaussian();
                const float smoothingCoeff = obj.orbitRadiusNoiseSmoothing > 0.0f
                    ? std::exp (-fdt / obj.orbitRadiusNoiseSmoothing) : 0.0f;
                obj.orbitRadiusNoiseSmoothed = smoothingCoeff * obj.orbitRadiusNoiseSmoothed
                                                + (1.0f - smoothingCoeff) * rawNoise;

                const float noise = obj.orbitRadiusNoiseAmplitude * std::sqrt (fdt) * obj.orbitRadiusNoiseSmoothed;
                obj.orbitRadius = juce::jlimit (0.05f, 1000.0f, obj.orbitRadius + reversion + noise);
            }

            Vec3 center = obj.orbitCenter;
            if (juce::isPositiveAndBelow (obj.orbitReferenceObjectId, (int) objects.size())
                && obj.orbitReferenceObjectId != obj.id)
                center = objects[(size_t) obj.orbitReferenceObjectId].position;

            // Ellipse formula factored out to OrbitMath.h so PluginEditor
            // can sample the same curve for an orbit-path preview instead
            // of reimplementing it -- see OrbitMath.h.
            const Vec3 newPos = OrbitMath::computePosition (obj, center, obj.orbitPhase);

            obj.velocity = (newPos - obj.position) / juce::jmax (fdt, 1.0e-6f);
            obj.position = newPos;
            break;
        }

        case SoundObject::Mode::Impulse:
        case SoundObject::Mode::Attracted:
        {
            auto force = computeAttractionForce (obj);
            force += sceneSettings.globalField * obj.mass; // globalField is force/mass, like gravity

            // While an active slingshot pull (see SoundObject::slingshotTargetId)
            // is deflecting this object, treat it as real, frictionless
            // space flight -- a genuine gravity-assist maneuver conserves
            // (approximately) its own energy. The object's own damping/
            // dragCoefficient are tuned for ordinary decelerating throws in
            // the scene and, applied here too, would bleed off the throw's
            // own momentum within a fraction of a second while the pull
            // (itself undamped) keeps re-accelerating it toward the
            // target -- so instead of swinging past or settling into a
            // smooth captured orbit, it dives almost straight in and then
            // hovers/jitters right at the target once its momentum is gone.
            const bool underSlingshotPull = obj.slingshotTargetId >= 0;
            if (! underSlingshotPull)
                force -= obj.velocity * obj.dragCoefficient; // real, velocity-proportional braking force

            auto accel = force / juce::jmax (obj.mass, 1.0e-3f);
            obj.velocity += accel * fdt;
            if (! underSlingshotPull)
                obj.velocity *= (1.0f - juce::jlimit (0.0f, 1.0f, obj.damping)); // existing simple extra decay

            if (obj.maxVelocity > 0.0f && obj.velocity.length() > obj.maxVelocity)
                obj.velocity = obj.velocity * (obj.maxVelocity / obj.velocity.length());

            if (obj.velocity.length() < obj.velocitySnapThreshold)
                obj.velocity = {};

            obj.position += obj.velocity * fdt;
            break;
        }
    }

    applyBoundary (obj);
}

void TrajectoryEngine::update (double dtSeconds)
{
    const double scaledDt = dtSeconds * (double) sceneSettings.timeScale;

    // The pulse phase for attraction modulation keeps running regardless of
    // the object's own mode -- it acts as a source for others after all.
    for (auto& obj : objects)
        if (obj.attractionPulseRate != 0.0f)
            obj.attractionPulsePhase += juce::MathConstants<float>::twoPi * obj.attractionPulseRate * (float) scaledDt;

    for (auto& obj : objects)
        integrate (obj, scaledDt);

    // Update the snapshot for the audio thread
    juce::ScopedLock lock (snapshotLock);
    for (size_t i = 0; i < objects.size(); ++i)
    {
        snapshotBuffer[i].position = objects[i].position;
        snapshotBuffer[i].velocity = objects[i].velocity;
        snapshotBuffer[i].active   = objects[i].inputChannel >= 0;
    }
}

void TrajectoryEngine::getSnapshot (std::vector<Snapshot>& out) const
{
    juce::ScopedLock lock (snapshotLock);
    out = snapshotBuffer;
}
