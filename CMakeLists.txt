cmake_minimum_required(VERSION 3.22)

# Static MSVC runtime so the plugin has no VC++ redistributable dependency.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

project(PlumppuVox VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

option(PLUMPPUVOX_BUILD_TOOLS "Build the offline render / test tools" OFF)
option(PLUMPPUVOX_COPY_AFTER_BUILD "Install the plugins into the user plugin folder after building" OFF)

# --------------------------------------------------------------------- JUCE
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.5
    GIT_SHALLOW TRUE
    # Works around a broken VST3 run loop in Audacity on Linux (see the patch).
    PATCH_COMMAND ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_SOURCE_DIR}/patches/patch_juce.cmake"
    UPDATE_DISCONNECTED TRUE)
FetchContent_MakeAvailable(JUCE)

# ------------------------------------------------- Rust engine (DeepFilterNet)
find_program(CARGO cargo REQUIRED)

set(ENGINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/engine")
set(ENGINE_TARGET_DIR "${CMAKE_BINARY_DIR}/engine")
set(ENGINE_CARGO_ARGS build --release --locked --manifest-path "${ENGINE_DIR}/Cargo.toml"
    --target-dir "${ENGINE_TARGET_DIR}")
set(ENGINE_ENV "")

if(WIN32)
    set(ENGINE_LIB_NAME "plumppuvox_engine.lib")
    # Match the static MSVC runtime chosen above.
    set(ENGINE_ENV "RUSTFLAGS=-C target-feature=+crt-static")
else()
    set(ENGINE_LIB_NAME "libplumppuvox_engine.a")
endif()

if(APPLE)
    set(ENGINE_ENV "MACOSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}")
endif()

set(ENGINE_LIB "${ENGINE_TARGET_DIR}/release/${ENGINE_LIB_NAME}")

# cargo does its own dependency tracking, so always invoke it.
add_custom_target(plumppuvox_engine_build
    COMMAND ${CMAKE_COMMAND} -E env ${ENGINE_ENV} ${CARGO} ${ENGINE_CARGO_ARGS}
    BYPRODUCTS "${ENGINE_LIB}"
    WORKING_DIRECTORY "${ENGINE_DIR}"
    COMMENT "Building the PlumppuVox engine (Rust)"
    VERBATIM)

add_library(plumppuvox_engine STATIC IMPORTED GLOBAL)
set_target_properties(plumppuvox_engine PROPERTIES IMPORTED_LOCATION "${ENGINE_LIB}")
target_include_directories(plumppuvox_engine INTERFACE "${ENGINE_DIR}")
add_dependencies(plumppuvox_engine plumppuvox_engine_build)

# System libraries the Rust standard library needs.
if(WIN32)
    target_link_libraries(plumppuvox_engine INTERFACE ws2_32 userenv bcrypt ntdll advapi32)
elseif(APPLE)
    target_link_libraries(plumppuvox_engine INTERFACE "-framework CoreFoundation" "-framework Security")
else()
    find_package(Threads REQUIRED)
    target_link_libraries(plumppuvox_engine INTERFACE Threads::Threads ${CMAKE_DL_LIBS} m)
endif()

# ------------------------------------------------------------------- Plugin
juce_add_plugin(PlumppuVox
    PRODUCT_NAME "PlumppuVox"
    PLUGIN_NAME "PlumppuVox Ren"
    COMPANY_NAME "Plumppu"
    BUNDLE_ID "com.plumppu.plumppuvox"
    PLUGIN_MANUFACTURER_CODE Plmp
    PLUGIN_CODE PVox
    FORMATS VST3
    VST3_CATEGORIES Fx Restoration
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${PLUMPPUVOX_COPY_AFTER_BUILD})

# Fonts for the shared Plumppu look (B612 and Barlow, SIL Open Font License).
juce_add_binary_data(PlumppuAssets SOURCES
    assets/fonts/B612-Regular.ttf
    assets/fonts/B612-Bold.ttf
    assets/fonts/B612Mono-Bold.ttf
    assets/fonts/Barlow-Black.ttf)

set(PLUMPPUVOX_SOURCES
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/plumppu/PlumppuLook.cpp)

target_sources(PlumppuVox PRIVATE ${PLUMPPUVOX_SOURCES})

target_compile_definitions(PlumppuVox PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(PlumppuVox
    PRIVATE
        juce::juce_audio_utils
        plumppuvox_engine
        PlumppuAssets
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

# A host process is shared with other plugins and with the host's own
# libraries. Keep our symbols private and bound to our own code, so that a
# same-named function elsewhere in the process can never be called by mistake.
if(UNIX AND NOT APPLE)
    target_link_options(PlumppuVox PUBLIC
        -Wl,--exclude-libs,ALL
        -Wl,-Bsymbolic
        # Never unmap the plugin: some hosts (Audacity) unload the module
        # while the plugin's interface thread is still winding down.
        -Wl,-z,nodelete
        -Wl,--no-undefined)
endif()

# ------------------------------------------------------- PlumppuVox Lagom
# The volume rider (lagom/). Plain C++, no engine; shares the look and fonts.
juce_add_plugin(PlumppuLagom
    PRODUCT_NAME "PlumppuLagom"
    PLUGIN_NAME "PlumppuVox Lagom"
    COMPANY_NAME "Plumppu"
    BUNDLE_ID "com.plumppu.plumppulagom"
    PLUGIN_MANUFACTURER_CODE Plmp
    PLUGIN_CODE PLgm
    FORMATS VST3
    VST3_CATEGORIES Fx Dynamics
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${PLUMPPUVOX_COPY_AFTER_BUILD})

set(PLUMPPULAGOM_SOURCES
    lagom/src/PluginProcessor.cpp
    lagom/src/PluginEditor.cpp
    lagom/src/Rider.cpp
    src/plumppu/PlumppuLook.cpp)

target_sources(PlumppuLagom PRIVATE ${PLUMPPULAGOM_SOURCES})
# lagom/src first: both plugins have a PluginProcessor.h
target_include_directories(PlumppuLagom PRIVATE lagom/src src)

target_compile_definitions(PlumppuLagom PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(PlumppuLagom
    PRIVATE
        juce::juce_audio_utils
        PlumppuAssets
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

if(UNIX AND NOT APPLE)
    target_link_options(PlumppuLagom PUBLIC
        -Wl,--exclude-libs,ALL
        -Wl,-Bsymbolic
        -Wl,-z,nodelete
        -Wl,--no-undefined)
endif()


# ------------------------------------------------------- Plumppu Radiomix
# Standalone app (radiomix/): automixes a recording with one speaker per
# channel, using Ren's engine and Lagom's rider on each mic, cppod's automix
# between them and Lagom once more on the mix.
option(PLUMPPUVOX_BUILD_RADIOMIX "Build the Plumppu Radiomix app" ON)

if(PLUMPPUVOX_BUILD_RADIOMIX)
    juce_add_gui_app(PlumppuRadiomix
        PRODUCT_NAME "Plumppu Radiomix"
        COMPANY_NAME "Plumppu"
        BUNDLE_ID "com.plumppu.radiomix"
        DOCUMENT_EXTENSIONS mp4 mov mkv webm m4v wav aif aiff flac mp3 ogg m4a aac)

    juce_add_binary_data(RadiomixData
        HEADER_NAME RadiomixData.h
        NAMESPACE RadiomixData
        SOURCES
            radiomix/assets/plumppu-radiomix.svg
            assets/fonts/MatrixSansPrint-Regular.ttf
            assets/fonts/MatrixMonoPrintSC-Regular.ttf)

    target_sources(PlumppuRadiomix PRIVATE
        radiomix/src/Main.cpp
        radiomix/src/Console.cpp
        radiomix/src/Faceplate.cpp
        radiomix/src/Mixer.cpp
        radiomix/src/Automix.cpp
        radiomix/src/Media.cpp
        lagom/src/Rider.cpp
        src/plumppu/PlumppuLook.cpp)
    target_include_directories(PlumppuRadiomix PRIVATE radiomix/src lagom/src src)

    target_compile_definitions(PlumppuRadiomix PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_USE_MP3AUDIOFORMAT=1
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:PlumppuRadiomix,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:PlumppuRadiomix,JUCE_VERSION>")

    target_link_libraries(PlumppuRadiomix
        PRIVATE
            juce::juce_audio_formats
            juce::juce_gui_basics
            plumppuvox_engine
            PlumppuAssets
            RadiomixData
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags)
endif()

# ---------------------------------------------------------------- Dev tools
if(PLUMPPUVOX_BUILD_TOOLS)
    juce_add_console_app(plumppuvox_tool PRODUCT_NAME "plumppuvox_tool")
    target_sources(plumppuvox_tool PRIVATE
        tools/tool.cpp
        ${PLUMPPUVOX_SOURCES})
    target_include_directories(plumppuvox_tool PRIVATE src)
    target_compile_definitions(plumppuvox_tool PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1
        JucePlugin_Name="PlumppuVox Ren"
        JucePlugin_Build_Standalone=1
        JucePlugin_IsSynth=0
        JucePlugin_WantsMidiInput=0
        JucePlugin_ProducesMidiOutput=0
        JucePlugin_IsMidiEffect=0
        JucePlugin_EditorRequiresKeyboardFocus=0)
    target_link_libraries(plumppuvox_tool PRIVATE
        juce::juce_audio_utils
        plumppuvox_engine
        PlumppuAssets
        juce::juce_recommended_config_flags)
    juce_add_console_app(plumppulagom_tool PRODUCT_NAME "plumppulagom_tool")
    target_sources(plumppulagom_tool PRIVATE
        lagom/tools/tool.cpp
        ${PLUMPPULAGOM_SOURCES})
    target_include_directories(plumppulagom_tool PRIVATE lagom/src src)
    target_compile_definitions(plumppulagom_tool PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1
        JucePlugin_Name="PlumppuVox Lagom"
        JucePlugin_Build_Standalone=1
        JucePlugin_IsSynth=0
        JucePlugin_WantsMidiInput=0
        JucePlugin_ProducesMidiOutput=0
        JucePlugin_IsMidiEffect=0
        JucePlugin_EditorRequiresKeyboardFocus=0)
    target_link_libraries(plumppulagom_tool PRIVATE
        juce::juce_audio_utils
        PlumppuAssets
        juce::juce_recommended_config_flags)
endif()
