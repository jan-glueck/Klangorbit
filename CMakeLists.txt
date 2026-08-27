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

# --- Ambisonics order as a compile-time default ------------------------
# Runtime switching comes later; this is just the starting value.
set(SAPOC_DEFAULT_AMBI_ORDER 3 CACHE STRING "Default Ambisonics order")
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
    FORMATS VST3 Standalone
    PRODUCT_NAME "Klangorbit"
)

target_compile_definitions(Klangorbit
    PUBLIC
        SAPOC_DEFAULT_AMBI_ORDER=${SAPOC_DEFAULT_AMBI_ORDER}
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
        Source/VBAP.cpp
        Source/ParameterRegistry.cpp
        Source/CanonicalInput.cpp
        Source/GamepadBridge.mm
        Source/GamepadDriver.cpp
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
)

target_link_libraries(Klangorbit
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        # GameController framework -- Source/GamepadBridge.mm's bridge to
        # Apple's GCController/GCExtendedGamepad API. macOS-only, matching
        # this project's own scope (see PROJECT_BRIEF.md).
        "-framework GameController"
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
