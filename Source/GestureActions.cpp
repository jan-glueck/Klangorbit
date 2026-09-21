#include "GestureActions.h"
#include <vector>

namespace GestureActions
{
    void fire (TrajectoryEngine& engine, LaunchMode mode, int objectIndex, Vec3 pullVector)
    {
        switch (mode)
        {
            case LaunchMode::FreeThrow:
                engine.throwObject (objectIndex, pullVector * SlingGesture::throwVelocityScale);
                break;

            case LaunchMode::OrbitShot:
            {
                // Always centers on the world origin and always circular --
                // no Tab/Alt-cycle equivalent for a gamepad or a hand, a
                // disclosed scope decision, not an oversight (both remain
                // available via the mouse gesture).
                const Vec3 center { 0.0f, 0.0f, 0.0f };
                const float semiMajor = juce::jmax (SlingGesture::minOrbitRadiusMeters, pullVector.length() * SlingGesture::orbitRadiusScale);
                const float orientation = SlingGesture::computeOrbitOrientation (pullVector);
                const Vec3 anchorPos = engine.getObject (objectIndex).position; // rest position -- never moved while aiming
                const float directionSign = SlingGesture::computeOrbitDirectionSign (anchorPos - center, pullVector);
                engine.startOrbit (objectIndex, center, semiMajor, directionSign * SlingGesture::orbitAngularSpeedMagnitude, 0.0f, orientation, -1);
                break;
            }

            case LaunchMode::Slingshot:
            {
                // Auto-targets the first other active object -- same
                // fallback the mouse gesture's own updateSlingModifiers()
                // uses (SlingGesture::cycleSlingReference()).
                std::vector<int> activeIds;
                for (int i = 0; i < engine.getNumObjects(); ++i)
                    if (engine.getObject (i).inputChannel >= 0)
                        activeIds.push_back (i);
                const int targetId = SlingGesture::cycleSlingReference (-1, activeIds, objectIndex);
                const float strength = (targetId >= 0) ? SlingGesture::slingshotGravityStrength : 0.0f;
                engine.throwObject (objectIndex, pullVector * SlingGesture::throwVelocityScale, targetId, strength);
                break;
            }
        }
    }

    void fireOrbit (TrajectoryEngine& engine, int objectIndex, float radiusMeters, float directionSign)
    {
        const Vec3 center { 0.0f, 0.0f, 0.0f };
        const float semiMajor = juce::jmax (SlingGesture::minOrbitRadiusMeters, radiusMeters * SlingGesture::orbitRadiusScale);
        const float sign = directionSign >= 0.0f ? 1.0f : -1.0f;
        engine.startOrbit (objectIndex, center, semiMajor, sign * SlingGesture::orbitAngularSpeedMagnitude, 0.0f, 0.0f, -1);
    }
}
