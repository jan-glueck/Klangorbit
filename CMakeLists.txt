cmake_minimum_required(VERSION 3.22)
project(Klangorbit VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "macOS deployment target")

# --- Getting JUCE -------------------------------------------------------
# Option A (recommended): check in JUCE as a submodule in ./JUCE:
#   git submodule add https://github.com/juce-framework/JUCE.git JUCE
# Option B: FetchContent (re-downloads on every clean build, needs network)
include(FetchContent)
if(EXISTS "${CMAKE_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
else()
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        7.0.12
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(JUCE)
endif()

# --- libmysofa (SOFA/HRTF file reading, Source/HrtfDataset.h/.cpp) -----
# First non-JUCE dependency in this project. BSD-3-Clause, tiny C library
# purpose-built for exactly this (see https://github.com/hoene/libmysofa)
# -- reading the AES69/SOFA HDF5-based file format ourselves would mean
# reimplementing a meaningful subset of HDF5, which this library already
# does correctly and maintains security fixes for (SOFA files can come
# from an untrusted user via the "load custom SOFA file" import feature,
# see OutputPanel.h -- v1.3.5 specifically hardens the parser against
# malformed input, which is exactly the class of file this needs to
# tolerate safely). BUILD_TESTS/BUILD_SHARED_LIBS off -- only the static
# library is needed, no need to build or run libmysofa's own test suite
# as part of this project's build.
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    mysofa
    GIT_REPOSITORY https://github.com/hoene/libmysofa.git
    GIT_TAG        v1.3.5
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(mysofa)

# libmysofa's own CMakeLists.txt generates mysofa_export.h (DLL export-
# macro header, from mysofa_export.h.in) into its OWN build directory,
# but doesn't add that directory to mysofa-static's PUBLIC include path
# the way it would for a full install()+find_package() consumer -- add it
# here so `#include <mysofa.h>` (which itself #includes "mysofa_export.h")
# resolves correctly for every target in this project that links
# mysofa-static, without repeating this per-target. $<BUILD_INTERFACE:>
# (not a bare PUBLIC path) specifically: mysofa-static also has its own
# install(EXPORT ...) rule, which validates every PUBLIC include
# directory it exports and rejects a raw path into the build tree (it
# wouldn't exist after a real install elsewhere) -- BUILD_INTERFACE scopes
# this to "only while building against this exact tree", matching how
# libmysofa's own CMakeLists.txt already wraps its OTHER include
# directories the same way, and sidesteps that validation entirely since
# this project never runs `cmake --install` on mysofa-static anyway.
target_include_directories(mysofa-static PUBLIC "$<BUILD_INTERFACE:${mysofa_BINARY_DIR}/src>")

# --- Bundled HRTF datasets (Source/HrtfDataset.h/.cpp, Binaural output
# mode) -- KEMAR (MIT Media Lab, freely usable with citation), SADIE
# II subject D1/KU100 (University of York, Apache License 2.0), and the
# TH Koeln/Bernschuetz "Spherical Far Field HRIR Compilation of the
# Neumann KU 100" (CC BY 3.0). See THIRD_PARTY_LICENSES.md for the full
# attribution text all three licenses require. juce_add_binary_data
# compiles each file into a byte array linked straight into the binary
# (JUCE's own resource-embedding mechanism, already used by no other
# asset in this project -- Assets/AppIcon.png instead goes through
# juce_add_plugin's own ICON_BIG/ICON_SMALL baking, a completely
# different, icon-specific path) -- avoids needing to locate/ship a
# loose data file relative to the plugin bundle at runtime, and behaves
# identically for VST3/Standalone/AU alike.
juce_add_binary_data(HrtfData
    SOURCES
        Assets/HRTF/kemar_44100.sofa
        Assets/HRTF/sadie_d1_44100.sofa
        Assets/HRTF/ku100_48000.sofa
)

set(SAPOC_MAX_LIVE_INPUTS 8 CACHE STRING "Max. number of simultaneous mono live inputs / SoundObjects")

juce_add_plugin(Klangorbit
    COMPANY_NAME "Jan Glueck"
    # Explicit, DNS-safe bundle identifier -- JUCE's own default derives
    # this from COMPANY_NAME, which contains a space and is therefore not
    # a valid bundle ID (a "Jan Glueck" default trips a CMake warning).
    BUNDLE_ID "com.janglueck.klangorbit"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE
    # System-wide plugin folder (all users, root of the boot volume) instead
    # of JUCE's per-user default (~/Library/Audio/Plug-Ins/VST3) -- world-
    # writable on this Mac, no sudo needed for the build.
    VST3_COPY_DIR "/Library/Audio/Plug-Ins/VST3"
    PLUGIN_MANUFACTURER_CODE Jgck
    PLUGIN_CODE Klor
    # App/plugin icon (Standalone .app + VST3 bundle), one 1024x1024 PNG
    # for both slots -- juceaide rescales it as needed per icon size, same
    # pattern JUCE's own examples use for a single square source image.
    # Source/vector original kept alongside it (Assets/AppIcon.svg) for
    # future edits; the .icns itself is baked at build time, not checked in.
    ICON_BIG "${CMAKE_SOURCE_DIR}/Assets/AppIcon.png"
    ICON_SMALL "${CMAKE_SOURCE_DIR}/Assets/AppIcon.png"
    # Classic AU (Component Manager, .component bundle) for Logic Pro/
    # GarageBand/other AU hosts, alongside the existing VST3/Standalone --
    # NOT AUv3 (a completely different app-extension packaging model,
    # unneeded here). Plain "Effect" (aufx), NOT "Music Effect" (aumf):
    # NEEDS_MIDI_INPUT TRUE + IS_SYNTH FALSE above would make JUCE infer
    # aumf by default (see JUCEUtils.cmake's own AU_MAIN_TYPE inference) --
    # deliberately overridden here after empirically confirming, with a
    # throwaway diagnostic build, that Logic Pro's Plugin Manager listed
    # this AU as installed/compatible while aumf, but never actually
    # offered it as insertable on ANY track/bus type -- switching to aufx
    # (same manufacturer/plugin code, otherwise identical) was the one
    # change that made it appear, confirmed directly by the user. Real
    # tradeoff, not free: auval flags "AU implements MusicDeviceMIDIEvent
    # but is of type 'aufx' (it should be 'aumf')" -- our MIDI-handling
    # code is still built in (from NEEDS_MIDI_INPUT), but declaring aufx
    # tells a host this isn't a MIDI-interested unit, so Logic likely
    # won't route MIDI (Mappings/Learn's CC input) to it via AU -- not yet
    # confirmed either way by an actual in-Logic MIDI test. Gamepad/OSC
    # control are unaffected either way (no dependency on this type).
    AU_MAIN_TYPE "kAudioUnitType_Effect"
    # Same system-wide-folder choice as VST3_COPY_DIR above, for
    # consistency -- requires /Library/Audio/Plug-Ins/Components itself to
    # be made world-writable first (it isn't by default, unlike the VST3
    # folder on this Mac): `sudo chmod 777 "/Library/Audio/Plug-Ins/Components"`,
    # a one-time manual step (not something this build/CMake does on its
    # own -- see the README's own Build section).
    AU_COPY_DIR "/Library/Audio/Plug-Ins/Components"
    FORMATS VST3 Standalone AU
    PRODUCT_NAME "Klangorbit"
)

target_compile_definitions(Klangorbit
    PUBLIC
        SAPOC_MAX_LIVE_INPUTS=${SAPOC_MAX_LIVE_INPUTS}
        # Just a convenience starting folder for the preset file dialog
        # during development -- absolute path, so it only applies to local
        # builds from this checkout, not to a plugin installed elsewhere
        # (there the dialog falls back to the last-used directory).
        SAPOC_PRESETS_USER_DIR="${CMAKE_SOURCE_DIR}/Presets/user"
        SAPOC_PRESETS_FACTORY_DIR="${CMAKE_SOURCE_DIR}/Presets/factory"
        # Same local-dev-convenience-path reasoning as the preset dirs
        # above, for mapping profiles (MappingProfiles/schema/README.md) --
        # a separate schema/directory tree from presets, see that file.
        SAPOC_MAPPING_PROFILES_USER_DIR="${CMAKE_SOURCE_DIR}/MappingProfiles/user"
        SAPOC_MAPPING_PROFILES_FACTORY_DIR="${CMAKE_SOURCE_DIR}/MappingProfiles/factory"
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_sources(Klangorbit
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/AmbisonicsEncoder.cpp
        Source/AmbisonicsDecoder.cpp
        Source/HrtfDataset.cpp
        Source/BinauralDecoder.cpp
        Source/VBAP.cpp
        Source/ParameterRegistry.cpp
        Source/AutomationParameterBridge.cpp
        Source/CanonicalInput.cpp
        Source/GamepadBridge.mm
        Source/GamepadDriver.cpp
        Source/MidiDriver.cpp
        Source/OscDriver.cpp
        Source/MappingEngine.cpp
        Source/MappingProfileManager.cpp
        Source/TrajectoryEngine.cpp
        Source/PresetManager.cpp
        Source/ParameterPanel.cpp
        Source/PropagationProcessor.cpp
        Source/GrainCloud.cpp
        Source/Camera3D.cpp
        Source/ObjectListPanel.cpp
        Source/SciFiLookAndFeel.cpp
        Source/HelpWindow.cpp
        Source/MappingPanel.cpp
        Source/MappingWindow.cpp
        Source/OutputPanel.cpp
        Source/OutputWindow.cpp
)

target_link_libraries(Klangorbit
    PRIVATE
        juce::juce_audio_utils
        # Source/BinauralDecoder.h/.cpp -- juce::dsp::Convolution for the
        # actual HRTF convolution (also already used by nothing else in
        # this project, first real use of this module).
        juce::juce_dsp
        # Source/OscDriver.h/.cpp -- juce::OSCReceiver/OSCMessage.
        juce::juce_osc
        # GameController framework -- Source/GamepadBridge.mm's bridge to
        # Apple's GCController/GCExtendedGamepad API. macOS-only, matching
        # this project's own scope (see PROJECT_BRIEF.md).
        "-framework GameController"
        # Source/HrtfDataset.h/.cpp -- reads the bundled/imported SOFA
        # files, see the FetchContent_Declare(mysofa ...) block above.
        mysofa-static
        HrtfData
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# --- Preset validation ---------------------------------------------------
# Loads all Presets/factory/*.json via the same PresetManager code path as
# the plugin and fails on an invalid preset (schemaVersion, field
# validation). Preparation for CI, see Docs/WORKFLOW.md.
juce_add_console_app(validate_presets
    PRODUCT_NAME "validate_presets"
)

target_compile_definitions(validate_presets
    PRIVATE
        SAPOC_MAX_LIVE_INPUTS=${SAPOC_MAX_LIVE_INPUTS}
)

target_sources(validate_presets
    PRIVATE
        Tools/validate_presets.cpp
        Source/PresetManager.cpp
        Source/TrajectoryEngine.cpp
        Source/GrainCloud.cpp
)

target_link_libraries(validate_presets
    PRIVATE
        juce::juce_core
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# --- Propagation DSP verification ----------------------------------------
# Runs PropagationProcessor against a synthetic moving source (no GUI/audio
# device needed) and checks basic invariants: measured latency roughly
# matches distance/speedOfSound, pitch goes down for a receding source and
# up for an approaching one, output never contains NaN/Inf or clips
# wildly. Not a substitute for listening, but catches DSP math regressions
# fast without a full plugin build/manual test each time.
juce_add_console_app(verify_propagation
    PRODUCT_NAME "verify_propagation"
)

target_sources(verify_propagation
    PRIVATE
        Tools/verify_propagation.cpp
        Source/PropagationProcessor.cpp
)

target_link_libraries(verify_propagation
    PRIVATE
        juce::juce_core
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# --- Grain cloud DSP verification -----------------------------------------
# Runs GrainCloud (spawn timing, all 5 movement modes, global/per-cloud
# caps, lifetime expiry) and renderGrainBlock (the exact function
# PluginProcessor uses to play back grains -- shared, not reimplemented)
# against synthetic scenarios, no GUI/audio device needed.
juce_add_console_app(verify_grain_cloud
    PRODUCT_NAME "verify_grain_cloud"
)

target_sources(verify_grain_cloud
    PRIVATE
        Tools/verify_grain_cloud.cpp
        Source/GrainCloud.cpp
)

target_link_libraries(verify_grain_cloud
    PRIVATE
        juce::juce_core
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# --- Orbit/sling-gesture math verification -------------------------------
# Runs TrajectoryEngine's Orbit-mode ellipse formula (orbitOrientation
# rotation, backward compatibility of startOrbit()'s new optional args) and
# the pure-math SlingGesture.h helpers (pull vector, orbit direction sign,
# orbit orientation) against known-correct geometry, no GUI/audio device
# needed.
juce_add_console_app(verify_orbit
    PRODUCT_NAME "verify_orbit"
)

target_sources(verify_orbit
    PRIVATE
        Tools/verify_orbit.cpp
        Source/TrajectoryEngine.cpp
        Source/GrainCloud.cpp
)

target_link_libraries(verify_orbit
    PRIVATE
        juce::juce_core
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# --- 3D camera math verification ------------------------------------------
# Runs Camera3D (default-state basis vectors, projection axis directions,
# rotation/zoom clamping, near-clip visibility, perspective size scaling)
# against known geometry. Camera3D.h itself has no juce_graphics/GUI
# dependency (see its class comment), so this stays a lightweight
# juce_core-only console app like the other verify_ tools.
juce_add_console_app(verify_camera
    PRODUCT_NAME "verify_camera"
)

target_sources(verify_camera
    PRIVATE
        Tools/verify_camera.cpp
        Source/Camera3D.cpp
)

target_link_libraries(verify_camera
    PRIVATE
        juce::juce_core
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# --- Mute/solo decision-logic verification ---------------------------------
# Runs MuteSoloLogic::isEffectivelyMuted() (own mute always wins, solo
# silences everything else non-exclusively) against every combination of
# muted/soloed/anySoloed. Pure logic, no JUCE dependency at all -- doesn't
# even need juce_core, but linked for consistency/build-flag parity with
# the other verify_ tools.
juce_add_console_app(verify_mute_solo
    PRODUCT_NAME "verify_mute_solo"
)

target_sources(verify_mute_solo
    PRIVATE
        Tools/verify_mute_solo.cpp
)

target_link_libraries(verify_mute_solo
    PRIVATE
        juce::juce_core
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# --- VBAP/triangulation verification ---------------------------------------
# Runs VBAP::triangulate()/computeGains() (horizontal-ring pairwise case and
# 3D convex-hull case) against known geometry -- gain at a speaker's own
# direction, energy normalization, correct hull-face selection for layouts
# with height speakers. No GUI/audio device needed.
juce_add_console_app(verify_vbap
    PRODUCT_NAME "verify_vbap"
)

target_sources(verify_vbap
    PRIVATE
        Tools/verify_vbap.cpp
        Source/VBAP.cpp
)

target_link_libraries(verify_vbap
    PRIVATE
        juce::juce_core
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# --- Parameter registry verification ---------------------------------------
# Runs ParameterRegistry's own contract in isolation (register/find,
# normalize()/denormalize() for both polarities including clamping and an
# asymmetric bipolar range, Scope::SpecificObject independence between
# slots, Scope::SelectedObject's dynamic retargeting including the
# nothing-selected inert case, registration-order preservation) against
# synthetic descriptors -- no PluginProcessor/GUI/audio device needed. See
# PluginProcessor::buildParameterRegistry() for the real registration (not
# covered here -- constructing a full juce::AudioProcessor outside a
# host/message-thread context isn't how any other verify_* tool in this
# project works; that integration is instead verified by the full plugin
# build + standalone launch).
juce_add_console_app(verify_parameter_registry
    PRODUCT_NAME "verify_parameter_registry"
)

target_sources(verify_parameter_registry
    PRIVATE
        Tools/verify_parameter_registry.cpp
        Source/ParameterRegistry.cpp
)

target_link_libraries(verify_parameter_registry
    PRIVATE
        juce::juce_core
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# --- DAW automation-parameter bridge verification ---------------------------
# Runs buildAutomationParameterGroups()/RegistryAutomationParameter
# (AutomationParameterBridge.h/.cpp) against synthetic ParameterRegistry
# descriptors -- grouping (one top-level group per object slot + one
# "Global" group, subgrouped by category), Scope::SelectedObject entries
# never bridged, and the actual get/set/range/default value bridging into a
# real juce::RangedAudioParameter. Deliberately standalone, same
# "no full juce::AudioProcessor outside a host context" reasoning
# verify_parameter_registry's own comment above already gives -- needs
# juce_audio_processors_headless (not the full juce_audio_processors) for
# RangedAudioParameter/AudioProcessorParameterGroup/NormalisableRange, same
# GUI-free module PluginProcessor.cpp's own VST3-wrapper investigation
# found these classes actually live in.
juce_add_console_app(verify_automation_parameters
    PRODUCT_NAME "verify_automation_parameters"
)

target_sources(verify_automation_parameters
    PRIVATE
        Tools/verify_automation_parameters.cpp
        Source/ParameterRegistry.cpp
        Source/AutomationParameterBridge.cpp
        # PresetManager/TrajectoryEngine/GrainCloud -- for the
        # getStateInformation()/setStateInformation() JSON-text round-trip
        # check (same three sources validate_presets' own target above
        # already links, for the same PresetManager dependency).
        Source/PresetManager.cpp
        Source/TrajectoryEngine.cpp
        Source/GrainCloud.cpp
)

target_link_libraries(verify_automation_parameters
    PRIVATE
        juce::juce_audio_processors_headless
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# --- Canonical controller input verification --------------------------------
# Runs CanonicalInputHub's own contract in isolation (basic dispatch/field
# round-tripping, multiple listeners, removeListener() actually stopping
# delivery, addListener() idempotency, and a reentrant add/remove-during-
# dispatch scenario to confirm the snapshot-before-call design is actually
# deadlock/corruption-safe) -- no driver, GUI, or audio device needed. No
# driver posts real events yet (that's a later branch); this only verifies
# the protocol-neutral dispatch mechanism itself.
juce_add_console_app(verify_canonical_input
    PRODUCT_NAME "verify_canonical_input"
)

target_sources(verify_canonical_input
    PRIVATE
        Tools/verify_canonical_input.cpp
        Source/CanonicalInput.cpp
)

target_link_libraries(verify_canonical_input
    PRIVATE
        juce::juce_core
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# --- Mapping engine verification ---------------------------------------------
# Runs MappingEngine (Learn-mode capture, binding application including
# cross-polarity conversion, paging/banking via a modifier source, the
# modifier itself never being a bindable target, addBinding()'s replace-
# not-accumulate semantics, removeBinding()) and MappingProfileManager
# (save/parse/load round-trip, schemaVersion rejection, load-replaces-not-
# merges) against synthetic ParameterRegistry descriptors and
# CanonicalInputEvents -- no gamepad hardware, GUI, or audio device needed.
juce_add_console_app(verify_mapping_engine
    PRODUCT_NAME "verify_mapping_engine"
)

target_sources(verify_mapping_engine
    PRIVATE
        Tools/verify_mapping_engine.cpp
        Source/MappingEngine.cpp
        Source/MappingProfileManager.cpp
        Source/CanonicalInput.cpp
        Source/ParameterRegistry.cpp
)

target_link_libraries(verify_mapping_engine
    PRIVATE
        juce::juce_core
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# --- Gamepad axis-shaping verification --------------------------------------
# Runs shapeAxis() (Source/AxisShaping.h -- deadzone rescale + exponential
# response curve for a bipolar control axis) against known math: deadzone
# clamping at/inside the boundary, no jump just past the boundary,
# full-scale endpoints always map to +-1 regardless of tuning, sign
# preservation/symmetry, exponent ordering (higher exponent -> smaller
# output at a fixed rescaled input), and monotonicity across the whole
# range. Pure math, no gamepad hardware needed -- GamepadDriver's actual
# hardware polling (Source/GamepadBridge.mm, GCController) isn't testable
# headlessly and is instead verified by full build + Standalone launch
# stability, consistent with how this project verifies that class of
# integration elsewhere.
juce_add_console_app(verify_axis_shaping
    PRODUCT_NAME "verify_axis_shaping"
)

target_sources(verify_axis_shaping
    PRIVATE
        Tools/verify_axis_shaping.cpp
)

target_link_libraries(verify_axis_shaping
    PRIVATE
        juce::juce_core
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# --- Ambisonics decoder verification ---------------------------------------
# Runs AmbisonicsDecoder's metadata helpers (channel counts/orders/LFE index
# per mode) and its actual decode() output (Stereo and AllRAD-based Quad/
# 5.1/Atmos decode, LFE bass-management on/off) against a real encoded test
# source, via AmbisonicsEncoder -- no GUI/audio device needed.
juce_add_console_app(verify_ambisonics_decoder
    PRODUCT_NAME "verify_ambisonics_decoder"
)

target_sources(verify_ambisonics_decoder
    PRIVATE
        Tools/verify_ambisonics_decoder.cpp
        Source/AmbisonicsDecoder.cpp
        Source/AmbisonicsEncoder.cpp
        Source/VBAP.cpp
)

target_link_libraries(verify_ambisonics_decoder
    PRIVATE
        juce::juce_audio_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# --- MIDI driver verification ------------------------------------------------
# Runs MidiDriver (Source/MidiDriver.h) against hand-built juce::MidiMessages
# wrapped in a juce::MidiBuffer -- processMidiBuffer() (the audio-thread-side
# queue push) followed by drainAndDispatch() (the message-thread-side
# translate + CanonicalInputHub::dispatch()) -- covering CC, Note On/Off,
# Pitch Bend (center/min/max), an empty-queue no-op, multiple messages in one
# block, and an untranslated message type (Program Change) producing nothing.
# No real MIDI hardware/host routing needed -- messages are constructed
# directly.
juce_add_console_app(verify_midi_driver
    PRODUCT_NAME "verify_midi_driver"
)

target_sources(verify_midi_driver
    PRIVATE
        Tools/verify_midi_driver.cpp
        Source/MidiDriver.cpp
        Source/CanonicalInput.cpp
        Source/ParameterRegistry.cpp
)

target_link_libraries(verify_midi_driver
    PRIVATE
        juce::juce_audio_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# --- OSC driver verification --------------------------------------------------
# Runs OscInterpretation::interpretMessage() (Source/OscDriver.h/.cpp) --
# the pure, extracted interpretation logic -- against hand-built
# juce::OSCMessages, covering a float argument, an int32 argument, zero
# arguments (bare-trigger Button convention), a non-numeric first argument
# (returns false), and out-of-range value clamping. Deliberately does NOT
# exercise OscDriver itself (the class with the real juce::OSCReceiver socket
# and JUCE message-thread marshaling) -- that half isn't testable headlessly
# the same way GamepadDriver's own hardware polling isn't, and is instead
# verified by full build + Standalone launch stability, consistent with how
# this project verifies that class of integration elsewhere.
juce_add_console_app(verify_osc_driver
    PRODUCT_NAME "verify_osc_driver"
)

target_sources(verify_osc_driver
    PRIVATE
        Tools/verify_osc_driver.cpp
        Source/OscDriver.cpp
        Source/CanonicalInput.cpp
        Source/ParameterRegistry.cpp
)

target_link_libraries(verify_osc_driver
    PRIVATE
        juce::juce_osc
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# --- Gamepad driver verification ---------------------------------------------
# Runs GamepadDriver's fixed default control scheme (see its own class
# comment) directly, via hand-built GamepadState previous/current pairs
# against a real TrajectoryEngine -- object cycle/add/remove (Button
# X/A/B) and Free Throw/Orbit Shot/Slingshot (Button Y/Left Shoulder/
# Right Shoulder held + left stick aim, reusing SlingGesture.h's own pure
# math). driveObjectManagement()/driveThrowGesture() are public
# specifically so this test can call them without a connected controller
# -- GamepadBridge (the actual GCController polling, and
# driveSelectedObjectMovement()'s own use of it via poll()) is the part
# that genuinely isn't testable headlessly, same as before this test
# existed, and is instead verified by full build + Standalone launch
# stability.
juce_add_console_app(verify_gamepad_driver
    PRODUCT_NAME "verify_gamepad_driver"
)

target_sources(verify_gamepad_driver
    PRIVATE
        Tools/verify_gamepad_driver.cpp
        Source/GamepadDriver.cpp
        Source/GamepadBridge.mm
        Source/TrajectoryEngine.cpp
        Source/GrainCloud.cpp
        Source/CanonicalInput.cpp
        Source/ParameterRegistry.cpp
)

target_link_libraries(verify_gamepad_driver
    PRIVATE
        juce::juce_core
        # GamepadBridge.mm's bridge to Apple's GCController/GCExtendedGamepad
        # API -- same dependency the Klangorbit target itself links, see its
        # own comment.
        "-framework GameController"
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# --- Binaural (HRTF) decoder verification ---------------------------------
# Runs HrtfDataset (the libmysofa wrapper, Source/HrtfDataset.h/.cpp)
# against the two real bundled SOFA files directly from Assets/HRTF/ (see
# SAPOC_ASSETS_HRTF_DIR below -- a local-dev-only absolute path define,
# same convention as SAPOC_PRESETS_USER_DIR etc.), and BinauralDecoder's
# own virtual-array/convolution-summing plumbing against a synthetic,
# hand-built HRTF (no real dataset needed for that half -- see the test
# file's own section comments).
juce_add_console_app(verify_binaural_decoder
    PRODUCT_NAME "verify_binaural_decoder"
)

target_compile_definitions(verify_binaural_decoder
    PRIVATE
        SAPOC_ASSETS_HRTF_DIR="${CMAKE_SOURCE_DIR}/Assets/HRTF"
)

target_sources(verify_binaural_decoder
    PRIVATE
        Tools/verify_binaural_decoder.cpp
        Source/HrtfDataset.cpp
        Source/BinauralDecoder.cpp
        Source/AmbisonicsEncoder.cpp
)

target_link_libraries(verify_binaural_decoder
    PRIVATE
        juce::juce_dsp
        mysofa-static
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# --- VST3 bus-negotiation diagnostic (not a pass/fail verify_* test) --------
# Loads a BUILT Klangorbit.vst3 the way a real VST3 host would, via JUCE's
# own hosting code (full juce_audio_processors, JUCE_PLUGINHOST_VST3=1) --
# exercises the REAL VST3 ABI (IAudioProcessor::setBusArrangements()/
# getBusArrangement() on the actual compiled binary), not just our own
# PluginProcessor::isBusesLayoutSupported() called in-process. Written to
# get ground truth on a "VST3 output stuck at Stereo, independent of DAW"
# report that survived two source-reading-only fixes -- prints which
# candidate output layouts a real negotiation attempt actually succeeds or
# fails for. Run manually (`vst3_bus_probe <path-to-.vst3>`), not part of
# the automated verify_*/validate_presets suite.
juce_add_console_app(vst3_bus_probe
    PRODUCT_NAME "vst3_bus_probe"
)

target_compile_definitions(vst3_bus_probe
    PRIVATE
        JUCE_PLUGINHOST_VST3=1
)

target_sources(vst3_bus_probe
    PRIVATE
        Tools/vst3_bus_probe.cpp
)

target_link_libraries(vst3_bus_probe
    PRIVATE
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
