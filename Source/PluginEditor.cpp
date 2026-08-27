#include "PluginEditor.h"
#include "PresetManager.h"
#include "OrbitMath.h"
#include "UiTheme.h"
#include "AxisShaping.h"
#include <algorithm>
#include <cmath>

namespace
{
    // Golden-ratio hue stepping: evenly, maximally spreads hues across any
    // number of objects without needing a fixed-size palette table (works
    // regardless of SAPOC_MAX_LIVE_INPUTS). Consistent per object index --
    // object 0 is always this same hue, not reassigned based on selection
    // order. Deliberately restricted to a blue -> violet -> magenta band
    // rather than the full hue wheel, so every object colour stays within
    // the same "sci-fi HUD" family as the rest of the theme -- this also
    // keeps the whole band clear of both red/amber (Mute/Solo, see
    // UiColours::mute()/solo()) AND UiColours::accent()'s own cyan
    // (~0.47 hue) at the low end, which the selection ring is drawn in
    // (see paint()) -- an object landing on that exact hue would make its
    // own selection ring nearly invisible against its fill.
    juce::Colour objectColour (int index)
    {
        const float frac = std::fmod ((float) index * 0.61803398875f, 1.0f);
        const float hue = 0.58f + frac * (0.95f - 0.58f);
        return juce::Colour::fromHSV (hue, 0.75f, 0.95f, 1.0f);
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

    // Shaded, translucent sphere for the room boundary (SceneSettings::roomSize),
    // replacing a flat wireframe outline. No 3D mesh/lighting model (this
    // project deliberately has no OpenGL, see Camera3D's class comment) --
    // instead a cheap "fake sphere" trick: the boundary is always centered
    // on the world origin, and this camera always looks directly at the
    // origin (see Camera3D's class comment), so the sphere's silhouette is
    // an EXACT circle centered at the viewport center for any camera
    // angle/zoom (Camera3D::projectSphereSilhouetteRadius() -- not an
    // approximation). A radial gradient (transparent center -> semi-opaque
    // rim) reads as a translucent shell without hiding anything inside it.
    // (An earlier version also drew a specular highlight here, lit from a
    // fixed world-space direction via Camera3D::computeSphereHighlight() --
    // removed again, it didn't read well visually; the gradient alone is
    // enough to suggest a sphere.)
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

    // Small fixed-size orientation gizmo, bottom-left of the 3D viewport --
    // three short arms in the world X/Y/Z directions, so a rotated view is
    // still easy to read at a glance. Deliberately NOT drawn via
    // Camera3D::project() (which needs a real world position and applies
    // perspective/distance scaling) -- this only needs the camera's
    // ROTATION, so each world axis direction is projected straight through
    // camera.getRight()/getUp() the same way project() derives its x/y
    // (see Camera3D::project()'s own dot products), then drawn as a fixed
    // pixel-length arm from a fixed screen anchor, independent of zoom or
    // where the camera is actually looking.
    void drawAxisGizmo (juce::Graphics& g, const Camera3D& camera, juce::Rectangle<int> viewport)
    {
        constexpr float armLength = 22.0f;
        constexpr float margin = 34.0f;
        const juce::Point<float> anchor { (float) viewport.getX() + margin, (float) viewport.getBottom() - margin };

        struct Axis { Vec3 direction; juce::Colour colour; const char* label; };
        const Axis axes[] = {
            { { 1.0f, 0.0f, 0.0f }, juce::Colour (0xffef5350), "X" }, // front (this project's own +X convention, see SoundObject.h)
            { { 0.0f, 1.0f, 0.0f }, juce::Colour (0xff66bb6a), "Y" }, // left
            { { 0.0f, 0.0f, 1.0f }, juce::Colour (0xff42a5f5), "Z" }, // up
        };

        g.setColour (UiColours::textSecondary().withAlpha (0.6f));
        g.fillEllipse (anchor.x - 2.0f, anchor.y - 2.0f, 4.0f, 4.0f); // origin dot

        for (auto& axis : axes)
        {
            // Same dot-product projection Camera3D::project() uses for its
            // own x/y (rel.dot(right), -rel.dot(up)) -- just applied to a
            // unit world-space direction instead of a world position
            // relative to the camera, since only rotation matters here.
            const juce::Point<float> screenDir { axis.direction.dot (camera.getRight()), -axis.direction.dot (camera.getUp()) };
            const auto tip = anchor + screenDir * armLength;

            g.setColour (axis.colour);
            g.drawLine ({ anchor, tip }, 2.0f);

            g.setFont (11.0f);
            g.drawText (axis.label, juce::Rectangle<float> (tip.x - 8.0f, tip.y - 8.0f, 16.0f, 16.0f),
                        juce::Justification::centred);
        }
    }
}

KlangorbitEditor::KlangorbitEditor (KlangorbitProcessor& p)
    : juce::AudioProcessorEditor (&p), audioProcessor (p)
{
    setLookAndFeel (&lookAndFeel);

    objectTrails.resize ((size_t) audioProcessor.getTrajectoryEngine().getNumObjects());

    addAndMakeVisible (loadPresetButton);
    addAndMakeVisible (savePresetButton);
    addAndMakeVisible (presetStatusLabel);
    addAndMakeVisible (addObjectButton);
    addAndMakeVisible (removeObjectButton);
    addAndMakeVisible (objectCountLabel);
    addAndMakeVisible (cpuLoadLabel);
    addAndMakeVisible (mappingButton);
    addAndMakeVisible (outputButton);
    addAndMakeVisible (helpButton);
    addAndMakeVisible (objectListPanel);
    addAndMakeVisible (parameterPanel);

    objectListPanel.onObjectSelected = [this] (int index) { selectObject (index); };

    loadPresetButton.onClick = [this] { loadPresetClicked(); };
    savePresetButton.onClick = [this] { savePresetClicked(); };
    addObjectButton.onClick = [this] { addObjectClicked(); };
    removeObjectButton.onClick = [this] { removeObjectClicked(); };
    helpButton.onClick = [this] { showHelpClicked(); };
    helpButton.setTooltip ("Help -- every feature and parameter explained");
    mappingButton.onClick = [this] { showMappingClicked(); };
    mappingButton.setTooltip ("Controller mapping -- Learn mode, current bindings, mapping profiles");
    outputButton.onClick = [this] { showOutputClicked(); };
    outputButton.setTooltip ("Output format, bass management, circular array speaker count");

    presetStatusLabel.setText (currentPresetName, juce::dontSendNotification);
    presetStatusLabel.setColour (juce::Label::textColourId, UiColours::textSecondary());
    presetStatusLabel.setJustificationType (juce::Justification::centredLeft);

    objectCountLabel.setColour (juce::Label::textColourId, UiColours::textSecondary());
    objectCountLabel.setJustificationType (juce::Justification::centredLeft);

    cpuLoadLabel.setColour (juce::Label::textColourId, UiColours::textSecondary());
    cpuLoadLabel.setJustificationType (juce::Justification::centredLeft);

    parameterPanel.setSceneSettings (&audioProcessor.getTrajectoryEngine().getSceneSettings());
    parameterPanel.refreshFromModel(); // show scene defaults (roomSize etc.) right away
    objectListPanel.refresh (audioProcessor.getTrajectoryEngine());
    lastKnownActiveObjectCount = audioProcessor.getTrajectoryEngine().getNumActiveObjects(); // matches the refresh() just above -- avoids a redundant one on the first timer tick
    selectObject (-1);                 // initializes panel enablement + object count label consistently

    setSize (700 + objectListWidth + parameterPanelWidth, 700 + toolbarHeight);
    setWantsKeyboardFocus (true);
    // View-side concerns only now (trails, sling-gesture modifier polling,
    // CPU-load readout, repaint) -- TrajectoryEngine/GrainCloud updates
    // run from KlangorbitProcessor's own timer, see this class's header
    // comment. Same rate as before, so trail capture/repaint smoothness
    // is unchanged.
    startTimerHz (90);
}

KlangorbitEditor::~KlangorbitEditor()
{
    stopTimer();
    setLookAndFeel (nullptr); // detach before lookAndFeel itself is torn down, see its member comment in PluginEditor.h
}

void KlangorbitEditor::timerCallback()
{
    // Polled here (not from mouseDrag) so a Ctrl/Alt press registers
    // immediately even while the mouse isn't currently moving.
    updateSlingModifiers();

    // TrajectoryEngine::update()/GrainCloud::update() moved to
    // KlangorbitProcessor's own timer -- see this class's own comment and
    // PluginProcessor.h for why (keeps the simulation running with this
    // editor closed). This timer only reads the results now, via
    // updateTrails()/repaint() below, same as everything else in this
    // class already reads TrajectoryEngine's live state without owning it.

    // GamepadDriver's own built-in object-management buttons (cycle/add/
    // remove) can change the selection/active-object set from the
    // background -- resync this editor's display state before anything
    // else this tick reads it.
    resyncFromBackgroundObjectChanges();

    // Right stick/D-pad camera control -- see its own comment for why this
    // lives here rather than in GamepadDriver.
    updateGamepadCamera();

    updateTrails();

    // See KlangorbitProcessor::getEstimatedCpuLoad()'s comment for
    // why this exists: maxConcurrentGrainsGlobal (128) is a rough
    // estimate, not a hardware-profiled number, so this surfaces the
    // actual measured load instead of asking the user to trust the
    // estimate. Color-coded as a simple, cheap warning rather than a
    // precise meter -- green/grey under normal load, amber approaching
    // the block deadline, red at or past it (audible dropouts likely).
    const float cpuLoad = audioProcessor.getEstimatedCpuLoad();
    cpuLoadLabel.setText ("CPU: " + juce::String (cpuLoad * 100.0f, 1) + "%", juce::dontSendNotification);
    cpuLoadLabel.setColour (juce::Label::textColourId,
                             cpuLoad >= 1.0f ? UiColours::mute()
                                              : (cpuLoad >= 0.7f ? UiColours::solo() : UiColours::textSecondary()));

    repaint();
}

void KlangorbitEditor::updateTrails()
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

juce::Point<float> KlangorbitEditor::worldToScreen (Vec3 pos) const
{
    const auto proj = camera.project (pos, (float) viewArea.getHeight());
    return viewArea.toFloat().getCentre() + juce::Point<float> (proj.x, proj.y);
}

bool KlangorbitEditor::screenToGroundWorld (juce::Point<float> screenPos, Vec3& outWorldPos) const
{
    const auto centre = viewArea.toFloat().getCentre();
    return camera.screenToGroundPlane (screenPos.x - centre.x, screenPos.y - centre.y,
                                        (float) viewArea.getHeight(), outWorldPos);
}

int KlangorbitEditor::findObjectNear (juce::Point<float> screenPos) const
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

bool KlangorbitEditor::isNearAnyGrain (juce::Point<float> screenPos) const
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

void KlangorbitEditor::paint (juce::Graphics& g)
{
    g.fillAll (UiColours::bgDeep());

    // Toolbar gets its own slightly lighter panel background + a hairline
    // bottom border, so it reads as a distinct header bar rather than
    // floating loose over the (otherwise pitch-black) 3D viewport.
    {
        const juce::Rectangle<int> toolbarArea (0, 0, getWidth(), toolbarHeight);
        g.setColour (UiColours::bgPanel());
        g.fillRect (toolbarArea);
        g.setColour (UiColours::border());
        g.fillRect (toolbarArea.withTop (toolbarArea.getBottom() - 1));
    }

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
                             (float) m, 48, UiColours::gridLine(), 1.0f);

