#include "PluginEditor.h"
#include "PresetManager.h"
#include "OrbitMath.h"
#include <algorithm>
#include <cmath>

namespace
{
    // Golden-ratio hue stepping: evenly, maximally spreads hues across any
    // number of objects without needing a fixed-size palette table (works
    // regardless of SAPOC_MAX_LIVE_INPUTS). Consistent per object index --
    // object 0 is always this same hue, not reassigned based on selection
    // order.
    juce::Colour objectColour (int index)
    {
        const float hue = std::fmod (0.12f + (float) index * 0.61803398875f, 1.0f);
        return juce::Colour::fromHSV (hue, 0.65f, 0.95f, 1.0f);
    }

    juce::Colour paleGrainColour (juce::Colour base)
    {
        return base.interpolatedWith (juce::Colours::white, 0.4f);
    }

    // Objects/grains fade slightly with camera distance for a spatial
    // depth cue, on top of perspective size scaling.
    float distanceFadeAlpha (float cameraSpaceDepth)
    {
        constexpr float nearFade = 3.0f, farFade = 20.0f, minAlpha = 0.4f;
        const float t = juce::jlimit (0.0f, 1.0f, (cameraSpaceDepth - nearFade) / (farFade - nearFade));
        return 1.0f - t * (1.0f - minAlpha);
    }

    // Builds a Path connecting the projected screen positions of
    // worldPoints, breaking into separate subpaths wherever a point goes
    // behind the camera (near-clip) instead of drawing a wrapped mess.
    // Shared by the wireframe grid/sphere and the orbit-path preview so
    // there's one "project a world curve" implementation, not several.
    juce::Path buildProjectedPath (const Camera3D& camera, juce::Point<float> viewportCentre, float viewportHeight,
                                    const std::vector<Vec3>& worldPoints, bool& anyVisible)
    {
        juce::Path path;
        bool first = true;
        anyVisible = false;

        for (auto& wp : worldPoints)
        {
            const auto proj = camera.project (wp, viewportHeight);
            if (! proj.visible)
            {
                first = true; // start a fresh subpath once visibility resumes
                continue;
            }

            anyVisible = true;
            const auto screenPt = viewportCentre + juce::Point<float> (proj.x, proj.y);
            if (first)
            {
                path.startNewSubPath (screenPt);
                first = false;
            }
            else
            {
                path.lineTo (screenPt);
            }
        }

        return path;
    }

    // Dashed stroke, so a movement PREVIEW (sling aiming -- see the
    // paint() sling overlay below) can never be mistaken for the real,
    // already-happened trail (drawTrail(), solid) or a confirmed orbit
    // path (drawOrbitPath(), solid) -- same dash pattern used for both the
    // throw-direction line and the orbit-ellipse preview.
    void strokeDashedPath (juce::Graphics& g, const juce::Path& path, juce::Colour colour, float lineWidth)
    {
        static const float dashLengths[] = { 6.0f, 4.0f };
        juce::Path dashed;
        juce::PathStrokeType (lineWidth).createDashedStroke (dashed, path, dashLengths, 2);
        g.setColour (colour);
        g.fillPath (dashed);
    }

    void drawWireframeCircle (juce::Graphics& g, const Camera3D& camera, juce::Point<float> viewportCentre, float viewportHeight,
                               Vec3 center, Vec3 planeU, Vec3 planeV, float radius, int numSegments,
                               juce::Colour colour, float lineWidth)
    {
        std::vector<Vec3> points;
        points.reserve ((size_t) numSegments + 1);
        for (int i = 0; i <= numSegments; ++i)
        {
            const float t = juce::MathConstants<float>::twoPi * (float) i / (float) numSegments;
            points.push_back (center + planeU * (radius * std::cos (t)) + planeV * (radius * std::sin (t)));
        }

        bool anyVisible = false;
        auto path = buildProjectedPath (camera, viewportCentre, viewportHeight, points, anyVisible);
        if (anyVisible)
        {
            g.setColour (colour);
            g.strokePath (path, juce::PathStrokeType (lineWidth));
        }
    }

    // Fixed WORLD-SPACE direction the boundary sphere's specular highlight
    // is lit from (see drawShadedBoundarySphere below) -- an arbitrary but
    // fixed "key light" choice (mostly from above, slightly front-left),
    // NOT attached to the camera. Rotating the view therefore moves the
    // bright spot across the sphere's surface exactly like a real
    // directional light would, instead of a screen-space decal glued to
    // one corner regardless of orientation.
    const Vec3 boundaryHighlightLightDir = [] {
        const Vec3 v { 0.35f, 0.35f, 0.85f };
        return v / v.length();
    }();

