#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "ParameterPanel.h"
#include "SlingGesture.h"
#include "Camera3D.h"
#include "ObjectListPanel.h"
#include "SciFiLookAndFeel.h"
#include "HelpWindow.h"
#include <deque>
#include <memory>
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
      scene view's highlight.
    - Toolbar at the top: load/save preset (PresetManager), add/remove
      object, "?" opens a scrollable reference of every feature/parameter
      (see HelpWindow.h/HelpContent.h) in its own OS-level window -- works
      the same whether Klangorbit is the Standalone app or hosted as a
      VST3 with no spare screen space of its own
    - Panel on the right: all parameters of the selected object + scene-wide
      parameters (room boundary, global field, time scale), see ParameterPanel
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
    CPU-load readout, and repaint().
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

    void addObjectClicked();
    void removeObjectClicked();
    void selectObject (int index); // -1 = clear the selection
    void updateObjectUiState();    // object count label + button enablement

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

    static constexpr int toolbarHeight = 76; // two even 38px rows
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

    juce::TextButton addObjectButton { "+ Object" };
    juce::TextButton removeObjectButton { "- Remove Object" };
    juce::Label objectCountLabel;

    // Read from KlangorbitProcessor::getEstimatedCpuLoad() each timer
    // tick -- see its comment for why this exists (maxConcurrentGrainsGlobal
    // was raised to 128 on a rough estimate, not a hardware profile; this
    // lets the user check the actual measured load for themselves).
    juce::Label cpuLoadLabel;

    ParameterPanel parameterPanel;
};
