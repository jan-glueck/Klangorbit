#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "ParameterPanel.h"
#include "SlingGesture.h"

/**
    2D top-down view (x/y plane from above, z not displayed -- extension to
    a real 3D view is planned later, same data backing via TrajectoryEngine).

    - Left-click on an object: select it (parameters appear in the panel on the right)
    - Left-click+drag on an object: manual movement (Mode::Manual)
    - Release with momentum: impulse (Mode::Impulse) -- simple throw gesture
    - Double-click on an object: start/stop an orbit around the origin (demo)
    - Shift+left-click+drag on an object: "sling" launch gesture -- pull the
      object away from its rest position like a catapult; releasing fires it
      in the opposite direction, scaled by how far it was pulled. Holding
      Ctrl while dragging toggles between a free throw (Impulse) and an
      orbit shot (Mode::Orbit, always centered on the origin, direction
      derived from the gesture -- see SlingGesture.h); holding Alt cycles
      through discrete orbit-eccentricity steps (circular/ellipse). See
      startSling()/updateSling()/releaseSling() below.
    - Click on empty space: clear the selection
    - Toolbar at the top: load/save preset (PresetManager), add/remove object
    - Panel on the right: all parameters of the selected object + scene-wide
      parameters (room boundary, global field, time scale), see ParameterPanel

    The actual physics update runs on a juce::Timer that calls
    TrajectoryEngine::update() with the measured time since the last tick --
    that's the control-rate loop, separate from the audio thread. The sling
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

    // Screen <-> world coordinates (metric x/y plane, 1m = pixelsPerMeter),
    // relative to viewArea (window minus the toolbar strip at the top and
    // the panel on the right).
    juce::Point<float> objectToScreen (Vec3 pos) const;
    Vec3 screenToObject (juce::Point<float> screenPos) const;
    int findObjectNear (juce::Point<float> screenPos) const;

    // Not "processor" -- that name is already taken (as the base-class
    // reference to juce::AudioProcessor) in AudioProcessorEditor.
    SpatialAudioPOCProcessor& audioProcessor;

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

    juce::int64 lastTimerMs = 0;
    static constexpr float pixelsPerMeter = 80.0f;
    static constexpr float hitRadiusPixels = 16.0f;

    static constexpr int toolbarHeight = 64; // two rows of 32px
    static constexpr int parameterPanelWidth = 340;
    juce::Rectangle<int> viewArea;

    juce::TextButton loadPresetButton { "Load Preset..." };
    juce::TextButton savePresetButton { "Save Preset..." };
    juce::Label presetStatusLabel;
    juce::String currentPresetName { "(no preset loaded)" };
    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::TextButton addObjectButton { "+ Object" };
    juce::TextButton removeObjectButton { "- Remove Object" };
    juce::Label objectCountLabel;

    ParameterPanel parameterPanel;
};