    // Shaded, translucent sphere for the room boundary (SceneSettings::roomSize),
    // replacing a flat wireframe outline. No 3D mesh/lighting model (this
    // project deliberately has no OpenGL, see Camera3D's class comment) --
    // instead a cheap "fake sphere" trick: the boundary is always centered
    // on the world origin, and this camera always looks directly at the
    // origin (see Camera3D's class comment), so the sphere's silhouette is
    // an EXACT circle centered at the viewport center for any camera
    // angle/zoom (Camera3D::projectSphereSilhouetteRadius() -- not an
    // approximation). A radial gradient (transparent center -> semi-opaque
    // rim) reads as a translucent shell without hiding anything inside it,
    // plus a specular highlight for a touch of "shaded sphere" look beyond
    // a flat gradient.
    void drawShadedBoundarySphere (juce::Graphics& g, const Camera3D& camera, juce::Point<float> viewportCentre,
                                    float viewportHeight, float sphereRadius, juce::Colour boundaryColour)
    {
        float screenRadius = 0.0f;
        if (! camera.projectSphereSilhouetteRadius (sphereRadius, viewportHeight, screenRadius))
            return; // camera is at/inside the boundary -- no silhouette exists to draw

        juce::ColourGradient rim (boundaryColour.withAlpha (0.0f), viewportCentre.x, viewportCentre.y,
                                   boundaryColour.withAlpha (0.4f), viewportCentre.x + screenRadius, viewportCentre.y,
                                   true); // radial
        rim.addColour (0.75, boundaryColour.withAlpha (0.08f)); // stays mostly transparent through most of the interior
        g.setGradientFill (rim);
        g.fillEllipse (viewportCentre.x - screenRadius, viewportCentre.y - screenRadius, screenRadius * 2.0f, screenRadius * 2.0f);

        g.setColour (boundaryColour.withAlpha (0.7f));
        g.drawEllipse (viewportCentre.x - screenRadius, viewportCentre.y - screenRadius, screenRadius * 2.0f, screenRadius * 2.0f, 1.5f);

        // Specular highlight -- the point on the sphere's surface nearest
        // the fixed world-space light direction above, projected through
        // the SAME camera used for everything else (Camera3D::
        // computeSphereHighlight()), so it moves exactly as a real lit
        // sphere would when the view rotates, rather than staying glued
        // to a fixed screen offset.
        Camera3D::SphereHighlight highlight;
        if (! camera.computeSphereHighlight (boundaryHighlightLightDir, sphereRadius, viewportHeight, highlight))
            return; // on the far side of the sphere from here, or behind the near clip plane -- nothing to draw

        const float highlightRadius = screenRadius * 0.35f;
        const auto highlightCentre = viewportCentre + juce::Point<float> (highlight.x, highlight.y);
        juce::ColourGradient highlightGradient (juce::Colours::white.withAlpha (0.18f * highlight.intensity), highlightCentre.x, highlightCentre.y,
                                                  juce::Colours::white.withAlpha (0.0f), highlightCentre.x + highlightRadius, highlightCentre.y,
                                                  true);
        g.setGradientFill (highlightGradient);
        g.fillEllipse (highlightCentre.x - highlightRadius, highlightCentre.y - highlightRadius,
                        highlightRadius * 2.0f, highlightRadius * 2.0f);
    }

    // Samples the exact same ellipse formula TrajectoryEngine uses to move
    // an orbiting object (OrbitMath.h) to draw a preview of the whole path,
    // not just the current point -- "shared, not reimplemented" math, same
    // as SlingGesture.h/GrainRenderer.h elsewhere in this codebase.
    void drawOrbitPath (juce::Graphics& g, const Camera3D& camera, juce::Point<float> viewportCentre, float viewportHeight,
                         const SoundObject& obj, Vec3 center, juce::Colour colour)
    {
        constexpr int numSegments = 64;
        std::vector<Vec3> points;
        points.reserve (numSegments + 1);
        for (int i = 0; i <= numSegments; ++i)
        {
            const float phase = juce::MathConstants<float>::twoPi * (float) i / (float) numSegments;
            points.push_back (OrbitMath::computePosition (obj, center, phase));
        }

        bool anyVisible = false;
        auto path = buildProjectedPath (camera, viewportCentre, viewportHeight, points, anyVisible);
        if (anyVisible)
        {
            g.setColour (colour);
            g.strokePath (path, juce::PathStrokeType (1.2f));
        }
    }

    void drawTrail (juce::Graphics& g, const Camera3D& camera, juce::Point<float> viewportCentre, float viewportHeight,
                     const std::deque<Vec3>& trail, juce::Colour colour)
    {
        const int n = (int) trail.size();
        if (n < 2) return;

        for (int i = 1; i < n; ++i)
        {
            const auto p0 = camera.project (trail[(size_t) (i - 1)], viewportHeight);
            const auto p1 = camera.project (trail[(size_t) i], viewportHeight);
            if (! p0.visible || ! p1.visible) continue;

            const float ageFrac = (float) i / (float) n; // 0 = oldest segment, 1 = newest (drawn most opaque)
            g.setColour (colour.withAlpha (0.5f * ageFrac));
            g.drawLine (viewportCentre.x + p0.x, viewportCentre.y + p0.y,
                        viewportCentre.x + p1.x, viewportCentre.y + p1.y, 1.5f);
        }
    }

    struct DrawItem
    {
        bool isGrain = false;
        int parentIndex = -1;      // object slot index (owner, for both kinds)
        Vec3 worldPos;
        float ageFraction = 0.0f;  // grains only
        Camera3D::Projection projection;
    };
}

