cmake_minimum_required(VERSION 3.22)
project(SpatialAudioPOC VERSION 0.1.0)

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

juce_add_plugin(SpatialAudioPOC
    COMPANY_NAME "YourName"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE Yrnm
    PLUGIN_CODE Sapc
    FORMATS VST3 Standalone
    PRODUCT_NAME "Spatial Audio POC"
)

target_compile_definitions(SpatialAudioPOC
    PUBLIC
        SAPOC_DEFAULT_AMBI_ORDER=${SAPOC_DEFAULT_AMBI_ORDER}
        SAPOC_MAX_LIVE_INPUTS=${SAPOC_MAX_LIVE_INPUTS}
        # Just a convenience starting folder for the preset file dialog
        # during development -- absolute path, so it only applies to local
        # builds from this checkout, not to a plugin installed elsewhere
        # (there the dialog falls back to the last-used directory).
        SAPOC_PRESETS_USER_DIR="${CMAKE_SOURCE_DIR}/Presets/user"
        SAPOC_PRESETS_FACTORY_DIR="${CMAKE_SOURCE_DIR}/Presets/factory"
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_sources(SpatialAudioPOC
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/AmbisonicsEncoder.cpp
        Source/TrajectoryEngine.cpp
        Source/PresetManager.cpp
        Source/ParameterPanel.cpp
        Source/PropagationProcessor.cpp
)

target_link_libraries(SpatialAudioPOC
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
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
