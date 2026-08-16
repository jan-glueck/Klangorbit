cmake_minimum_required(VERSION 3.22)
project(SpatialAudioPOC VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "macOS deployment target")

# --- JUCE holen -------------------------------------------------------
# Variante A (empfohlen): JUCE als Submodule in ./JUCE einchecken:
#   git submodule add https://github.com/juce-framework/JUCE.git JUCE
# Variante B: FetchContent (lädt bei jedem clean build neu, braucht Netz)
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

# --- Ambisonics-Ordnung als Kompilierzeit-Default ----------------------
# Laufzeit-Wechsel folgt spaeter; das hier ist nur der Startwert.
set(SAPOC_DEFAULT_AMBI_ORDER 3 CACHE STRING "Default Ambisonics order")
set(SAPOC_MAX_LIVE_INPUTS 8 CACHE STRING "Max. Anzahl gleichzeitiger Mono-Live-Inputs / SoundObjects")

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
        # Nur Komfort-Startordner fuer den Preset-Dateidialog waehrend der
        # Entwicklung -- absoluter Pfad, gilt daher nur fuer lokale Builds
        # aus diesem Checkout, nicht fuer ein an anderer Stelle installiertes
        # Plugin (dort faellt der Dialog auf das zuletzt genutzte Verzeichnis
        # zurueck).
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

# --- Preset-Validierung -------------------------------------------------
# Laedt alle Presets/factory/*.json ueber denselben PresetManager-Codepfad
# wie das Plugin und schlaegt bei ungueltigem Preset fehl (schemaVersion,
# Feldvalidierung). Vorbereitung fuer CI, siehe Docs/WORKFLOW.md.
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