    const auto& sceneSettings = audioProcessor.getTrajectoryEngine().getSceneSettings();
    if (sceneSettings.roomSize > 0.0f && sceneSettings.showRoomBoundary)
        drawShadedBoundarySphere (g, camera, centre, viewportHeight, sceneSettings.roomSize, UiColours::accent());

    {
        const auto originProj = camera.project ({ 0.0f, 0.0f, 0.0f }, viewportHeight);
        if (originProj.visible)
        {
            const auto originScreen = centre + juce::Point<float> (originProj.x, originProj.y);
            g.setColour (UiColours::textPrimary());
            g.drawLine (originScreen.x - 6.0f, originScreen.y, originScreen.x + 6.0f, originScreen.y);
            g.drawLine (originScreen.x, originScreen.y - 6.0f, originScreen.x, originScreen.y + 6.0f);
        }

        const auto frontProj = camera.project ({ 0.7f, 0.0f, 0.0f }, viewportHeight);
        if (frontProj.visible)
        {
            const auto frontScreen = centre + juce::Point<float> (frontProj.x, frontProj.y);
            g.setColour (UiColours::textSecondary());
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
                g.setColour (UiColours::accent().withAlpha (distAlpha));
                g.drawEllipse (screenPos.x - radius * 1.4f, screenPos.y - radius * 1.4f, radius * 2.8f, radius * 2.8f, 2.0f);

                if (isActivelyDragged)
                {
                    // A clearly stronger highlight than plain selection --
                    // an extra, softer outer ring while the object is
                    // actually being manipulated, not just selected.
                    g.setColour (UiColours::accent().withAlpha (0.35f * distAlpha));
                    g.drawEllipse (screenPos.x - radius * 2.0f, screenPos.y - radius * 2.0f, radius * 4.0f, radius * 4.0f, 3.0f);
                }
            }
        }

