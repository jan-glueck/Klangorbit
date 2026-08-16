#include <cmath>
#include <cstdio>
#include <juce_core/juce_core.h>
#include "../Source/Camera3D.h"

namespace
{
    int g_failures = 0;

    void check (bool condition, const char* description)
    {
        std::printf ("%s  %s\n", condition ? "PASS" : "FAIL", description);
        if (! condition)
            ++g_failures;
    }

    bool approxEqual (float a, float b, float tolerance = 1.0e-3f)
    {
        return std::abs (a - b) <= tolerance;
    }

    bool approxEqual (Vec3 a, Vec3 b, float tolerance = 1.0e-3f)
    {
        return approxEqual (a.x, b.x, tolerance) && approxEqual (a.y, b.y, tolerance) && approxEqual (a.z, b.z, tolerance);
    }
}

int main()
{
    constexpr float viewportHeight = 640.0f;

    // --- Default state reproduces the old 2D top-down mapping exactly ---
    // (world +X/"front" -> up on screen, world -Y -> right on screen; see
    // Camera3D.cpp's rebuildBasis() for the full derivation.)
    {
        Camera3D cam;
        check (approxEqual (cam.getPosition(), { 0.0f, 0.0f, cam.getDistance() }),
               "default camera position is straight above the origin");
        check (approxEqual (cam.getForward(), { 0.0f, 0.0f, -1.0f }),
               "default camera looks straight down");
        check (approxEqual (cam.getRight(), { 0.0f, -1.0f, 0.0f }),
               "default camera's screen-right is world -Y");
        check (approxEqual (cam.getUp(), { 1.0f, 0.0f, 0.0f }),
               "default camera's screen-up is world +X (front)");
    }

    // --- Projection axis directions/signs at the default state ---
    {
        Camera3D cam;
        const auto origin = cam.project ({ 0.0f, 0.0f, 0.0f }, viewportHeight);
        check (origin.visible && approxEqual (origin.x, 0.0f, 0.5f) && approxEqual (origin.y, 0.0f, 0.5f),
               "world origin projects to the screen center");

        const auto front = cam.project ({ 1.0f, 0.0f, 0.0f }, viewportHeight);
        check (front.visible && front.y < 0.0f && approxEqual (front.x, 0.0f, 0.5f),
               "a point in front (+X) projects above screen center (negative y)");

        const auto left = cam.project ({ 0.0f, 1.0f, 0.0f }, viewportHeight);
        check (left.visible && left.x < 0.0f && approxEqual (left.y, 0.0f, 0.5f),
               "a point to the left (+Y) projects left of screen center (negative x)");
    }

    // --- Default state matches the old formula's scale consistently ---
    // (screenOffset == (-worldY, -worldX) * scale, same scale for any point
    // near z=0 -- i.e. it's genuinely the same linear top-down map, not
    // just individually-correct axis directions.)
    {
        Camera3D cam;
        const auto reference = cam.project ({ 1.0f, 0.0f, 0.0f }, viewportHeight); // -> (0, -scale)
        const float scale = -reference.y;

        const auto p = cam.project ({ 2.0f, -1.5f, 0.0f }, viewportHeight);
        const bool matches = p.visible
            && approxEqual (p.x, -(-1.5f) * scale, 0.5f)
            && approxEqual (p.y, -(2.0f) * scale, 0.5f);
        check (matches, "projected offsets stay proportional to (-worldY, -worldX) with a single consistent scale");
    }

    // --- Rotation ---
    {
        // At the pole (elevation=halfPi, the default), azimuth has no
        // effect on the camera's position -- expected spherical-coordinate
        // behavior (all azimuth values collapse to the same point at the
        // pole), not a bug. right/up still rotate with azimuth (checked
        // implicitly by the earlier default-basis test using azimuth=pi).
        Camera3D cam;
        cam.rotate (juce::MathConstants<float>::halfPi, 0.0f);
        check (approxEqual (cam.getPosition(), { 0.0f, 0.0f, cam.getDistance() }, 0.01f),
               "rotating azimuth alone at the pole (elevation=halfPi) leaves the camera position unchanged");
    }
    {
        Camera3D cam;
        cam.rotate (0.0f, -juce::MathConstants<float>::halfPi); // tilt all the way down to the horizon... (elevation halfPi -> 0)
        const auto pos = cam.getPosition();
        check (approxEqual (pos.z, 0.0f, 0.01f) && pos.x * pos.x + pos.y * pos.y > 1.0f,
               "tilting elevation to 0 moves the camera to the horizon (z~0, nonzero horizontal offset)");
    }
    {
        // Elevation clamps rather than flipping past the poles.
        Camera3D cam;
        cam.rotate (0.0f, 10.0f); // way more than halfPi
        check (approxEqual (cam.getElevation(), juce::MathConstants<float>::halfPi, 1.0e-4f),
               "elevation clamps at halfPi instead of flipping past it");
        cam.rotate (0.0f, -20.0f);
        check (approxEqual (cam.getElevation(), -juce::MathConstants<float>::halfPi, 1.0e-4f),
               "elevation clamps at -halfPi instead of flipping past it");
    }

    // --- Zoom ---
    {
        Camera3D cam;
        const float startDistance = cam.getDistance();
        cam.zoom (2.0f);
        check (approxEqual (cam.getDistance(), startDistance + 2.0f), "zoom(+2) increases distance by 2");
        cam.zoom (-1000.0f);
        check (cam.getDistance() > 0.0f && cam.getDistance() < startDistance, "zoom() clamps to a positive minimum distance, doesn't go to/past zero");
    }

    // --- Near-clip visibility ---
    {
        Camera3D cam;
        const auto atCamera = cam.project (cam.getPosition(), viewportHeight);
        check (! atCamera.visible, "a point exactly at the camera position is not visible (near-clip)");

        const auto behind = cam.project (cam.getPosition() + cam.getForward() * -1.0f, viewportHeight);
        check (! behind.visible, "a point behind the camera is not visible");

        const auto inFront = cam.project (cam.getPosition() + cam.getForward() * 1.0f, viewportHeight);
        check (inFront.visible, "a point in front of the camera (along forward) is visible");
    }

    // --- worldSizeToScreenSize: perspective size scaling ---
    {
        Camera3D cam;
        const float sizeNear = cam.worldSizeToScreenSize (0.2f, 2.0f, viewportHeight);
        const float sizeFar  = cam.worldSizeToScreenSize (0.2f, 4.0f, viewportHeight);
        check (sizeNear > sizeFar, "a nearer object (smaller depth) projects to a larger screen size");
        check (approxEqual (sizeNear, sizeFar * 2.0f, 0.5f), "doubling the depth roughly halves the projected size (perspective, not just monotonic)");
    }

    // --- screenToGroundPlane: round-trips with project() on the z=0 plane ---
    {
        Camera3D cam;
        const Vec3 groundPoint { 1.3f, -0.7f, 0.0f };
        const auto proj = cam.project (groundPoint, viewportHeight);

        Vec3 recovered;
        const bool ok = cam.screenToGroundPlane (proj.x, proj.y, viewportHeight, recovered);
        check (ok && proj.visible, "screenToGroundPlane succeeds for a visible ground-plane point");
        check (approxEqual (recovered, groundPoint, 0.01f), "screenToGroundPlane recovers the exact original ground-plane point");
    }
    {
        // After rotating away from top-down, the ground plane is no longer
        // perpendicular to the view -- the round-trip must still hold.
        Camera3D cam;
        cam.rotate (0.4f, -0.6f);
        const Vec3 groundPoint { -2.0f, 1.1f, 0.0f };
        const auto proj = cam.project (groundPoint, viewportHeight);

        Vec3 recovered;
        const bool ok = cam.screenToGroundPlane (proj.x, proj.y, viewportHeight, recovered);
        check (ok && proj.visible, "screenToGroundPlane succeeds after rotating the camera");
        check (approxEqual (recovered, groundPoint, 0.01f), "screenToGroundPlane still recovers the original point after rotation");
    }

    std::printf ("\n%s (%d failures)\n", g_failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}