SpatialAudioPOCEditor::SpatialAudioPOCEditor (SpatialAudioPOCProcessor& p)
    : juce::AudioProcessorEditor (&p), audioProcessor (p)
{
    objectTrails.resize ((size_t) audioProcessor.getTrajectoryEngine().getNumObjects());

    addAndMakeVisible (loadPresetButton);
    addAndMakeVisible (savePresetButton);
    addAndMakeVisible (presetStatusLabel);
    addAndMakeVisible (addObjectButton);
    addAndMakeVisible (removeObjectButton);
    addAndMakeVisible (objectCountLabel);
    addAndMakeVisible (cpuLoadLabel);
    addAndMakeVisible (objectListPanel);
    addAndMakeVisible (parameterPanel);

    objectListPanel.onObjectSelected = [this] (int index) { selectObject (index); };

    loadPresetButton.onClick = [this] { loadPresetClicked(); };
    savePresetButton.onClick = [this] { savePresetClicked(); };
    addObjectButton.onClick = [this] { addObjectClicked(); };
    removeObjectButton.onClick = [this] { removeObjectClicked(); };

    presetStatusLabel.setText (currentPresetName, juce::dontSendNotification);
    presetStatusLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    presetStatusLabel.setJustificationType (juce::Justification::centredLeft);

    objectCountLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    objectCountLabel.setJustificationType (juce::Justification::centredLeft);

    cpuLoadLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    cpuLoadLabel.setJustificationType (juce::Justification::centredLeft);

    parameterPanel.setSceneSettings (&audioProcessor.getTrajectoryEngine().getSceneSettings());
    parameterPanel.refreshFromModel(); // show scene defaults (roomSize etc.) right away
    objectListPanel.refresh (audioProcessor.getTrajectoryEngine());
    selectObject (-1);                 // initializes panel enablement + object count label consistently

    setSize (700 + objectListWidth + parameterPanelWidth, 700 + toolbarHeight);
    setWantsKeyboardFocus (true);
    lastTimerMs = juce::Time::getMillisecondCounter();
    startTimerHz (90); // control rate for the TrajectoryEngine
}

SpatialAudioPOCEditor::~SpatialAudioPOCEditor()
{
    stopTimer();
}

void SpatialAudioPOCEditor::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounter();
    const double dt = juce::jlimit (0.0, 0.1, (double) (now - lastTimerMs) / 1000.0); // clamp against outliers
    lastTimerMs = now;

    // Polled here (not from mouseDrag) so a Ctrl/Alt press registers
    // immediately even while the mouse isn't currently moving.
    updateSlingModifiers();

    auto& engine = audioProcessor.getTrajectoryEngine();
    engine.update (dt);

    // GrainCloud: control-rate update, same loop/rate as TrajectoryEngine
    // above. A single global spawn budget is shared across all clouds so
    // the total number of simultaneously active grains never exceeds
    // SpatialAudioPOCProcessor::maxConcurrentGrainsGlobal, no matter how
    // many objects are granulating at once -- each active grain costs a
    // full Ambisonics encode pass in PluginProcessor::processBlock.
    int globalGrainBudget = SpatialAudioPOCProcessor::maxConcurrentGrainsGlobal;
    for (int i = 0; i < engine.getNumGrainClouds(); ++i)
        globalGrainBudget -= engine.getGrainCloud (i).getNumActiveGrains();

    for (int i = 0; i < engine.getNumGrainClouds(); ++i)
    {
        auto& obj = engine.getObject (i);
        if (obj.inputChannel < 0)
            continue; // no active parent -- freeze this cloud instead of updating it with a meaningless position

        auto& cloud = engine.getGrainCloud (i);
        cloud.setRingBufferContext (audioProcessor.getGrainRingBufferWriteHead (i), audioProcessor.getSampleRate());
        cloud.update (dt, obj.position, obj.velocity, globalGrainBudget, grainRandom);
    }

    updateTrails();

    // See SpatialAudioPOCProcessor::getEstimatedCpuLoad()'s comment for
    // why this exists: maxConcurrentGrainsGlobal (128) is a rough
    // estimate, not a hardware-profiled number, so this surfaces the
    // actual measured load instead of asking the user to trust the
    // estimate. Color-coded as a simple, cheap warning rather than a
    // precise meter -- green/grey under normal load, amber approaching
    // the block deadline, red at or past it (audible dropouts likely).
    const float cpuLoad = audioProcessor.getEstimatedCpuLoad();
    cpuLoadLabel.setText ("CPU: " + juce::String (cpuLoad * 100.0f, 1) + "%", juce::dontSendNotification);
    cpuLoadLabel.setColour (juce::Label::textColourId,
                             cpuLoad >= 1.0f ? juce::Colours::red
                                              : (cpuLoad >= 0.7f ? juce::Colours::orange : juce::Colours::lightgrey));

    repaint();
}

void SpatialAudioPOCEditor::updateTrails()
{
    if (++trailCaptureCounter < trailCaptureDecimation)
        return;
    trailCaptureCounter = 0;

    auto& engine = audioProcessor.getTrajectoryEngine();
    for (int i = 0; i < engine.getNumObjects(); ++i)
    {
        auto& obj = engine.getObject (i);
        auto& trail = objectTrails[(size_t) i];

        if (obj.inputChannel < 0)
        {
            trail.clear(); // don't let a stale trail linger through an inactive period
            continue;
        }

        trail.push_back (obj.position);
        while ((int) trail.size() > maxTrailPoints)
            trail.pop_front();
    }
}

juce::Point<float> SpatialAudioPOCEditor::worldToScreen (Vec3 pos) const
{
    const auto proj = camera.project (pos, (float) viewArea.getHeight());
    return viewArea.toFloat().getCentre() + juce::Point<float> (proj.x, proj.y);
}

bool SpatialAudioPOCEditor::screenToGroundWorld (juce::Point<float> screenPos, Vec3& outWorldPos) const
{
    const auto centre = viewArea.toFloat().getCentre();
    return camera.screenToGroundPlane (screenPos.x - centre.x, screenPos.y - centre.y,
                                        (float) viewArea.getHeight(), outWorldPos);
}