        g.setColour (UiColours::textSecondary().withAlpha (distAlpha));
        g.drawText (juce::String (i), (int) screenPos.x - 20, (int) (screenPos.y + radius + 2.0f), 40, 16, juce::Justification::centred);
    }

    // --- Sling bow line + cursor-follow marker (UI overlay, always on top) ---
    if (slingActive)
    {
        const auto anchorScreen = worldToScreen (slingAnchorWorldPos);
        const auto slingLineColour = slingMode == SlingLaunchMode::OrbitShot  ? juce::Colours::violet
                                    : slingMode == SlingLaunchMode::Slingshot ? juce::Colours::yellowgreen
                                                                               : UiColours::accent();

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

                if (slingMode == SlingLaunchMode::OrbitShot)
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

                    // Same center resolution as releaseSling() -- if a
                    // target is selected, this reads its CURRENT live
                    // position every repaint, so the preview itself
                    // already tracks a moving target while aiming.
                    const Vec3 previewCenter = (slingReferenceObjectId >= 0)
                                                    ? engine.getObject (slingReferenceObjectId).position
                                                    : Vec3 { 0.0f, 0.0f, 0.0f };

                    constexpr int numSegments = 64;
                    std::vector<Vec3> previewPoints;
                    previewPoints.reserve (numSegments + 1);
                    for (int i = 0; i <= numSegments; ++i)
                    {
                        const float phase = juce::MathConstants<float>::twoPi * (float) i / (float) numSegments;
                        previewPoints.push_back (OrbitMath::computePosition (previewObj, previewCenter, phase));
                    }

                    bool anyVisible = false;
                    auto previewPath = buildProjectedPath (camera, centre, viewportHeight, previewPoints, anyVisible);
                    if (anyVisible)
                        strokeDashedPath (g, previewPath, juce::Colours::violet.withAlpha (0.85f), 1.5f);
                }
                else if (slingMode == SlingLaunchMode::Slingshot)
                {
                    // Same physics a release would actually integrate (see
                    // releaseSling()/TrajectoryEngine::computeAttractionForce()) --
                    // forward-simulated a couple of seconds ahead via
                    // SlingGesture::simulateSlingshotPreview(), the exact
                    // same force law/constant, so the curve shown is what
                    // will actually happen, not an approximation. No
                    // target selected (-1) still previews correctly: 0
                    // strength collapses this to the same straight line
                    // Free Throw draws.
                    const auto& thrown = engine.getObject (slingObjectIndex);
                    const Vec3 targetPos = (slingReferenceObjectId >= 0)
                                                ? engine.getObject (slingReferenceObjectId).position
                                                : Vec3 { 0.0f, 0.0f, 0.0f };
                    const float targetMass = (slingReferenceObjectId >= 0)
                                                  ? engine.getObject (slingReferenceObjectId).mass : 0.0f;
                    const float strength = (slingReferenceObjectId >= 0) ? SlingGesture::slingshotGravityStrength : 0.0f;

                    const auto previewPoints = SlingGesture::simulateSlingshotPreview (
                        slingAnchorWorldPos, launchDirection * SlingGesture::throwVelocityScale,
                        targetPos, targetMass, strength, thrown.mass, TrajectoryEngine::gravityLikeConstant);

                    bool anyVisible = false;
                    auto previewPath = buildProjectedPath (camera, centre, viewportHeight, previewPoints, anyVisible);
                    if (anyVisible)
                        strokeDashedPath (g, previewPath, juce::Colours::yellowgreen.withAlpha (0.85f), 1.5f);
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
                    strokeDashedPath (g, throwPath, UiColours::accent().withAlpha (0.9f), 2.0f);

                    g.setColour (UiColours::accent());
                    g.fillEllipse (endScreen.x - 4.0f, endScreen.y - 4.0f, 8.0f, 8.0f);
                }
            }
        }

        // "Center" = the world origin; otherwise the chosen object's id --
        // see slingReferenceObjectId's comment (Tab cycles this while the
        // gesture is active). Shared by Orbit Shot and Slingshot's labels.
        const auto targetLabel = juce::String ("around ")
            + (slingReferenceObjectId >= 0 ? ("Object " + juce::String (slingReferenceObjectId + 1)) // display 1-based; slingReferenceObjectId itself stays the real 0-based id
                                            : juce::String ("Center"));

        if (slingMode == SlingLaunchMode::OrbitShot)
        {
            const auto shapeLabel = juce::String ("Orbit: ")
                + SlingGesture::orbitEccentricitySteps[(size_t) slingEccentricityStepIndex].label;

            g.setColour (UiColours::textPrimary());
            g.drawText (shapeLabel, (int) slingCursorScreenPos.x - 70, (int) slingCursorScreenPos.y + 12, 140, 16, juce::Justification::centred);
            g.setColour (UiColours::textSecondary());
            g.drawText (targetLabel, (int) slingCursorScreenPos.x - 70, (int) slingCursorScreenPos.y + 28, 140, 14, juce::Justification::centred);
        }
        else if (slingMode == SlingLaunchMode::Slingshot)
        {
            g.setColour (UiColours::textPrimary());
            g.drawText ("Slingshot", (int) slingCursorScreenPos.x - 70, (int) slingCursorScreenPos.y + 12, 140, 16, juce::Justification::centred);
            g.setColour (UiColours::textSecondary());
            g.drawText (slingReferenceObjectId >= 0 ? targetLabel : juce::String ("(no target -- tap Tab)"),
                        (int) slingCursorScreenPos.x - 70, (int) slingCursorScreenPos.y + 28, 140, 14, juce::Justification::centred);
        }
        else
        {
            g.setColour (UiColours::textPrimary());
            g.drawText ("Free Throw", (int) slingCursorScreenPos.x - 60, (int) slingCursorScreenPos.y + 12, 120, 16, juce::Justification::centred);
        }
    }

    // Orientation gizmo, bottom-left of the viewport -- see its own
    // comment. The centred bottom hint text drawn just below doesn't
    // overlap it (that text is horizontally centred, leaving the corners
    // free).
    drawAxisGizmo (g, camera, viewArea);

    // Persistent on-screen reminder of the two modifier-key mouse gestures
    // (orbit, sling launch) -- both are otherwise fully hidden (no button,
    // no menu entry), so without this a first-time user has no way to
    // discover them at all. Drawn every frame at a fixed position rather
    // than e.g. a one-time tooltip, since it's cheap and the gestures are
    // easy to forget.
    {
        // Local copy -- removeFromBottom() mutates its receiver, and
        // viewArea is the editor's own member (set once in resized()), not
        // a value to be shrunk a little more on every single repaint.
        auto viewAreaBottom = viewArea;
        auto hintArea = viewAreaBottom.removeFromBottom (34).reduced (6, 2);
        g.setColour (UiColours::textPrimary().withAlpha (0.55f));
        g.setFont (12.5f);
        g.drawText ("Double-click object: Orbit    |    Shift+Drag object: Sling launch",
                    hintArea.removeFromTop (16), juce::Justification::centred);
        g.setColour (UiColours::textSecondary().withAlpha (0.85f));
        g.setFont (11.0f);
        g.drawText ("(while pulling: tap Ctrl = cycle Free Throw/Orbit/Slingshot, Alt = cycle shape, Tab = target)    |    Drag empty space: rotate view    |    Scroll: zoom",
                    hintArea, juce::Justification::centred);
    }

    g.restoreState();
}

