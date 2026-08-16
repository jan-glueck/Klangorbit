#include "Camera3D.h"
#include <algorithm>
#include <cmath>

namespace
{
    constexpr float pi = 3.14159265358979323846f;
    constexpr float halfPi = pi * 0.5f;
    constexpr float twoPi = pi * 2.0f;
}

Camera3D::Camera3D()
    : azimuth (pi), elevation (halfPi), distance (6.5f)
{
    rebuildBasis();
}

void Camera3D::rotate (float deltaAzimuth, float deltaElevation)
{
    azimuth = std::fmod (azimuth + deltaAzimuth, twoPi);
    if (azimuth < 0.0f)
        azimuth += twoPi;

    elevation = std::min (std::max (elevation + deltaElevation, -halfPi), halfPi);

    rebuildBasis();
}

void Camera3D::zoom (float deltaDistance)
{
    distance = std::min (std::max (distance + deltaDistance, minDistance), maxDistance);
    rebuildBasis(); // position depends on distance too
}

void Camera3D::rebuildBasis()
{
    const float cosEl = std::cos (elevation);
    const float sinEl = std::sin (elevation);
    const float cosAz = std::cos (azimuth);
    const float sinAz = std::sin (azimuth);

    // Standard spherical coordinates: azimuth rotates around world Z (up),
    // elevation tilts from the horizon toward straight up/down.
    position = Vec3 { cosEl * cosAz, cosEl * sinAz, sinEl } * distance;

    // forward = normalize(target - position) = normalize(-position).
    // position's length is always exactly `distance` by construction
    // above, so this can skip the actual normalize (divide by distance).
    forward = Vec3 { -cosEl * cosAz, -cosEl * sinAz, -sinEl };

    // "right" as the analytic tangent of the orbit sphere along azimuth --
    // always unit length and always exactly perpendicular to `forward`
    // (verified algebraically: right . forward == 0 for any az/el), unlike
    // the usual cross(forward, worldUp) construction, which degenerates to
    // the zero vector exactly when forward is parallel to worldUp -- i.e.
    // exactly at elevation = +-halfPi, which is this camera's own DEFAULT
    // state (straight down), so that degenerate case isn't an edge case
    // here, it's the common case and must be solid.
    right = Vec3 { -sinAz, cosAz, 0.0f };

    // "up" completes the orthonormal basis. At the default state
    // (azimuth=pi, elevation=halfPi): position=(0,0,distance),
    // forward=(0,0,-1), right=(0,-1,0), up=(1,0,0) -- i.e. world +X
    // ("front") is "up" on screen and world -Y is "right" on screen,
    // exactly matching the old fixed 2D top-down mapping
    // (PluginEditor's previous objectToScreen()). See Camera3D.h.
    up = cross (right, forward);
}

float Camera3D::focalLengthPixels (float viewportHeightPixels) const
{
    constexpr float degToRad = pi / 180.0f;
    return (viewportHeightPixels * 0.5f) / std::tan (fovYDegrees * degToRad * 0.5f);
}

Camera3D::Projection Camera3D::project (Vec3 worldPos, float viewportHeightPixels) const
{
    const Vec3 rel = worldPos - position;
    const float depth = rel.dot (forward);

    Projection result;
    result.cameraSpaceDepth = depth;
    result.visible = depth > nearClipMeters;
    if (! result.visible)
        return result;

    const float focalLength = focalLengthPixels (viewportHeightPixels);
    result.x =  rel.dot (right) * focalLength / depth;
    result.y = -rel.dot (up)    * focalLength / depth; // screen Y grows downward, "up" should move things toward smaller Y
    return result;
}

float Camera3D::worldSizeToScreenSize (float worldSizeMeters, float cameraSpaceDepth, float viewportHeightPixels) const
{
    return worldSizeMeters * focalLengthPixels (viewportHeightPixels) / std::max (cameraSpaceDepth, nearClipMeters);
}

bool Camera3D::screenToGroundPlane (float screenX, float screenY, float viewportHeightPixels, Vec3& outWorldPos) const
{
    // Inverse of project(): a point at camera-space depth d projects to
    // (right.(worldPos-pos)*f/d, -up.(worldPos-pos)*f/d), so a ray through
    // this screen offset has direction right*(x/f) + up*(-y/f) + forward
    // (using d=1 as an arbitrary reference -- only the direction matters
    // for a ray-plane intersection, not its length).
    const float focalLength = focalLengthPixels (viewportHeightPixels);
    const Vec3 rayDir = right * (screenX / focalLength) + up * (-screenY / focalLength) + forward;

    if (std::abs (rayDir.z) < 1.0e-6f)
        return false; // ray parallel to the ground plane, no unique intersection

    const float t = -position.z / rayDir.z;
    if (t <= 0.0f)
        return false; // the ground plane is behind the camera along this ray

    outWorldPos = position + rayDir * t;
    return true;
}
