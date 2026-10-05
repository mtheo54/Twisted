# Twisted: CLAP + VST3 mixing plugin. CMake only, no plugin framework.
# Dependencies are fetched at configure time (all permissive licences):
#   - CLAP headers (MIT)                        https://github.com/free-audio/clap
#   - VST3 interface headers "pluginterfaces"   https://github.com/steinbergmedia/vst3_pluginterfaces (MIT)
#   - stb_truetype, to draw text with the system fonts (public domain / MIT)
cmake_minimum_required(VERSION 3.22)

project(Twisted VERSION 1.0.0 LANGUAGES C CXX)
if(APPLE)
  enable_language(OBJCXX)
endif()

option(TWISTED_BUILD_CLAP "Build the CLAP plugin" ON)
option(TWISTED_BUILD_VST3 "Build the VST3 plugin" ON)
option(TWISTED_BUILD_PREVIEW "Build the standalone preview app (GUI without a DAW)" ON)
option(TWISTED_BUILD_TESTS "Build the DSP unit tests" ON)
option(TWISTED_ENABLE_AVX2 "Compile for AVX2 (x86-64 CPUs from 2013 on); off = SSE2, runs everywhere" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_OBJCXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
if(MSVC)
  # Static runtime: the plugin does not need the Visual C++ redistributable.
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)
endif()

# ---- dependencies ------------------------------------------------------------------------
include(FetchContent)
# SOURCE_SUBDIR points to a folder without CMakeLists.txt: download only, we use the headers.
FetchContent_Declare(clap
  GIT_REPOSITORY https://github.com/free-audio/clap.git
  GIT_TAG 1.2.10
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR headers-only)
FetchContent_Declare(vst3_pluginterfaces
  GIT_REPOSITORY https://github.com/steinbergmedia/vst3_pluginterfaces.git
  GIT_TAG v3.8.1_build_84
  GIT_SHALLOW TRUE
  SOURCE_DIR ${CMAKE_BINARY_DIR}/_deps/vst3/pluginterfaces
  SOURCE_SUBDIR headers-only)
FetchContent_Declare(stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20
  SOURCE_SUBDIR headers-only)
FetchContent_MakeAvailable(clap vst3_pluginterfaces stb)
get_filename_component(VST3_INCLUDE_DIR ${vst3_pluginterfaces_SOURCE_DIR} DIRECTORY)

# ---- shared code: DSP, analysis, GUI, native window ------------------------------------
add_library(twisted_core STATIC
  src/core/Params.cpp
  src/core/Oversampler.cpp
  src/core/Engine.cpp
  src/core/Analyzer.cpp
  src/core/PluginCore.cpp
  src/ui/Gl.cpp
  src/ui/MeshBuilder.cpp
  src/ui/Scene.cpp
  src/ui/Draw2D.cpp
  src/ui/Editor.cpp)