void KlangorbitEditor::resized()
{
    auto bounds = getLocalBounds();
    auto toolbar = bounds.removeFromTop (toolbarHeight).reduced (UiSpacing::m, 0);

    auto row1 = toolbar.removeFromTop (38); // toolbarHeight (76) split into two even 38px rows
    // Right-to-left: "?" in the far corner, Output/Mappings just left of
    // it -- all three open their own OS-level window (see the class
    // comment), grouped together for that reason.
    helpButton.setBounds (row1.removeFromRight (32).reduced (UiSpacing::xs));
    outputButton.setBounds (row1.removeFromRight (110).reduced (UiSpacing::xs));
    mappingButton.setBounds (row1.removeFromRight (130).reduced (UiSpacing::xs));
    loadPresetButton.setBounds (row1.removeFromLeft (140).reduced (UiSpacing::xs));
    row1.removeFromLeft (UiSpacing::s);
    savePresetButton.setBounds (row1.removeFromLeft (140).reduced (UiSpacing::xs));
    row1.removeFromLeft (UiSpacing::m);
    presetStatusLabel.setBounds (row1.reduced (UiSpacing::xs));

    auto row2 = toolbar;
    addObjectButton.setBounds (row2.removeFromLeft (100).reduced (UiSpacing::xs));
    row2.removeFromLeft (UiSpacing::s);
    removeObjectButton.setBounds (row2.removeFromLeft (160).reduced (UiSpacing::xs));
    row2.removeFromLeft (UiSpacing::m);
    objectCountLabel.setBounds (row2.removeFromLeft (120).reduced (UiSpacing::xs));
    row2.removeFromLeft (UiSpacing::m);
    cpuLoadLabel.setBounds (row2.removeFromLeft (140).reduced (UiSpacing::xs));

    parameterPanel.setBounds (bounds.removeFromRight (parameterPanelWidth));
    objectListPanel.setBounds (bounds.removeFromLeft (objectListWidth));
    viewArea = bounds;
}

