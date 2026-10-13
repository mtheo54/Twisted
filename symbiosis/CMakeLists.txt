# SYMBIOSIS — energy-conserving spectral transfer and vector phase alignment, VST3.
# Built on the official Steinberg VST3 SDK (MIT since 3.8) and VSTGUI4 (BSD).
#
#   cmake -S symbiosis -B build-symbiosis                    # fetches the SDK if needed
#   cmake -S symbiosis -B build-symbiosis -DVST3_SDK_ROOT=... # or use a local SDK
#
# The VST3 SDK 3.8 itself requires CMake 3.25.
cmake_minimum_required(VERSION 3.25)

project(Symbiosis VERSION 1.0.0 LANGUAGES CXX C)
if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(SYMBIOSIS_BUILD_PLUGIN "Build the VST3 plug-in (needs the VST3 SDK)" ON)
option(SYMBIOSIS_BUILD_TESTS "Build the DSP unit tests (headers only, no SDK)" ON)
option(SYMBIOSIS_BUILD_SDK_HOSTS "Also build the SDK test hosts (editorhost, audiohost)" OFF)

# ---- DSP unit tests: header-only engine, no SDK needed ----------------------------------
if(SYMBIOSIS_BUILD_TESTS)
    enable_testing()
    find_package(Threads REQUIRED)
    add_executable(symbiosis_dsp_test tests/dsp_test.cpp)
    target_include_directories(symbiosis_dsp_test PRIVATE source)
    target_link_libraries(symbiosis_dsp_test PRIVATE Threads::Threads)
    if(MSVC)
        target_compile_options(symbiosis_dsp_test PRIVATE /W4 /utf-8)
    else()
        target_compile_options(symbiosis_dsp_test PRIVATE -Wall -Wextra)
    endif()
    add_test(NAME symbiosis_dsp_test COMMAND symbiosis_dsp_test)
endif()

if(NOT SYMBIOSIS_BUILD_PLUGIN)
    return()
endif()

# ---- Steinberg VST3 SDK -------------------------------------------------------------------
# Local SDK or submodule in ./vst3sdk; otherwise downloaded at configure time.
if(NOT DEFINED VST3_SDK_ROOT)
    set(VST3_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/vst3sdk" CACHE PATH "Path to VST3 SDK")
endif()
if(NOT EXISTS "${VST3_SDK_ROOT}/CMakeLists.txt")
    include(FetchContent)
    FetchContent_Declare(vst3sdk
        GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
        GIT_TAG v3.8.1_build_84
        GIT_SHALLOW TRUE
        GIT_SUBMODULES base cmake pluginterfaces public.sdk vstgui4
        SOURCE_SUBDIR do-not-add) # add_subdirectory below, with our options
    FetchContent_MakeAvailable(vst3sdk)
    set(VST3_SDK_ROOT "${vst3sdk_SOURCE_DIR}" CACHE PATH "Path to VST3 SDK" FORCE)
endif()

# Only what the plug-in needs from the SDK.
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES ${SYMBIOSIS_BUILD_SDK_HOSTS} CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VSTGUI_SUPPORT ON CACHE BOOL "" FORCE)
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "Symlink the plug-in into the system VST3 folder (admin on Windows)")
set(SMTG_CXX_STANDARD "20" CACHE STRING "" FORCE)

# Inclusion of the official SDK
set(vst3sdk_SOURCE_DIR "${VST3_SDK_ROOT}")
add_subdirectory(${VST3_SDK_ROOT} ${CMAKE_CURRENT_BINARY_DIR}/vst3sdk)
smtg_enable_vst3_sdk()

# ---- VST3 target ---------------------------------------------------------------------------
smtg_add_vst3plugin(Symbiosis
    source/version.h
    source/symbiosis_cids.h
    source/symbiosis_processor.h
    source/symbiosis_processor.cpp
    source/symbiosis_controller.h
    source/symbiosis_controller.cpp
    source/symbiosis_entry.cpp
    source/dsp/fft_engine.h
    source/dsp/ring_buffer.h
    source/dsp/spectral_engine.h
    source/dsp/yin_pitch.h
    source/ui/symbiosis_editor.h
    source/ui/symbiosis_editor.cpp
)

target_include_directories(Symbiosis PRIVATE
    source
    source/dsp
    source/ui
)

# Base libraries of the Steinberg SDK
target_link_libraries(Symbiosis PRIVATE
    sdk
    vstgui_support
)

# Montserrat (SIL OFL 1.1), loaded by VSTGUI from <bundle>/Resources/Fonts
smtg_target_add_plugin_resources(Symbiosis
    RESOURCES
        resource/fonts/Montserrat-Regular.ttf
        resource/fonts/Montserrat-Bold.ttf
    OUTPUT_SUBDIRECTORY Fonts
)

# Textures rendered by tools/generate_assets.py, loaded by VSTGUI from <bundle>/Resources
smtg_target_add_plugin_resources(Symbiosis
    RESOURCES
        resource/textures/nebula.png
        resource/textures/astrolabe_ring.png
        resource/textures/knob_bezel.png
        resource/textures/knob_gem_ruby.png
        resource/textures/knob_gem_garnet.png
        resource/textures/knob_gloss.png
        resource/textures/plasma_a.png
        resource/textures/plasma_b.png
)

if(SMTG_MAC)
    smtg_target_set_bundle(Symbiosis
        BUNDLE_IDENTIFIER dev.abrasion.symbiosis
        COMPANY_NAME "abrasion.dev"
    )
elseif(SMTG_WIN)
    target_sources(Symbiosis PRIVATE resource/win32resource.rc)
    target_compile_options(Symbiosis PRIVATE /utf-8)
endif()