int SpatialAudioPOCEditor::findObjectNear (juce::Point<float> screenPos) const
{
    auto& engine = audioProcessor.getTrajectoryEngine();
    const auto centre = viewArea.toFloat().getCentre();
    const float viewportHeight = (float) viewArea.getHeight();

    for (int i = 0; i < engine.getNumObjects(); ++i)
    {
        auto& obj = engine.getObject (i);
        if (obj.inputChannel < 0) continue; // inactive objects are not clickable/visible

        const auto proj = camera.project (obj.position, viewportHeight);
        if (! proj.visible) continue;

        const auto p = centre + juce::Point<float> (proj.x, proj.y);
        if (p.getDistanceFrom (screenPos) <= hitRadiusPixels)
            return i;
    }
    return -1;
}

bool SpatialAudioPOCEditor::isNearAnyGrain (juce::Point<float> screenPos) const
{
    auto& engine = audioProcessor.getTrajectoryEngine();
    const auto centre = viewArea.toFloat().getCentre();
    const float viewportHeight = (float) viewArea.getHeight();

    for (int i = 0; i < engine.getNumObjects(); ++i)
    {
        std::vector<GrainCloud::Snapshot> snapshot;
        engine.getGrainCloud (i).getSnapshot (snapshot);

        for (auto& gs : snapshot)
        {
            if (! gs.active) continue;

            const auto proj = camera.project (gs.position, viewportHeight);
            if (! proj.visible) continue;

            const auto p = centre + juce::Point<float> (proj.x, proj.y);
            if (p.getDistanceFrom (screenPos) <= grainHitRadiusPixels)
                return true;
        }
    }
    return false;
}

void SpatialAudioPOCEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);

    // The 3D scene must never draw outside viewArea -- see the class
    // comment on the toolbar/panel layout.
    g.saveState();
    g.reduceClipRegion (viewArea);

    const auto viewportF = viewArea.toFloat();
    const auto centre = viewportF.getCentre();
    const float viewportHeight = viewportF.getHeight();

    // --- Background: ground reference grid, room-boundary wireframe sphere, origin/front marker ---
    for (int m = 1; m <= 3; ++m)
        drawWireframeCircle (g, camera, centre, viewportHeight, { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f },
                             (float) m, 48, juce::Colours::darkgrey, 1.0f);

    const auto& sceneSettings = audioProcessor.getTrajectoryEngine().getSceneSettings();
    if (sceneSettings.roomSize > 0.0f && sceneSettings.showRoomBoundary)
        drawShadedBoundarySphere (g, camera, centre, viewportHeight, sceneSettings.roomSize, juce::Colours::darkred);

    {
        const auto originProj = camera.project ({ 0.0f, 0.0f, 0.0f }, viewportHeight);
        if (originProj.visible)
        {
            const auto originScreen = centre + juce::Point<float> (originProj.x, originProj.y);
            g.setColour (juce::Colours::white);
            g.drawLine (originScreen.x - 6.0f, originScreen.y, originScreen.x + 6.0f, originScreen.y);
            g.drawLine (originScreen.x, originScreen.y - 6.0f, originScreen.x, originScreen.y + 6.0f);
        }

        const auto frontProj = camera.project ({ 0.7f, 0.0f, 0.0f }, viewportHeight);
        if (frontProj.visible)
        {
            const auto frontScreen = centre + juce::Point<float> (frontProj.x, frontProj.y);
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.drawText ("Front", (int) frontScreen.x - 25, (int) frontScreen.y - 18, 50, 16, juce::Justification::centred);
        }
    }

    // --- Depth-sorted objects + grains ------------------------------------
    // Small, fixed-bound item count (<= SAPOC_MAX_LIVE_INPUTS objects plus
    // <= maxConcurrentGrainsGlobal grains, currently 8 + 32) -- sorting
    // this fresh every repaint is trivially cheap, no pooling/caching
    // needed (see class comment on the performance requirement).
    auto& engine = audioProcessor.getTrajectoryEngine();
    std::vector<DrawItem> items;
    items.reserve (64);

    for (int i = 0; i < engine.getNumObjects(); ++i)
    {
        auto& obj = engine.getObject (i);
        if (obj.inputChannel < 0) continue;

        DrawItem item;
        item.isGrain = false;
        item.parentIndex = i;
        // While this object is being sling-aimed, its real position stays
        // frozen at the anchor (see startSling()) -- depth-sort it there
        // too, so its hollow marker sorts consistently with everything
        // else instead of using a stale pre-freeze depth.
        item.worldPos = (slingActive && i == slingObjectIndex) ? slingAnchorWorldPos : obj.position;
        item.projection = camera.project (item.worldPos, viewportHeight);
        items.push_back (item);

        std::vector<GrainCloud::Snapshot> grainSnapshot;
        engine.getGrainCloud (i).getSnapshot (grainSnapshot);
        for (auto& gs : grainSnapshot)
        {
            if (! gs.active) continue;

            DrawItem gItem;
            gItem.isGrain = true;
            gItem.parentIndex = i;
            gItem.worldPos = gs.position;
            gItem.ageFraction = gs.ageFraction;
            gItem.projection = camera.project (gs.position, viewportHeight);
            items.push_back (gItem);
        }
    }

    std::sort (items.begin(), items.end(), [] (const DrawItem& a, const DrawItem& b)
    {
        return a.projection.cameraSpaceDepth > b.projection.cameraSpaceDepth; // farthest first, nearest drawn last (on top)
    });

    for (auto& item : items)
    {
        if (! item.projection.visible) continue;

        const auto screenPos = centre + juce::Point<float> (item.projection.x, item.projection.y);
        const float depth = item.projection.cameraSpaceDepth;
        const float distAlpha = distanceFadeAlpha (depth);

        if (item.isGrain)
        {
            const float radius = juce::jmax (1.5f, camera.worldSizeToScreenSize (0.045f, depth, viewportHeight));
            const float alpha = juce::jlimit (0.0f, 1.0f, 1.0f - item.ageFraction) * distAlpha;
            g.setColour (paleGrainColour (objectColour (item.parentIndex)).withAlpha (alpha));
            g.fillEllipse (screenPos.x - radius, screenPos.y - radius, radius * 2.0f, radius * 2.0f);
            continue;
        }

        const int i = item.parentIndex;
        auto& obj = engine.getObject (i);
        const auto baseColour = objectColour (i);

        drawTrail (g, camera, centre, viewportHeight, objectTrails[(size_t) i], baseColour.withAlpha (0.7f));

        if (obj.mode == SoundObject::Mode::Orbit)
        {
            Vec3 orbitCenterWorld = obj.orbitCenter;
            if (juce::isPositiveAndBelow (obj.orbitReferenceObjectId, engine.getNumObjects())
                && obj.orbitReferenceObjectId != obj.id)
                orbitCenterWorld = engine.getObject (obj.orbitReferenceObjectId).position;

            drawOrbitPath (g, camera, centre, viewportHeight, obj, orbitCenterWorld, baseColour.withAlpha (0.35f));
        }

        const float radius = juce::jmax (3.0f, camera.worldSizeToScreenSize (0.12f, depth, viewportHeight));
        const bool isSlingingThis = slingActive && i == slingObjectIndex;

        if (isSlingingThis)
        {
            // Hollow marker at the frozen anchor -- the solid "following
            // the cursor" marker + bow line are drawn once more at the
            // very end (outside this depth-sorted loop, see below): the
            // cursor position isn't a real world position, so it has no
            // meaningful depth to sort by.
            g.setColour (baseColour.withAlpha (distAlpha));
            g.drawEllipse (screenPos.x - radius, screenPos.y - radius, radius * 2.0f, radius * 2.0f, 1.5f);
        }
        else
        {
            const bool isActivelyDragged = (i == draggedObjectIndex && physicsDragActive);
            const auto markerColour = isActivelyDragged ? baseColour.brighter (0.6f) : baseColour;
            g.setColour (markerColour.withAlpha (distAlpha));
            g.fillEllipse (screenPos.x - radius, screenPos.y - radius, radius * 2.0f, radius * 2.0f);

            if (i == selectedObjectIndex)
            {
                g.setColour (juce::Colours::white.withAlpha (distAlpha));
                g.drawEllipse (screenPos.x - radius * 1.4f, screenPos.y - radius * 1.4f, radius * 2.8f, radius * 2.8f, 2.0f);

                if (isActivelyDragged)
                {
                    // A clearly stronger highlight than plain selection --
                    // an extra, softer outer ring while the object is
                    // actually being manipulated, not just selected.
                    g.setColour (juce::Colours::white.withAlpha (0.35f * distAlpha));
                    g.drawEllipse (screenPos.x - radius * 2.0f, screenPos.y - radius * 2.0f, radius * 4.0f, radius * 4.0f, 3.0f);
                }
            }
        }

        g.setColour (juce::Colours::white.withAlpha (distAlpha));
        g.drawText (juce::String (i), (int) screenPos.x - 20, (int) (screenPos.y + radius + 2.0f), 40, 16, juce::Justification::centred);
    }

    // --- Sling bow line + cursor-follow marker (UI overlay, always on top) ---
    if (slingActive)
    {
        const auto anchorScreen = worldToScreen (slingAnchorWorldPos);
        const bool isOrbit = slingWantsOrbit;
        const auto slingLineColour = isOrbit ? juce::Colours::violet : juce::Colours::orange;

        g.setColour (slingLineColour);
        g.drawLine (anchorScreen.x, anchorScreen.y, slingCursorScreenPos.x, slingCursorScreenPos.y, 2.0f);

        g.setColour (objectColour (slingObjectIndex));
        g.fillEllipse (slingCursorScreenPos.x - 8.0f, slingCursorScreenPos.y - 8.0f, 16.0f, 16.0f);

        // Movement preview: what will actually happen on release, as
        // opposed to the bow line above (which shows the pull/aim, i.e.
        // the opposite direction). Dashed (see strokeDashedPath()) so it's
        // never confused with the real trail or a confirmed orbit path.
        // Gated entirely on slingActive -- disappears the instant the
        // gesture ends, nothing lingers.
        Vec3 releasePreviewPoint;
        if (screenToGroundWorld (slingCursorScreenPos, releasePreviewPoint))
        {
            const Vec3 pullVector = SlingGesture::computePullVector (slingAnchorWorldPos, releasePreviewPoint);

            if (pullVector.length() >= SlingGesture::minPullDistanceMeters)
            {
                const Vec3 launchDirection = pullVector; // opposite the pull, i.e. where it will actually go

                if (isOrbit)
                {
                    // Same shape a release would actually produce (see
                    // releaseSling()) -- reuses OrbitMath.h, the exact
                    // formula TrajectoryEngine itself uses to move an
                    // orbiting object, via a scratch object holding only
                    // the orbit-shape fields. orbitPlaneNormal is read
                    // from the real object so a previously tilted orbit
                    // plane previews correctly too.
                    SoundObject previewObj;
                    previewObj.orbitPlaneNormal = engine.getObject (slingObjectIndex).orbitPlaneNormal;
                    previewObj.orbitRadius = juce::jmax (SlingGesture::minOrbitRadiusMeters,
                                                          pullVector.length() * SlingGesture::orbitRadiusScale);
                    previewObj.orbitEccentricity = SlingGesture::orbitEccentricitySteps[(size_t) slingEccentricityStepIndex].eccentricity;
                    previewObj.orbitOrientation = SlingGesture::computeOrbitOrientation (pullVector);

                    constexpr int numSegments = 64;
                    std::vector<Vec3> previewPoints;
                    previewPoints.reserve (numSegments + 1);
                    for (int i = 0; i <= numSegments; ++i)
                    {
                        const float phase = juce::MathConstants<float>::twoPi * (float) i / (float) numSegments;
                        previewPoints.push_back (OrbitMath::computePosition (previewObj, { 0.0f, 0.0f, 0.0f }, phase));
                    }

                    bool anyVisible = false;
                    auto previewPath = buildProjectedPath (camera, centre, viewportHeight, previewPoints, anyVisible);
                    if (anyVisible)
                        strokeDashedPath (g, previewPath, juce::Colours::violet.withAlpha (0.85f), 1.5f);
                }
                else
                {
                    // Simple straight preview in the launch direction --
                    // deliberately not a full trajectory simulation (no
                    // globalField/damping), just a clear visual hint, per
                    // the feature request.
                    const Vec3 previewEndWorld = slingAnchorWorldPos + launchDirection;
                    const auto endScreen = worldToScreen (previewEndWorld);

                    juce::Path throwPath;
                    throwPath.startNewSubPath (anchorScreen);
                    throwPath.lineTo (endScreen);
                    strokeDashedPath (g, throwPath, juce::Colours::orange.withAlpha (0.9f), 2.0f);

                    g.setColour (juce::Colours::orange);
                    g.fillEllipse (endScreen.x - 4.0f, endScreen.y - 4.0f, 8.0f, 8.0f);
                }
            }
        }

        const auto label = isOrbit
            ? juce::String ("Orbit: ") + SlingGesture::orbitEccentricitySteps[(size_t) slingEccentricityStepIndex].label
            : juce::String ("Free Throw");
        g.setColour (juce::Colours::white);
        g.drawText (label, (int) slingCursorScreenPos.x - 60, (int) slingCursorScreenPos.y + 12, 120, 16, juce::Justification::centred);
    }

    g.restoreState();
}

