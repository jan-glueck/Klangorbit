#include "TrajectoryEngine.h"
#include "OrbitMath.h"

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

void TrajectoryEngine::throwObject (int objectIndex, Vec3 initialVelocity)
{
    auto& o = getObject (objectIndex);
    o.mode = SoundObject::Mode::Impulse;
    o.velocity = initialVelocity;
}

void TrajectoryEngine::startOrbit (int objectIndex, Vec3 center, float semiMajorAxis, float angularSpeed,
                                    float eccentricity, float orientation)
{
    auto& o = getObject (objectIndex);
    o.mode = SoundObject::Mode::Orbit;
    o.orbitCenter = center;
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
        case SoundObject::Mode::Manual:
            // Position is set externally (dragTo), nothing to do here.
            break;

        case SoundObject::Mode::Orbit:
        {
            obj.orbitPhase += obj.orbitAngularSpeed * fdt;

            if (obj.orbitDecay != 0.0f)
                obj.orbitRadius = juce::jmax (0.05f, obj.orbitRadius + obj.orbitDecay * fdt);

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
            force -= obj.velocity * obj.dragCoefficient;    // real, velocity-proportional braking force

            auto accel = force / juce::jmax (obj.mass, 1.0e-3f);
            obj.velocity += accel * fdt;
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
