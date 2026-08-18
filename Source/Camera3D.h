#pragma once
#include "Vec3.h"

/**
    Orbit camera around the world origin (azimuth/elevation/distance state),
    with a hand-written perspective projection -- deliberately no OpenGL and
    no juce_graphics dependency (this header only includes Vec3.h), so this
    class stays as lightweight and headlessly testable as SlingGesture.h/
    GrainRenderer.h. PluginEditor converts Projection results to actual
    juce::Point<float> screen coordinates.

    Default state (azimuth=pi, elevation=halfPi) reproduces EXACTLY the
    screen mapping the old fixed 2D top-down view used (world +X/"front" ->
    up on screen, world -Y -> right on screen, see rebuildBasis() for the
    derivation) -- this is what lets the old top-down view be replaced by
    this camera's default state instead of kept as a second rendering path.

    All angles in radians. The camera always looks at the origin -- an
    orbit/arcball camera around a fixed target, not a free-fly camera.
*/
class Camera3D
{
public:
    Camera3D();

    // Interaction (see PluginEditor::mouseDrag()/mouseWheelMove()).
    // deltaElevation is clamped so the camera never flips past straight
    // up/down; deltaAzimuth wraps but is otherwise unrestricted (no
    // singularity at any azimuth, including exactly at the poles -- see
    // rebuildBasis()).
    void rotate (float deltaAzimuth, float deltaElevation);
    void zoom (float deltaDistance);

    float getAzimuth() const   { return azimuth; }
    float getElevation() const { return elevation; }
    float getDistance() const  { return distance; }

    Vec3 getPosition() const { return position; }
    Vec3 getRight() const    { return right; }
    Vec3 getUp() const       { return up; }
    Vec3 getForward() const  { return forward; }

    struct Projection
    {
        float x = 0.0f, y = 0.0f;      // screen-space offset from the viewport's center, pixels
        float cameraSpaceDepth = 0.0f; // distance along the view axis; larger = farther. Only meaningful if visible
        bool visible = false;          // false if worldPos is behind/at the near clip plane
    };

    // viewportHeightPixels sets the focal length (vertical-FOV convention;
    // the same focal length also applies horizontally, so there's no
    // separate/distorted horizontal FOV -- wider windows simply show more
    // horizontally, same as a real camera). The caller adds its own
    // viewport center afterward (see PluginEditor::worldToScreen()).
    Projection project (Vec3 worldPos, float viewportHeightPixels) const;

    // World-space size (meters) -> screen-space size (pixels) at a given
    // camera-space depth, e.g. so a marker of fixed real-world size shrinks
    // with distance like an actual object would.
    float worldSizeToScreenSize (float worldSizeMeters, float cameraSpaceDepth, float viewportHeightPixels) const;

    // Casts a ray from the camera through the given screen-space offset
    // (from the viewport center, pixels -- the inverse of project()'s x/y)
    // and returns where it crosses the world's z=0 ground plane. Used to
    // turn a 2D mouse position into a 3D world position for dragging
    // objects/aiming the sling gesture, now that the view can be rotated
    // (a fixed top-down inverse-projection formula no longer applies).
    // Returns false if the ray is (numerically) parallel to the ground
    // plane or the plane is behind the camera along this ray -- the caller
    // should leave the dragged position where it was in that case rather
    // than teleporting it.
    bool screenToGroundPlane (float screenX, float screenY, float viewportHeightPixels, Vec3& outWorldPos) const;

private:
    void rebuildBasis();
    float focalLengthPixels (float viewportHeightPixels) const;

    float azimuth;
    float elevation;
    float distance;

    // Cached, rebuilt only in rotate()/zoom()/the constructor -- project()
    // runs once per drawn object/grain every repaint (see
    // PluginEditor::paint()), so recomputing the camera's own trig
    // per-object-per-frame would be wasted, easily cacheable work; this way
    // it only happens when the camera actually moves.
    Vec3 position, right, up, forward;

    static constexpr float minDistance = 1.0f;
    // Generous enough to fit a much larger-than-default custom
    // SceneSettings::roomSize (default 5m) comfortably in frame -- at this
    // FOV, fitting a radius-50m room needs a distance of roughly
    // radius / tan(fovY/2) ~= 95m, so 150 leaves real margin rather than
    // capping out right at the edge of "large but plausible" scenes.
    static constexpr float maxDistance = 150.0f;
    static constexpr float fovYDegrees = 55.0f;
    static constexpr float nearClipMeters = 0.1f;
};