void SpatialAudioPOCEditor::resized()
{
    auto bounds = getLocalBounds();
    auto toolbar = bounds.removeFromTop (toolbarHeight);

    auto row1 = toolbar.removeFromTop (32);
    loadPresetButton.setBounds (row1.removeFromLeft (140).reduced (4));
    savePresetButton.setBounds (row1.removeFromLeft (140).reduced (4));
    presetStatusLabel.setBounds (row1.reduced (4));

    auto row2 = toolbar;
    addObjectButton.setBounds (row2.removeFromLeft (100).reduced (4));
    removeObjectButton.setBounds (row2.removeFromLeft (160).reduced (4));
    objectCountLabel.setBounds (row2.removeFromLeft (120).reduced (4));
    cpuLoadLabel.setBounds (row2.removeFromLeft (140).reduced (4));

    parameterPanel.setBounds (bounds.removeFromRight (parameterPanelWidth));
    objectListPanel.setBounds (bounds.removeFromLeft (objectListWidth));
    viewArea = bounds;
}

void SpatialAudioPOCEditor::showPresetError (const juce::String& title, const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, title, message);
}

void SpatialAudioPOCEditor::loadPresetClicked()
{
    juce::File startDir (juce::File::getCurrentWorkingDirectory());
#if defined (SAPOC_PRESETS_USER_DIR)
    if (juce::File (SAPOC_PRESETS_USER_DIR).isDirectory())
        startDir = juce::File (SAPOC_PRESETS_USER_DIR);
#endif

    fileChooser = std::make_unique<juce::FileChooser> ("Load Preset", startDir, "*.json");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (! file.existsAsFile())
                return;

            juce::String loadedName;
            const auto result = PresetManager::loadFile (file, audioProcessor.getTrajectoryEngine(), &loadedName);

            if (result.failed())
            {
                showPresetError ("Could not load preset", result.getErrorMessage());
                return;
            }

            currentPresetName = loadedName;
            presetStatusLabel.setText ("Loaded: " + currentPresetName, juce::dontSendNotification);

            // The scene was replaced entirely -- the old selection/panel
            // values no longer make sense, and the active-object set
            // itself may have changed (a preset can activate/deactivate
            // any number of objects at once).
            objectListPanel.refresh (audioProcessor.getTrajectoryEngine());
            selectObject (-1);
            parameterPanel.refreshFromModel();
        });
}

void SpatialAudioPOCEditor::savePresetClicked()
{
    juce::File startDir (juce::File::getCurrentWorkingDirectory());
#if defined (SAPOC_PRESETS_USER_DIR)
    startDir = juce::File (SAPOC_PRESETS_USER_DIR);
    if (! startDir.isDirectory())
        startDir.createDirectory();
#endif

    fileChooser = std::make_unique<juce::FileChooser> ("Save Preset",
                                                        startDir.getChildFile (currentPresetName + ".json"),
                                                        "*.json");

    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                               | juce::FileBrowserComponent::warnAboutOverwriting,
        [this] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File())
                return;

            if (! file.hasFileExtension ("json"))
                file = file.withFileExtension ("json");

            const auto name = file.getFileNameWithoutExtension();
            const auto result = PresetManager::saveFile (file, audioProcessor.getTrajectoryEngine(), name);

            if (result.failed())
            {
                showPresetError ("Could not save preset", result.getErrorMessage());
                return;
            }

            currentPresetName = name;
            presetStatusLabel.setText ("Saved: " + currentPresetName, juce::dontSendNotification);
        });
}