void KlangorbitEditor::showHelpClicked()
{
    if (helpWindow == nullptr)
        helpWindow = std::make_unique<HelpWindow>(); // constructor already makes it visible

    helpWindow->setVisible (true); // in case a previous close just hid it
    helpWindow->toFront (true);
    // Deferred second attempt -- see showMappingClicked()'s own comment on
    // why a synchronous toFront() right here isn't always enough in every
    // DAW host. SafePointer (not a raw pointer): callAsync()'s callback
    // runs on a LATER message-loop iteration, by which point the user
    // could conceivably have already closed this editor (and with it,
    // helpWindow, which this editor owns) -- SafePointer becomes null
    // automatically in that case instead of leaving a dangling pointer.
    juce::Component::SafePointer<HelpWindow> window (helpWindow.get());
    juce::MessageManager::callAsync ([window] { if (window != nullptr) window->toFront (true); });
}

void KlangorbitEditor::showMappingClicked()
{
    if (mappingWindow == nullptr)
        mappingWindow = std::make_unique<MappingWindow> (audioProcessor.getParameterRegistry(), audioProcessor.getMappingEngine()); // constructor already makes it visible

    mappingWindow->setVisible (true); // in case a previous close just hid it
    mappingWindow->toFront (true);
    // Deferred second attempt: the host's own window can still win the
    // initial ordering race in some DAWs (Reaper, per a user report) even
    // with MappingWindow's own setAlwaysOnTop(true) -- re-asserting
    // toFront() one message-loop iteration later, after the OS has fully
    // realized the window's peer, reliably wins where the synchronous call
    // right above sometimes doesn't. SafePointer, not a raw pointer -- see
    // showHelpClicked()'s identical pattern for why.
    juce::Component::SafePointer<MappingWindow> window (mappingWindow.get());
    juce::MessageManager::callAsync ([window] { if (window != nullptr) window->toFront (true); });
}

