#include "TrajectoryEngine.h"

TrajectoryEngine::TrajectoryEngine (int maxObjects)
{
    objects.resize ((size_t) maxObjects);
    for (int i = 0; i < maxObjects; ++i)
        objects[(size_t) i].id = i;

    snapshotBuffer.resize ((size_t) maxObjects);
}

SoundObject& TrajectoryEngine::getObject (int index)
{
    jassert (juce::isPositiveAndBelow (index, (int) objects.size()));
    return objects[(size_t) index];
}

void TrajectoryEngine::beginDrag (int objectIndex)
{
    getObject (objectIndex).mode = SoundObject::Mode::Manual;
}

void TrajectoryEngine::dragTo (int objectIndex, juce::Vector3D<float> newPosition)
{
    auto& o = getObject (objectIndex);
    o.velocity = (newPosition - o.position); // grobe Geschwindigkeitsschaetzung, wird in update() skaliert
    o.position = newPosition;
}

void TrajectoryEngine::endDrag (int objectIndex)
{
    getObject (objectIndex).mode = SoundObject::Mode::Static;
}

void TrajectoryEngine::throwObject (int objectIndex, juce::Vector3D<float> initialVelocity)
{
    auto& o = getObject (objectIndex);
    o.mode = SoundObject::Mode::Impulse;
    o.velocity = initialVelocity;
}

void TrajectoryEngine::startOrbit (int objectIndex, juce::Vector3D<float> center, float radius, float angularSpeed)
{
    auto& o = getObject (objectIndex);
    o.mode = SoundObject::Mode::Orbit;
    o.orbitCenter = center;
    o.orbitRadius = radius;
    o.orbitAngularSpeed = angularSpeed;
    o.orbitPhase = 0.0f;
}

void TrajectoryEngine::setAttraction (int objectIndex, float strength)
{
    auto& o = getObject (objectIndex);
    o.attractionStrength = strength;
    if (o.mode == SoundObject::Mode::Static)
        o.mode = SoundObject::Mode::Attracted;
}

juce::Vector3D<float> TrajectoryEngine::computeAttractionForce (const SoundObject& obj) const
{
    juce::Vector3D<float> force { 0.0f, 0.0f, 0.0f };

    for (auto& other : objects)
    {
        if (other.id == obj.id) continue;
        if (std::abs (other.attractionStrength) < 1.0e-6f) continue;

        auto diff = other.position - obj.position;
        float dist = diff.length();
        dist = juce::jmax (dist, minDistance);

        // Inverses Quadratgesetz, Vorzeichen von other.attractionStrength
        // bestimmt Anziehung (positiv) oder Abstossung (negativ).
        float magnitude = gravityLikeConstant * other.attractionStrength * other.mass / (dist * dist);
        auto dir = diff / dist;
        force += dir * magnitude;
    }
    return force;
}

void TrajectoryEngine::integrate (SoundObject& obj, double dt)
{
    const float fdt = (float) dt;

    switch (obj.mode)
    {
        case SoundObject::Mode::Static:
        case SoundObject::Mode::Manual:
            // Position wird extern gesetzt (dragTo), hier nichts zu tun.
            break;

        case SoundObject::Mode::Orbit:
        {
            obj.orbitPhase += obj.orbitAngularSpeed * fdt;
            // Kreisbewegung in der x/y-Ebene um orbitCenter; z bleibt konstant
            // relativ zum Zentrum -- fuer geneigte Bahnen spaeter Rotation der
            // Ebene selbst ergaenzen.
            float x = obj.orbitCenter.x + obj.orbitRadius * std::cos (obj.orbitPhase);
            float y = obj.orbitCenter.y + obj.orbitRadius * std::sin (obj.orbitPhase);
            juce::Vector3D<float> newPos { x, y, obj.position.z };
            obj.velocity = (newPos - obj.position) / juce::jmax (fdt, 1.0e-6f);
            obj.position = newPos;
            break;
        }

        case SoundObject::Mode::Impulse:
        case SoundObject::Mode::Attracted:
        {
            auto force = computeAttractionForce (obj);
            // F = m*a -> a = F/m
            auto accel = force / juce::jmax (obj.mass, 1.0e-3f);
            obj.velocity += accel * fdt;
            obj.velocity *= (1.0f - juce::jlimit (0.0f, 1.0f, obj.damping));
            obj.position += obj.velocity * fdt;
            break;
        }
    }
}

void TrajectoryEngine::update (double dtSeconds)
{
    for (auto& obj : objects)
        integrate (obj, dtSeconds);

    // Snapshot fuer Audio-Thread aktualisieren
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
