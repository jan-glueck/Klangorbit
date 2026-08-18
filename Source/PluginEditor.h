#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "ParameterPanel.h"
#include "SlingGesture.h"
#include "Camera3D.h"
#include "ObjectListPanel.h"
#include <deque>
#include <vector>

/**
    3D orbit-camera view of the scene (x/y/z, camera orbits around the
    origin -- see Camera3D). The previous fixed top-down 2D view is just
    this camera's default state (looking straight down): there is only one
    rendering path, not a separate 2D/3D mode.

    - Left-click on an object: select it (parameters appear in the panel on the right)
    - Left-click+drag on an object: manual movement (Mode::Manual), on the world's ground plane (z=0)
    - Release with momentum: impulse (Mode::Impulse) -- simple throw gesture
    - Double-click on an object: start/stop an orbit around the origin (demo)
    - Shift+left-click+drag on an object: "sling" launch gesture -- pull the
      object away from its rest position like a catapult; releasing fires it
      in the opposite direction, scaled by how far it was pulled. Holding
      Ctrl while dragging toggles between a free throw (Impulse) and an
      orbit shot (Mode::Orbit, always centered on the origin, direction
      derived from the gesture -- see SlingGesture.h); holding Alt cycles
      through discrete orbit-eccentricity steps (circular/ellipse). See
      startSling()/updateSlingModifiers()/releaseSling() below.
    - Left-click+drag on EMPTY space (no object or grain under the cursor):
      orbits the camera (azimuth/elevation) instead -- object/sling
      gestures always take priority when something is actually hit, camera
      dragging only starts otherwise. See startCameraDrag() below.
    - Mouse wheel: zooms the camera (changes its distance from the origin).
    - Click on empty space (no drag): clear the selection
    - Object list (left side, see ObjectListPanel): click an object's row
      to select it, same as clicking it in the scene view -- useful for
      objects too small, fast, or far away to reliably click directly.
      Selection made either way stays in sync between the list and the
      scene view's highlight.
    - Toolbar at the top: load/save preset (PresetManager), add/remove object
    - Panel on the right: all parameters of the selected object + scene-wide
      parameters (room boundary, global field, time scale), see ParameterPanel
    - Grain clouds (see GrainCloud/Grain): small dots in a paler variant of
      their parent object's color, fading out as they age

    The actual physics update runs on a juce::Timer that calls
    TrajectoryEngine::update() with the measured time since the last tick --
    that's the control-rate loop, separate from the audio thread. Each
    object's GrainCloud is updated from the same timer tick, as is the
    per-object movement-trail capture (see updateTrails()). The sling
    gesture's Ctrl/Alt modifier toggling is also polled from that same timer
    (not from mouseDrag), so a key press registers immediately even if the
    mouse isn't currently moving.
*/
class SpatialAudioPOCEditor : public juce::AudioProcessorEditor,
                               private juce::Timer
{
public:
    explicit SpatialAudioPOCEditor (SpatialAudioPOCProcessor&);
    ~SpatialAudioPOCEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void timerCallback() override;

    void loadPresetClicked();
    void savePresetClicked();
    void showPresetError (const juce::String& title, const juce::String& message);

    void addObjectClicked();
    void removeObjectClicked();
    void selectObject (int index); // -1 = clear the selection
    void updateObjectUiState();    // object count label + button enablement

    // Sling launch gesture (see class comment).
    void startSling (int objectIndex);
    void updateSlingModifiers(); // polled from timerCallback(), edge-detects Ctrl/Alt
    void releaseSling();

    // Per-object fading movement trail (see class comment), polled from
    // timerCallback() at a decimated rate (not every tick -- a few tenths
    // of a second of trail is plenty, and capturing every control-rate
    // tick would just waste memory on redundant nearby points).
    void updateTrails();

    // World <-> screen mapping through the current camera (see Camera3D).
    // worldToScreen() is for simple cases that don't need depth (hit
    // testing, single-point placement); paint() calls camera.project()
    // directly where it needs the depth for sorting/distance scaling.
    juce::Point<float> worldToScreen (Vec3 pos) const;
    // Ground-plane (z=0) raycast for dragging/aiming -- see
    // Camera3D::screenToGroundPlane(). Returns false (and leaves
    // outWorldPos untouched) if the screen position doesn't correspond to
    // a point on the ground plane in front of the camera; callers should
    // then leave whatever they were positioning where it was.
    bool screenToGroundWorld (juce::Point<float> screenPos, Vec3& outWorldPos) const;
    int findObjectNear (juce::Point<float> screenPos) const;
    bool isNearAnyGrain (juce::Point<float> screenPos) const;

    // Not "processor" -- that name is already taken (as the base-class
    // reference to juce::AudioProcessor) in AudioProcessorEditor.
    SpatialAudioPOCProcessor& audioProcessor;

    Camera3D camera;

    // --- Camera-drag gesture (see class comment) -------------------------
    bool cameraDragActive = false;
    juce::Point<float> lastCameraDragScreenPos;

    int draggedObjectIndex = -1;
    int selectedObjectIndex = -1;
    // Only becomes true once movement beyond mouseDown actually happens
    // (see mouseDrag) -- distinguishes a plain selection click from a real
    // drag, so that clicking doesn't stop a running orbit/impulse.
    bool physicsDragActive = false;
    juce::Point<float> lastDragScreenPos;
    juce::int64 lastDragTimeMs = 0;
    Vec3 estimatedDragVelocity;

    // --- Sling launch gesture state -------------------------------------
    bool slingActive = false;
    int slingObjectIndex = -1;
    Vec3 slingAnchorWorldPos;             // object's rest position when the gesture started; never written to the engine while aiming
    juce::Point<float> slingCursorScreenPos;
    bool slingWantsOrbit = false;         // toggled by Ctrl (edge-detected in updateSlingModifiers())
    int slingEccentricityStepIndex = 0;   // into SlingGesture::orbitEccentricitySteps, cycled by Alt
    bool slingPrevCtrlDown = false;
    bool slingPrevAltDown = false;

    // --- Fading movement trails -------------------------------------------
    // One deque of recent world positions per object slot (index-aligned
    // with TrajectoryEngine's fixed object array, sized once in the
    // constructor). Oldest points are at the front, newest at the back.
    std::vector<std::deque<Vec3>> objectTrails;
    int trailCaptureCounter = 0;
    static constexpr int trailCaptureDecimation = 3; // capture every 3rd timer tick
    static constexpr int maxTrailPoints = 8;

    juce::int64 lastTimerMs = 0;
    static constexpr float hitRadiusPixels = 16.0f;
    static constexpr float grainHitRadiusPixels = 8.0f;

    // Message-thread-only RNG for GrainCloud spawn randomization, see
    // timerCallback() -- GrainCloud::update() takes it by reference rather
    // than owning one itself, since the caller (here) also needs to share
    // one global spawn budget across all clouds each tick.
    juce::Random grainRandom;

    static constexpr int toolbarHeight = 64; // two rows of 32px
    static constexpr int parameterPanelWidth = 340;
    static constexpr int objectListWidth = 160;
    juce::Rectangle<int> viewArea;

    // Sidebar list of active objects, clickable to select -- see
    // ObjectListPanel's class comment. Reuses selectObject() (below),
    // exactly the same path a scene-view click already used.
    ObjectListPanel objectListPanel;

    juce::TextButton loadPresetButton { "Load Preset..." };
    juce::TextButton savePresetButton { "Save Preset..." };
    juce::Label presetStatusLabel;
    juce::String currentPresetName { "(no preset loaded)" };
    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::TextButton addObjectButton { "+ Object" };
    juce::TextButton removeObjectButton { "- Remove Object" };
    juce::Label objectCountLabel;

    // Read from SpatialAudioPOCProcessor::getEstimatedCpuLoad() each timer
    // tick -- see its comment for why this exists (maxConcurrentGrainsGlobal
    // was raised to 128 on a rough estimate, not a hardware profile; this
    // lets the user check the actual measured load for themselves).
    juce::Label cpuLoadLabel;

    ParameterPanel parameterPanel;
};