void KlangorbitEditor::showOutputClicked()
{
    if (outputWindow == nullptr)
        outputWindow = std::make_unique<OutputWindow> (audioProcessor); // constructor already makes it visible

    outputWindow->setVisible (true); // in case a previous close just hid it
    outputWindow->refreshFromModel(); // pick up any change made elsewhere (e.g. a preset load) since it was last shown
    outputWindow->toFront (true);
    // Deferred second attempt -- see showMappingClicked()'s identical
    // comment for why, and why SafePointer rather than a raw pointer.
    juce::Component::SafePointer<OutputWindow> window (outputWindow.get());
    juce::MessageManager::callAsync ([window] { if (window != nullptr) window->toFront (true); });
}

void KlangorbitEditor::showPresetError (const juce::String& title, const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, title, message);
}

void KlangorbitEditor::loadPresetClicked()
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

void KlangorbitEditor::savePresetClicked()
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

void KlangorbitEditor::addObjectClicked()
{
    auto& engine = audioProcessor.getTrajectoryEngine();
    const int idx = engine.findNextInactiveObject();
    if (idx < 0)
        return; // all objects already active

    engine.activateObject (idx);
    objectListPanel.refresh (engine); // the active-object set just changed
    selectObject (idx); // select it right away, convenient for immediate tweaking
}

void KlangorbitEditor::removeObjectClicked()
{
    if (selectedObjectIndex < 0)
        return;

    audioProcessor.getTrajectoryEngine().deactivateObject (selectedObjectIndex);
    objectListPanel.refresh (audioProcessor.getTrajectoryEngine()); // the active-object set just changed
    selectObject (-1);
}