void SpatialAudioPOCEditor::addObjectClicked()
{
    auto& engine = audioProcessor.getTrajectoryEngine();
    const int idx = engine.findNextInactiveObject();
    if (idx < 0)
        return; // all objects already active

    engine.activateObject (idx);
    objectListPanel.refresh (engine); // the active-object set just changed
    selectObject (idx); // select it right away, convenient for immediate tweaking
}

void SpatialAudioPOCEditor::removeObjectClicked()
{
    if (selectedObjectIndex < 0)
        return;

    audioProcessor.getTrajectoryEngine().deactivateObject (selectedObjectIndex);
    objectListPanel.refresh (audioProcessor.getTrajectoryEngine()); // the active-object set just changed
    selectObject (-1);
}

void SpatialAudioPOCEditor::selectObject (int index)
{
    selectedObjectIndex = index;
    auto& engine = audioProcessor.getTrajectoryEngine();

    if (index < 0)
    {
        parameterPanel.setEditedObject (nullptr, -1, engine.getNumObjects());
        parameterPanel.setEditedGrainCloud (nullptr);
    }
    else
    {
        parameterPanel.setEditedObject (&engine.getObject (index), index, engine.getNumObjects());
        parameterPanel.setEditedGrainCloud (&engine.getGrainCloud (index).getSettings());
    }

    // Reflects the selection into the list's own highlight regardless of
    // which side triggered it (scene-view click, list click, +/- Object
    // buttons, or a preset load clearing the selection) -- see
    // ObjectListPanel's class comment.
    objectListPanel.setSelectedIndex (index);

    updateObjectUiState();
}

void SpatialAudioPOCEditor::updateObjectUiState()
{
    auto& engine = audioProcessor.getTrajectoryEngine();
    const int active = engine.getNumActiveObjects();
    const int total = engine.getNumObjects();

    objectCountLabel.setText ("Objects: " + juce::String (active) + " / " + juce::String (total), juce::dontSendNotification);
    addObjectButton.setEnabled (active < total);
    removeObjectButton.setEnabled (selectedObjectIndex >= 0);
}

void SpatialAudioPOCEditor::startSling (int objectIndex)
{
    auto& engine = audioProcessor.getTrajectoryEngine();

    slingActive = true;
    slingObjectIndex = objectIndex;
    slingAnchorWorldPos = engine.getObject (objectIndex).position;
    slingCursorScreenPos = worldToScreen (slingAnchorWorldPos);

    // If Ctrl/Alt already happen to be held when the gesture starts, take
    // that as the starting state directly (rather than requiring a
    // press-transition first) -- but don't auto-cycle the eccentricity
    // step just because Alt happens to already be down; that only
    // advances on an actual press (see updateSlingModifiers()).
    const auto mods = juce::ModifierKeys::getCurrentModifiers();
    slingWantsOrbit = mods.isCtrlDown();
    slingEccentricityStepIndex = 0;
    slingPrevCtrlDown = mods.isCtrlDown();
    slingPrevAltDown = mods.isAltDown();
}

void SpatialAudioPOCEditor::updateSlingModifiers()
{
    if (! slingActive) return;

    const auto mods = juce::ModifierKeys::getCurrentModifiers();
    const bool ctrlDown = mods.isCtrlDown();
    const bool altDown = mods.isAltDown();

    // Edge-detected (only on the down-transition), so holding a key
    // doesn't repeatedly toggle/cycle every timer tick.
    if (ctrlDown && ! slingPrevCtrlDown)
        slingWantsOrbit = ! slingWantsOrbit;

    if (altDown && ! slingPrevAltDown)
        slingEccentricityStepIndex = (slingEccentricityStepIndex + 1)
                                      % (int) SlingGesture::orbitEccentricitySteps.size();

    slingPrevCtrlDown = ctrlDown;
    slingPrevAltDown = altDown;
}

void SpatialAudioPOCEditor::releaseSling()
{
    auto& engine = audioProcessor.getTrajectoryEngine();

    // If the ground-plane raycast fails (camera looking near-parallel to
    // the ground), fall back to the anchor itself -- that yields a
    // zero-length pull vector, which is below minPullDistanceMeters, so no
    // shot fires. Safer than teleporting the object to an undefined point.
    Vec3 releasePoint = slingAnchorWorldPos;
    screenToGroundWorld (slingCursorScreenPos, releasePoint);

    const Vec3 pullVector = SlingGesture::computePullVector (slingAnchorWorldPos, releasePoint);

    if (pullVector.length() >= SlingGesture::minPullDistanceMeters)
    {
        const Vec3 launchDirection = pullVector; // already points opposite the drag, i.e. the launch direction

        if (slingWantsOrbit)
        {
            const Vec3 center { 0.0f, 0.0f, 0.0f }; // always the origin, see design decision in the feature discussion
            const float semiMajor = juce::jmax (SlingGesture::minOrbitRadiusMeters,
                                                 pullVector.length() * SlingGesture::orbitRadiusScale);
            const float orientation = SlingGesture::computeOrbitOrientation (pullVector);
            const float directionSign = SlingGesture::computeOrbitDirectionSign (slingAnchorWorldPos - center, launchDirection);
            const float eccentricity = SlingGesture::orbitEccentricitySteps[(size_t) slingEccentricityStepIndex].eccentricity;

            engine.startOrbit (slingObjectIndex, center, semiMajor,
                                directionSign * SlingGesture::orbitAngularSpeedMagnitude,
                                eccentricity, orientation);
        }
        else
        {
            engine.throwObject (slingObjectIndex, launchDirection * SlingGesture::throwVelocityScale);
        }
    }

    slingActive = false;
    slingObjectIndex = -1;
}

