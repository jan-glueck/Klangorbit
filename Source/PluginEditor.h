#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "ParameterPanel.h"
#include "SlingGesture.h"
#include "Camera3D.h"
#include "ObjectListPanel.h"
#include "SciFiLookAndFeel.h"
#include "HelpWindow.h"
#include "MappingWindow.h"
#include "OutputWindow.h"
#include <deque>
#include <memory>
#include <vector>

/**
    3D orbit-camera view of the scene (x/y/z, camera orbits around the
    origin -- see Camera3D). The previous fixed top-down 2D view is just
    this camera's default state (looking straight down): there is only one
    rendering path, not a separate 2D/3D mode.

    - Left-click on an object: select it (parameters appear in the panel on the right)
    - Left-click+drag on an object: manual movement (Mode::Manual), on the
      world's ground plane (z=0). Hold Alt while dragging to move along the
      height (Z) axis instead -- X/Y stay fixed, only vertical mouse
      movement counts. See mouseDrag()/screenToGroundWorld().
    - Release with momentum: impulse (Mode::Impulse) -- simple throw gesture
    - Double-click on an object: start/stop an orbit around the origin (demo)
    - Shift+left-click+drag on an object: "sling" launch gesture -- pull the
      object away from its rest position like a catapult; releasing fires it
      in the opposite direction, scaled by how far it was pulled. Tapping
      Ctrl while dragging cycles through three launch modes: Free Throw
      (Mode::Impulse, the default), Orbit Shot (Mode::Orbit, a scripted
      ellipse/circle -- direction derived from the gesture, see
      SlingGesture.h), and Slingshot (Mode::Impulse with a real,
      physics-based gravity pull toward another object -- see
      SoundObject::slingshotTargetId/TrajectoryEngine::computeAttractionForce(),
      NOT a scripted path: whether the result looks like a deflected flyby
      or a captured orbit emerges from the actual physics). Tapping Alt
      cycles discrete orbit-eccentricity steps for Orbit Shot; tapping Tab
      cycles which other object Orbit Shot centers on / Slingshot targets
      (SlingLaunchMode, slingReferenceObjectId,
      SlingGesture::cycleSlingReference()). See
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
      scene view's highlight. Also owns "+ Object"/"- Remove Object" and
      the "Objects: N / M" count (see ObjectListPanel's own class
      comment) -- they act on this list, so they live here rather than
      in the toolbar.
    - Toolbar at the top (a single row -- see toolbarHeight): load/save
      preset (PresetManager) on the left; the CPU meter, "Mappings...",
      "Output...", and "?" on the right. "Mappings..."/"Output..."/"?"
      each open their own OS-level window (MappingWindow/OutputWindow/
      HelpWindow) -- works identically whether Klangorbit is the
      Standalone app or hosted as a VST3 with no spare screen space of
      its own.
    - Panel on the right: scene settings and the selected object's
      parameters, see ParameterPanel -- Output Format/Bass Management/
      Circular Array speaker count live in their own "Output..." window
      instead (see above), not in this panel
    - Grain clouds (see GrainCloud/Grain): small dots in a paler variant of
      their parent object's color, fading out as they age

    The actual physics update (TrajectoryEngine::update()) and every
    GrainCloud::update() run on KlangorbitProcessor's own juce::Timer now,
    NOT this editor's -- see PluginProcessor.h's class comment: moving that
    ownership to the processor means the whole simulation (movement,
    panning, grain spawning) keeps running with this editor closed, not
    just the audio callback. This editor still runs its OWN, separate
    juce::Timer at the same rate, but only for view-side concerns: the
    per-object movement-trail capture (see updateTrails()), the sling
    gesture's Ctrl/Alt modifier polling (not from mouseDrag, so a key press
    registers immediately even if the mouse isn't currently moving), the
    CPU-load readout, gamepad camera control, and repaint().

    Gamepad: see GamepadDriver.h for its full fixed default control scheme
    (left stick movement, D-pad Left/Right/A/B cycle/add/remove selected
    object, Y/Left Shoulder/Right Shoulder held + left stick for Free
    Throw/Orbit Shot/Slingshot -- a gamepad-driven equivalent of this
    editor's own Shift+drag sling gesture above, direction/strength chosen
    by pushing the stick rather than pulling the mouse back). All of that
    runs from KlangorbitProcessor's own timer and keeps working with this
    editor closed, same as gamepad movement always has. Only the right
    stick (camera look) and D-pad Up/Down (camera zoom) are handled here
    instead, in updateGamepadCamera() below -- Camera3D is purely this
    editor's own view state (see its own class comment), so unlike
    everything else above, camera control is meaningless without an editor
    open and has no background equivalent. resyncFromBackgroundObjectChanges()
    below keeps this editor's own selection highlight/object list in sync
    with whatever GamepadDriver's cycle/add/remove buttons just changed.
*/
class KlangorbitEditor : public juce::AudioProcessorEditor,
                               private juce::Timer
{
public:
    explicit KlangorbitEditor (KlangorbitProcessor&);
    ~KlangorbitEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void timerCallback() override;

    void loadPresetClicked();
    void savePresetClicked();
    void showPresetError (const juce::String& title, const juce::String& message);
    void showHelpClicked(); // lazily creates helpWindow, or re-shows/refronts it if already open
    void showMappingClicked(); // lazily creates mappingWindow, or re-shows/refronts it if already open
    void showOutputClicked(); // lazily creates outputWindow, or re-shows/refronts it if already open

    // Wired to helpWindow/mappingWindow/outputWindow's onClosed callback --
    // brings THIS editor's own top-level window back to front once one of
    // those always-on-top auxiliary windows hides itself, see those
    // windows' own onClosed comment (HelpWindow.h) for the reported bug
    // this fixes.
    void bringEditorToFront();

    void addObjectClicked();
    void removeObjectClicked();
    void selectObject (int index); // -1 = clear the selection

    // Polled from timerCallback(): notices when GamepadDriver's own
    // built-in object-management buttons (see GamepadDriver.h) changed the
    // selection or the active-object set in the BACKGROUND (i.e. not via
    // this editor's own addObjectClicked()/removeObjectClicked()/
    // selectObject() calls) and resyncs this editor's display state to
    // match -- object list highlight, parameter panel, count label. Not
    // needed for anything mouse/toolbar-driven, since those already call
    // selectObject()/refresh() themselves at the point of the change.
    void resyncFromBackgroundObjectChanges();

    // Right stick (look-around) + D-pad Up/Down (zoom) camera control -- polled
    // directly from GamepadDriver::getLastState() each tick, entirely
    // separate from GamepadDriver itself (see its class comment for why:
    // Camera3D is editor-only view state, meaningless with no editor open,
    // so this deliberately does NOT live in the background driver the way
    // movement/object-management/throw gestures do).
    void updateGamepadCamera();

    // Sling launch gesture (see class comment).
    void startSling (int objectIndex);
    void updateSlingModifiers(); // polled from timerCallback(), edge-detects Ctrl/Alt
    void releaseSling();
    // Advances slingReferenceObjectId to the next candidate (see its own
    // comment) -- bound to the Tab key while the gesture is active, see
    // keyPressed().
    void cycleSlingReference();

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

    // Declared first (and so destroyed last, per C++ member-destruction
    // order) so it always outlives every child component that might still
    // be looking it up while being torn down. Applied to `this` in the
    // constructor via setLookAndFeel(); see SciFiLookAndFeel's class
    // comment for the overall design.
    SciFiLookAndFeel lookAndFeel;

    // Not "processor" -- that name is already taken (as the base-class
    // reference to juce::AudioProcessor) in AudioProcessorEditor.
    KlangorbitProcessor& audioProcessor;

    Camera3D camera;

    // --- Camera-drag gesture (see class comment) -------------------------
    bool cameraDragActive = false;
    juce::Point<float> lastCameraDragScreenPos;

    // --- Gamepad camera control (see updateGamepadCamera()) --------------
    // Same deadzone/curve-shaping philosophy as GamepadDriver's own
    // movement tuning (AxisShaping.h), applied here independently since
    // this control lives entirely in the editor, not that driver.
    static constexpr float gamepadCameraDeadzone = 0.12f;
    static constexpr float gamepadCameraCurveExponent = 2.0f;
    // Radians/second of azimuth or elevation change at full right-stick
    // deflection -- tuned so a full-deflection hold sweeps a half turn in
    // well under a second, similar in feel to a fast mouse drag.
    static constexpr float gamepadCameraRadiansPerSecond = 2.5f;
    // Meters/second of distance change at full D-pad-held zoom -- Camera3D
    // clamps to [minDistance, maxDistance] itself, so this can't overshoot.
    static constexpr float gamepadCameraZoomMetersPerSecond = 6.0f;

    // See resyncFromBackgroundObjectChanges(). -1 (an impossible real
    // count) so the very first tick after construction always treats the
    // initial state as "changed" and does one harmless refresh -- simpler
    // than special-casing "not yet initialized".
    int lastKnownActiveObjectCount = -1;

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
    // Cycled by Ctrl (edge-detected in updateSlingModifiers()), one full
    // cycle back to FreeThrow after Slingshot -- see the class comment.
    enum class SlingLaunchMode { FreeThrow, OrbitShot, Slingshot };

    bool slingActive = false;
    int slingObjectIndex = -1;
    Vec3 slingAnchorWorldPos;             // object's rest position when the gesture started; never written to the engine while aiming
    juce::Point<float> slingCursorScreenPos;
    SlingLaunchMode slingMode = SlingLaunchMode::FreeThrow;
    int slingEccentricityStepIndex = 0;   // into SlingGesture::orbitEccentricitySteps, cycled by Alt (Orbit Shot only)
    bool slingPrevCtrlDown = false;
    bool slingPrevAltDown = false;
    // -1 = "Center" (the world origin); >=0 = that other active object's
    // id. Meaning depends on slingMode: for OrbitShot, the (possibly
    // moving) point the scripted ellipse is centered on; for Slingshot,
    // which object's real gravity the thrown object is pulled toward (see
    // SlingGesture::cycleSlingReference()'s own comment for the full
    // picture). Cycled with the Tab key while the gesture is active (see
    // cycleSlingReference()/keyPressed()) -- tracked independently of
    // slingMode so a target can be picked before or after Ctrl-cycling
    // into a mode that actually uses it.
    int slingReferenceObjectId = -1;

    // --- Fading movement trails -------------------------------------------
    // One deque of recent world positions per object slot (index-aligned
    // with TrajectoryEngine's fixed object array, sized once in the
    // constructor). Oldest points are at the front, newest at the back.
    std::vector<std::deque<Vec3>> objectTrails;
    int trailCaptureCounter = 0;
    static constexpr int trailCaptureDecimation = 3; // capture every 3rd timer tick
    static constexpr int maxTrailPoints = 8;

    static constexpr float hitRadiusPixels = 16.0f;
    static constexpr float grainHitRadiusPixels = 8.0f;

    // Single row now -- +/- Object, the object count, and the CPU meter
    // all moved out (to ObjectListPanel and this row respectively, see
    // below), so a second row is no longer needed.
    static constexpr int toolbarHeight = 38;
    static constexpr int parameterPanelWidth = 360;
    static constexpr int objectListWidth = 208; // wide enough for "Object N" + its Mute/Solo buttons, see ObjectListPanel
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

    // Opens/reuses helpWindow (see HelpWindow.h) -- a single lazily-created
    // instance, kept alive (just hidden, not destroyed) across closes so
    // reopening is instant. Declared here, not created until first clicked.
    juce::TextButton helpButton { "?" };
    std::unique_ptr<HelpWindow> helpWindow;

    // Same lazily-created, hide-not-destroy pattern as helpWindow above.
    juce::TextButton mappingButton { "Mappings..." };
    std::unique_ptr<MappingWindow> mappingWindow;

    // Output Format/Bass Management/Circular Array speaker count -- moved
    // out of ParameterPanel's own former "Output" category into their own
    // window (see OutputWindow.h), same lazily-created, hide-not-destroy
    // pattern as helpWindow/mappingWindow above.
    juce::TextButton outputButton { "Output..." };
    std::unique_ptr<OutputWindow> outputWindow;

    // Read from KlangorbitProcessor::getEstimatedCpuLoad() each timer
    // tick -- see its comment for why this exists (maxConcurrentGrainsGlobal
    // was raised to 128 on a rough estimate, not a hardware profile; this
    // lets the user check the actual measured load for themselves).
    juce::Label cpuLoadLabel;

    ParameterPanel parameterPanel;
};