if(WIN32)
  target_sources(twisted_core PRIVATE src/platform/PlatformView_win32.cpp)
  target_link_libraries(twisted_core PUBLIC opengl32 gdi32 user32)
  target_compile_definitions(twisted_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
elseif(APPLE)
  target_sources(twisted_core PRIVATE src/platform/PlatformView_cocoa.mm)
  set_source_files_properties(src/platform/PlatformView_cocoa.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
  string(REPLACE "." "_" TW_OBJC_SUFFIX "${PROJECT_VERSION}")
  target_compile_definitions(twisted_core PRIVATE TW_OBJC_SUFFIX=${TW_OBJC_SUFFIX})
  target_link_libraries(twisted_core PUBLIC "-framework Cocoa" "-framework OpenGL" "-framework CoreFoundation")
else()
  find_package(X11 REQUIRED)
  find_package(OpenGL REQUIRED)
  target_sources(twisted_core PRIVATE src/platform/PlatformView_x11.cpp)
  target_link_libraries(twisted_core PUBLIC X11::X11 OpenGL::GL OpenGL::GLX)
endif()
find_package(Threads REQUIRED)
target_link_libraries(twisted_core PUBLIC Threads::Threads)
target_include_directories(twisted_core PUBLIC src PRIVATE ${stb_SOURCE_DIR})
target_compile_definitions(twisted_core PUBLIC TW_VERSION_STRING="${PROJECT_VERSION}")

if(MSVC)
  target_compile_options(twisted_core PUBLIC /O2 /W3 /utf-8)
  if(TWISTED_ENABLE_AVX2)
    target_compile_options(twisted_core PUBLIC /arch:AVX2)
  endif()
else()
  target_compile_options(twisted_core PUBLIC -O3 -Wall -Wextra)
  if(TWISTED_ENABLE_AVX2 AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    target_compile_options(twisted_core PUBLIC -mavx2 -mfma)
  endif()
endif()

# Where each format expects its binary inside the bundle (Windows / Linux; macOS uses real bundles).
if(WIN32)
  set(TW_VST3_ARCH "x86_64-win")
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|arm64|aarch64")
    set(TW_VST3_ARCH "arm64-win")
  endif()
else()
  set(TW_VST3_ARCH "${CMAKE_SYSTEM_PROCESSOR}-linux")
endif()
set(TW_OUT ${CMAKE_BINARY_DIR}/plugins)

# ---- CLAP ------------------------------------------------------------------------------
if(TWISTED_BUILD_CLAP)
  add_library(twisted_clap MODULE src/clap/ClapPlugin.cpp)
  target_include_directories(twisted_clap PRIVATE ${clap_SOURCE_DIR}/include)
  target_link_libraries(twisted_clap PRIVATE twisted_core)
  set_target_properties(twisted_clap PROPERTIES OUTPUT_NAME Twisted PREFIX "")
  if(APPLE)
    set_target_properties(twisted_clap PROPERTIES
      BUNDLE TRUE
      BUNDLE_EXTENSION clap
      MACOSX_BUNDLE_GUI_IDENTIFIER com.twisted.monomi.clap
      MACOSX_BUNDLE_BUNDLE_NAME Twisted
      MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
      MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
      MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/cmake/Info.plist.in
      LIBRARY_OUTPUT_DIRECTORY ${TW_OUT})
  else()
    set_target_properties(twisted_clap PROPERTIES SUFFIX ".clap"
      LIBRARY_OUTPUT_DIRECTORY ${TW_OUT}
      LIBRARY_OUTPUT_DIRECTORY_RELEASE ${TW_OUT}
      LIBRARY_OUTPUT_DIRECTORY_DEBUG ${TW_OUT})
  endif()
  install(TARGETS twisted_clap LIBRARY DESTINATION CLAP BUNDLE DESTINATION CLAP)
endif()

# ---- VST3 ------------------------------------------------------------------------------
if(TWISTED_BUILD_VST3)
  add_library(twisted_vst3 MODULE
    src/vst3/Vst3Plugin.cpp
    ${vst3_pluginterfaces_SOURCE_DIR}/base/funknown.cpp
    ${vst3_pluginterfaces_SOURCE_DIR}/base/coreiids.cpp)
  target_include_directories(twisted_vst3 PRIVATE ${VST3_INCLUDE_DIR})
  target_link_libraries(twisted_vst3 PRIVATE twisted_core)
  set_target_properties(twisted_vst3 PROPERTIES OUTPUT_NAME Twisted PREFIX "")
  if(WIN32)
    target_link_libraries(twisted_vst3 PRIVATE ole32)
  endif()
  if(APPLE)
    set_target_properties(twisted_vst3 PROPERTIES
      BUNDLE TRUE
      BUNDLE_EXTENSION vst3
      MACOSX_BUNDLE_GUI_IDENTIFIER com.twisted.monomi.vst3
      MACOSX_BUNDLE_BUNDLE_NAME Twisted
      MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
      MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
      MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/cmake/Info.plist.in
      LIBRARY_OUTPUT_DIRECTORY ${TW_OUT})
    install(TARGETS twisted_vst3 LIBRARY DESTINATION VST3 BUNDLE DESTINATION VST3)
  else()
    # Twisted.vst3/Contents/<arch>/Twisted.vst3 (Windows) or Twisted.so (Linux)
    set(TW_VST3_DIR ${TW_OUT}/Twisted.vst3/Contents/${TW_VST3_ARCH})
    if(WIN32)
      set_target_properties(twisted_vst3 PROPERTIES SUFFIX ".vst3")
    else()
      set_target_properties(twisted_vst3 PROPERTIES SUFFIX ".so")
    endif()
    set_target_properties(twisted_vst3 PROPERTIES
      LIBRARY_OUTPUT_DIRECTORY ${TW_VST3_DIR}
      LIBRARY_OUTPUT_DIRECTORY_RELEASE ${TW_VST3_DIR}
      LIBRARY_OUTPUT_DIRECTORY_DEBUG ${TW_VST3_DIR})
    install(DIRECTORY ${TW_OUT}/Twisted.vst3 DESTINATION VST3)
  endif()
endif()

# ---- preview app -------------------------------------------------------------------------
if(TWISTED_BUILD_PREVIEW)
  add_executable(twisted_preview WIN32 MACOSX_BUNDLE src/preview/PreviewMain.cpp)
  target_link_libraries(twisted_preview PRIVATE twisted_core)
  set_target_properties(twisted_preview PROPERTIES OUTPUT_NAME TwistedPreview
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
  if(WIN32 AND MSVC)
    # WIN32 executable with a plain main()
    target_link_options(twisted_preview PRIVATE /ENTRY:mainCRTStartup)
  elseif(WIN32)
    target_link_options(twisted_preview PRIVATE -mwindows)
  endif()
endif()

# ---- tests -------------------------------------------------------------------------------
if(TWISTED_BUILD_TESTS)
  enable_testing()
  foreach(t oversampler_test engine_test analyzer_test)
    add_executable(${t} tests/${t}.cpp)
    target_link_libraries(${t} PRIVATE twisted_core)
    set_target_properties(${t} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    add_test(NAME ${t} COMMAND ${t})
  endforeach()
endif()