void SpatialAudioPOCEditor::mouseDown (const juce::MouseEvent& e)
{
    const int hit = findObjectNear (e.position);

    // Shift+click on an object starts the sling gesture instead of the
    // ordinary free drag -- deliberately a modifier key, not just "drag
    // starts a sling", so the two remain clearly distinguishable (see
    // class comment).
    if (hit >= 0 && e.mods.isShiftDown())
    {
        selectObject (hit);
        startSling (hit);
        return;
    }

    if (hit >= 0)
    {
        draggedObjectIndex = hit;
        selectObject (draggedObjectIndex);
        lastDragScreenPos = e.position;
        lastDragTimeMs = juce::Time::getMillisecondCounter();
        estimatedDragVelocity = { 0.0f, 0.0f, 0.0f };
        return;
    }

    // Nothing hit -- clear the selection like before. Only start orbiting
    // the camera if a grain isn't sitting right under the cursor either
    // (object/grain hits always take priority over camera dragging, see
    // class comment).
    selectObject (-1);
    if (! isNearAnyGrain (e.position))
    {
        cameraDragActive = true;
        lastCameraDragScreenPos = e.position;
    }
}

void SpatialAudioPOCEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (slingActive)
    {
        // The real object position is never touched while aiming (see
        // startSling()) -- only this cursor position, which paint() uses
        // to draw the object "following" the cursor and the bow line.
        slingCursorScreenPos = e.position;
        repaint();
        return;
    }

    if (cameraDragActive)
    {
        const auto delta = e.position - lastCameraDragScreenPos;
        // Pixel-to-radian sensitivity tuned so a full-viewport-height drag
        // is roughly a half turn -- feels proportional regardless of
        // window size. Sign convention: dragging right/up rotates the
        // camera to follow the cursor, the usual "grab and spin" feel.
        const float sensitivity = juce::MathConstants<float>::pi / juce::jmax (200.0f, (float) viewArea.getHeight());
        camera.rotate (delta.x * sensitivity, -delta.y * sensitivity);
        lastCameraDragScreenPos = e.position;
        repaint();
        return;
    }

    if (draggedObjectIndex < 0) return;

    if (! physicsDragActive)
    {
        // Only switch into the manual motion mode once there's actually
        // some movement -- a plain click (for selecting) should NOT
        // interrupt a running orbit/impulse.
        if (e.position.getDistanceFrom (lastDragScreenPos) < 3.0f)
            return;

        physicsDragActive = true;
        audioProcessor.getTrajectoryEngine().beginDrag (draggedObjectIndex);
    }

    const auto now = juce::Time::getMillisecondCounter();
    const double dt = juce::jmax (0.001, (double) (now - lastDragTimeMs) / 1000.0);

    Vec3 newPos;
    if (screenToGroundWorld (e.position, newPos))
    {
        auto& engine = audioProcessor.getTrajectoryEngine();
        auto oldPos = engine.getObject (draggedObjectIndex).position;

        estimatedDragVelocity = (newPos - oldPos) / (float) dt;
        engine.dragTo (draggedObjectIndex, newPos);
    }
    // If the raycast fails (camera looking near-parallel to the ground),
    // simply leave the object where it was this frame rather than
    // teleporting it -- see screenToGroundWorld().

    lastDragScreenPos = e.position;
    lastDragTimeMs = now;
}

void SpatialAudioPOCEditor::mouseUp (const juce::MouseEvent&)
{
    if (slingActive)
    {
        releaseSling();
        return;
    }

    if (cameraDragActive)
    {
        cameraDragActive = false;
        return;
    }

    if (draggedObjectIndex < 0) return;

    if (physicsDragActive)
    {
        auto& engine = audioProcessor.getTrajectoryEngine();

        // If there's still noticeable momentum on release: interpret it as a throw.
        if (estimatedDragVelocity.length() > 0.3f)
            engine.throwObject (draggedObjectIndex, estimatedDragVelocity);
        else
            engine.endDrag (draggedObjectIndex);
    }
    // Plain click without movement (physicsDragActive == false): leave the
    // object's mode unchanged, see mouseDrag() -- only the selection
    // (already set in mouseDown) counts.

    draggedObjectIndex = -1;
    physicsDragActive = false;
}

void SpatialAudioPOCEditor::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    // Multiplicative zoom (scaled by the current distance) feels
    // proportional at any zoom level, unlike a fixed per-notch step, which
    // would feel too fast zoomed in and too slow zoomed out.
    constexpr float zoomSensitivity = 2.5f;
    camera.zoom (-wheel.deltaY * camera.getDistance() * zoomSensitivity);
    repaint();
}

void SpatialAudioPOCEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int idx = findObjectNear (e.position);
    if (idx < 0) return;

    auto& engine = audioProcessor.getTrajectoryEngine();
    auto& obj = engine.getObject (idx);

    if (obj.mode == SoundObject::Mode::Orbit)
    {
        engine.endDrag (idx); // -> Static
    }
    else
    {
        const float radius = obj.position.length();
        engine.startOrbit (idx, { 0.0f, 0.0f, 0.0f }, juce::jmax (radius, 0.5f), 1.0f);
    }
}