void KlangorbitEditor::selectObject (int index)
{
    selectedObjectIndex = index;
    // The editor's own copy above is purely for rendering (selection
    // highlight, panel enablement) -- the processor's copy is the actual
    // source of truth ParameterRegistry::Scope::SelectedObject parameters
    // resolve against, and keeps working even without an editor open (see
    // KlangorbitProcessor::getSelectedObjectIndex()'s own comment).
    audioProcessor.setSelectedObjectIndex (index);
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

void KlangorbitEditor::updateObjectUiState()
{
    auto& engine = audioProcessor.getTrajectoryEngine();
    const int active = engine.getNumActiveObjects();
    const int total = engine.getNumObjects();

    objectCountLabel.setText ("Objects: " + juce::String (active) + " / " + juce::String (total), juce::dontSendNotification);
    addObjectButton.setEnabled (active < total);
    removeObjectButton.setEnabled (selectedObjectIndex >= 0);
}

void KlangorbitEditor::resyncFromBackgroundObjectChanges()
{
    auto& engine = audioProcessor.getTrajectoryEngine();

    // The active-object SET (not just which one is selected) can change in
    // the background via GamepadDriver's cycle/add/remove buttons -- only
    // rebuild the (relatively expensive, full-teardown) object list when
    // the count actually changed, not unconditionally every tick at 90Hz.
    const int currentActiveCount = engine.getNumActiveObjects();
    if (currentActiveCount != lastKnownActiveObjectCount)
    {
        objectListPanel.refresh (engine);
        lastKnownActiveObjectCount = currentActiveCount;
    }

    // The processor's own selectedObjectIndex is the one true value (see
    // its own comment in PluginProcessor.h); this editor's copy is a
    // display convenience that can go stale when a background button
    // changes the real one. selectObject() itself round-trips back into
    // audioProcessor.setSelectedObjectIndex() with the same value, which is
    // harmless (same pattern already used by every other selectObject()
    // caller in this file).
    if (audioProcessor.getSelectedObjectIndex() != selectedObjectIndex)
        selectObject (audioProcessor.getSelectedObjectIndex());
    else
        updateObjectUiState(); // keeps the count label/button enablement fresh even when the selection itself didn't change
}

void KlangorbitEditor::updateGamepadCamera()
{
    const auto state = audioProcessor.getGamepadState();
    if (! state.connected)
        return;

    constexpr double dt = 1.0 / 90.0; // matches startTimerHz(90) below -- see its own comment

    // Right stick: look-around (azimuth/elevation). Sign convention
    // (stick right -> orbit the same way dragging right does; stick up ->
    // orbit the same way dragging up does) is a reasonable best guess, NOT
    // yet manually verified against real hardware in this environment --
    // same disclosed caveat as this project's own camera drag/zoom and
    // gamepad-movement sign conventions (see README's "Known limitations"),
    // and just as easy to flip (negate one shapedX/shapedY term) if it
    // feels backwards.
    const float shapedX = shapeAxis (state.rightStickX, gamepadCameraDeadzone, gamepadCameraCurveExponent);
    const float shapedY = shapeAxis (state.rightStickY, gamepadCameraDeadzone, gamepadCameraCurveExponent);
    if (shapedX != 0.0f || shapedY != 0.0f)
    {
        camera.rotate (shapedX * gamepadCameraRadiansPerSecond * (float) dt,
                        shapedY * gamepadCameraRadiansPerSecond * (float) dt);
        repaint();
    }

    // D-pad Up/Down: zoom in/out (multiplicative-feeling since Camera3D::
    // zoom() takes an absolute distance delta and distance is clamped to
    // Camera3D's own [minDistance, maxDistance] range regardless -- a
    // fixed rate feels fine here since, unlike the mouse wheel, this is a
    // continuous hold rather than discrete notches). Left/Right are
    // deliberately unused -- see GamepadDriver.h's own class comment for
    // what the rest of the D-pad/face buttons/shoulders do instead.
    if (state.dpadUp != state.dpadDown) // both held at once cancels out, same as neither
    {
        camera.zoom ((state.dpadUp ? -1.0f : 1.0f) * gamepadCameraZoomMetersPerSecond * (float) dt);
        repaint();
    }
}

void KlangorbitEditor::startSling (int objectIndex)
{
    auto& engine = audioProcessor.getTrajectoryEngine();

    slingActive = true;
    slingObjectIndex = objectIndex;
    slingAnchorWorldPos = engine.getObject (objectIndex).position;
    slingCursorScreenPos = worldToScreen (slingAnchorWorldPos);

    // Always starts on FreeThrow/"Center", even if Ctrl/Alt already happen
    // to be held -- consistent edge-detection philosophy for all three
    // modifier keys (Ctrl/Alt/Tab): only an actual fresh press advances
    // anything (see updateSlingModifiers()/keyPressed()), never merely
    // holding a key down through the start of a new gesture.
    slingMode = SlingLaunchMode::FreeThrow;
    slingEccentricityStepIndex = 0;
    slingReferenceObjectId = -1;
    const auto mods = juce::ModifierKeys::getCurrentModifiers();
    slingPrevCtrlDown = mods.isCtrlDown();
    slingPrevAltDown = mods.isAltDown();
}

void KlangorbitEditor::cycleSlingReference()
{
    if (! slingActive) return;

    auto& engine = audioProcessor.getTrajectoryEngine();
    std::vector<int> activeIds;
    for (int i = 0; i < engine.getNumObjects(); ++i)
        if (engine.getObject (i).inputChannel >= 0)
            activeIds.push_back (i);

    slingReferenceObjectId = SlingGesture::cycleSlingReference (slingReferenceObjectId, activeIds, slingObjectIndex);
}

void KlangorbitEditor::updateSlingModifiers()
{
    if (! slingActive) return;

    const auto mods = juce::ModifierKeys::getCurrentModifiers();
    const bool ctrlDown = mods.isCtrlDown();
    const bool altDown = mods.isAltDown();

    // Edge-detected (only on the down-transition), so holding a key
    // doesn't repeatedly toggle/cycle every timer tick.
    if (ctrlDown && ! slingPrevCtrlDown)
    {
        slingMode = static_cast<SlingLaunchMode> ((static_cast<int> (slingMode) + 1) % 3);

        // Slingshot needs a real target to mean anything ("Center" has no
        // gravity-well interpretation, see cycleSlingReference()'s own
        // comment) -- if the user hasn't already picked one via Tab,
        // auto-select the first available candidate instead of silently
        // doing nothing, so switching into Slingshot always visibly does
        // something if any other object exists to target.
        if (slingMode == SlingLaunchMode::Slingshot && slingReferenceObjectId < 0)
            cycleSlingReference();
    }

    if (altDown && ! slingPrevAltDown)
        slingEccentricityStepIndex = (slingEccentricityStepIndex + 1)
                                      % (int) SlingGesture::orbitEccentricitySteps.size();

    slingPrevCtrlDown = ctrlDown;
    slingPrevAltDown = altDown;
}

void KlangorbitEditor::releaseSling()
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

        switch (slingMode)
        {
            case SlingLaunchMode::OrbitShot:
            {
                // -1 ("Center") uses the world origin, same as before this
                // feature existed; >=0 (orbiting another object, see
                // slingReferenceObjectId's comment) uses that object's
                // CURRENT position as the center at release time -- its
                // live position going forward is then tracked by
                // TrajectoryEngine::integrate() itself via
                // referenceObjectId below, not by this snapshot.
                const Vec3 center = (slingReferenceObjectId >= 0)
                                         ? engine.getObject (slingReferenceObjectId).position
                                         : Vec3 { 0.0f, 0.0f, 0.0f };
                const float semiMajor = juce::jmax (SlingGesture::minOrbitRadiusMeters,
                                                     pullVector.length() * SlingGesture::orbitRadiusScale);
                const float orientation = SlingGesture::computeOrbitOrientation (pullVector);
                const float directionSign = SlingGesture::computeOrbitDirectionSign (slingAnchorWorldPos - center, launchDirection);
                const float eccentricity = SlingGesture::orbitEccentricitySteps[(size_t) slingEccentricityStepIndex].eccentricity;

                engine.startOrbit (slingObjectIndex, center, semiMajor,
                                    directionSign * SlingGesture::orbitAngularSpeedMagnitude,
                                    eccentricity, orientation, slingReferenceObjectId);
                break;
            }

            case SlingLaunchMode::Slingshot:
            {
                // A real, physics-based gravity pull -- NOT a scripted
                // path. -1 ("Center") has no gravity-well meaning, so it
                // degrades gracefully to a plain, unaffected throw (0
                // strength) rather than needing special-casing here; see
                // SlingGesture::cycleSlingReference()'s own comment.
                const float strength = (slingReferenceObjectId >= 0) ? SlingGesture::slingshotGravityStrength : 0.0f;
                engine.throwObject (slingObjectIndex, launchDirection * SlingGesture::throwVelocityScale,
                                     slingReferenceObjectId, strength);
                break;
            }

            case SlingLaunchMode::FreeThrow:
            default:
                engine.throwObject (slingObjectIndex, launchDirection * SlingGesture::throwVelocityScale);
                break;
        }
    }

    slingActive = false;
    slingObjectIndex = -1;
}

void KlangorbitEditor::mouseDown (const juce::MouseEvent& e)
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

void KlangorbitEditor::mouseDrag (const juce::MouseEvent& e)
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

void KlangorbitEditor::mouseUp (const juce::MouseEvent&)
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

void KlangorbitEditor::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    // Multiplicative zoom (scaled by the current distance) feels
    // proportional at any zoom level, unlike a fixed per-notch step, which
    // would feel too fast zoomed in and too slow zoomed out.
    constexpr float zoomSensitivity = 2.5f;
    camera.zoom (-wheel.deltaY * camera.getDistance() * zoomSensitivity);
    repaint();
}

bool KlangorbitEditor::keyPressed (const juce::KeyPress& key)
{
    // Cycles the sling gesture's "slingshot" reference target (see
    // slingReferenceObjectId's comment in PluginEditor.h) -- only
    // meaningful while actively pulling, so this simply doesn't consume
    // Tab otherwise (letting normal keyboard focus traversal, if any,
    // still work the rest of the time).
    if (slingActive && key == juce::KeyPress::tabKey)
    {
        cycleSlingReference();
        repaint();
        return true;
    }
    return false;
}

void KlangorbitEditor::mouseDoubleClick (const juce::MouseEvent& e)
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
