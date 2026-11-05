# RAW 2 (ABRASION) : synthé VST3, C++20, sans JUCE ni framework.
# Seule dépendance externe : les en-têtes d'interface VST3 de Steinberg (licence MIT),
# copiés dans third_party/vst3/pluginterfaces. Aucun téléchargement pendant la compilation.
cmake_minimum_required(VERSION 3.22)
# macOS : 10.15 au minimum (std::filesystem), binaire universel si CMAKE_OSX_ARCHITECTURES="arm64;x86_64"
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Version minimale de macOS" FORCE)
endif()
project(RAW2 VERSION 2.0.0 LANGUAGES C CXX)
if(APPLE)
  enable_language(OBJCXX)
  set(CMAKE_OBJCXX_STANDARD 20)
  set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
  set(CMAKE_OBJCXX_VISIBILITY_PRESET hidden)
endif()

option(RAW2_BUILD_VST3 "Plugin VST3" ON)
option(RAW2_BUILD_AU "Plugin Audio Unit (macOS)" ON)
option(RAW2_BUILD_PREVIEW "Appli RAW2Preview : l'interface et le son sans DAW" ON)
option(RAW2_BUILD_TESTS "Tests du moteur" ON)
option(RAW2_BUILD_TRAINER "Outil raw2_train : ré-entraîne les réseaux de l'IA" ON)
option(RAW2_BUILD_AUDIT "Bancs d'audit (torture, course entre fils, fuzzing) : à compiler avec les sanitizers" OFF)
option(RAW2_DEPLOY "Copie RAW2.vst3 dans %CommonProgramFiles%\\VST3 après compilation (droits admin)" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
if(MSVC)
  # Runtime statique : pas besoin du redistribuable Visual C++ chez l'utilisateur.
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(MINGW)  # compilation croisée de vérification : tout en statique, aucune DLL MinGW à fournir
  add_link_options(-static -static-libgcc -static-libstdc++)
endif()

set(VST3_SDK_DIR ${CMAKE_SOURCE_DIR}/third_party/vst3)

# ---- polices embarquées dans la DLL (JetBrains Mono, Silkscreen : licence OFL) ----------
set(RAW2_FONTS
  ${CMAKE_SOURCE_DIR}/resources/fonts/JetBrainsMono-Regular.ttf
  ${CMAKE_SOURCE_DIR}/resources/fonts/JetBrainsMono-Medium.ttf
  ${CMAKE_SOURCE_DIR}/resources/fonts/JetBrainsMono-Bold.ttf
  ${CMAKE_SOURCE_DIR}/resources/fonts/Silkscreen-Regular.ttf)
set(RAW2_FONTS_CPP ${CMAKE_BINARY_DIR}/generated/EmbeddedFonts.cpp)
add_custom_command(
  OUTPUT ${RAW2_FONTS_CPP}
  COMMAND ${CMAKE_COMMAND} -DOUT=${RAW2_FONTS_CPP} "-DFILES=${RAW2_FONTS}" -P ${CMAKE_SOURCE_DIR}/cmake/EmbedFiles.cmake
  DEPENDS ${RAW2_FONTS} ${CMAKE_SOURCE_DIR}/cmake/EmbedFiles.cmake
  COMMENT "Polices embarquées"
  VERBATIM)

# ---- cœur : modèle, vie, moteur audio ------------------------------------------------------
add_library(raw2_core STATIC
  src/core/Params.cpp
  src/core/FactoryData.cpp
  src/core/FactoryFx.cpp
  src/core/Packs.cpp
  src/core/Json.cpp
  src/core/Files.cpp
  src/core/Wav.cpp
  src/core/Life.cpp
  src/core/Model.cpp
  src/core/Modulation.cpp
  src/core/Samples.cpp
  src/core/Instruments.cpp
  src/dsp/Fft.cpp
  src/dsp/Wavetable.cpp
  src/dsp/WavetableOps.cpp
  src/dsp/Engine.cpp
  src/dsp/Effects.cpp
  src/dsp/Mastering.cpp
  src/dsp/Bounce.cpp
  src/ai/Features.cpp
  src/ai/Mlp.cpp
  src/ai/Probe.cpp
  src/ai/AiWeights.cpp
  src/ai/Assistant.cpp)
target_include_directories(raw2_core PUBLIC src)
find_package(Threads REQUIRED)
target_link_libraries(raw2_core PUBLIC Threads::Threads)
target_compile_definitions(raw2_core PUBLIC RAW2_VERSION_STRING="${PROJECT_VERSION}")
if(WIN32)
  target_compile_definitions(raw2_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
  target_link_libraries(raw2_core PUBLIC shell32 ole32)
endif()
if(APPLE)  # corbeille du Finder (NSFileManager)
  target_sources(raw2_core PRIVATE src/platform/MacFiles.mm)
  set_source_files_properties(src/platform/MacFiles.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  target_link_libraries(raw2_core PUBLIC "-framework Foundation")
endif()
if(MSVC)
  target_compile_options(raw2_core PUBLIC /O2 /W3 /utf-8 /permissive- /Zc:__cplusplus /MP)
else()
  target_compile_options(raw2_core PUBLIC -O2 -Wall -Wextra -ffast-math -fno-finite-math-only)
endif()

# ---- tests --------------------------------------------------------------------------------
if(RAW2_BUILD_TESTS)
  enable_testing()
  add_executable(raw2_engine_test tests/engine_test.cpp)
  target_link_libraries(raw2_engine_test PRIVATE raw2_core)
  add_test(NAME raw2_engine_test COMMAND raw2_engine_test)
  add_executable(raw2_ai_test tests/ai_test.cpp)
  target_link_libraries(raw2_ai_test PRIVATE raw2_core)
  add_test(NAME raw2_ai_test COMMAND raw2_ai_test)
  add_executable(raw2_fx_test tests/fx_test.cpp)
  target_link_libraries(raw2_fx_test PRIVATE raw2_core)
  add_test(NAME raw2_fx_test COMMAND raw2_fx_test)
  add_executable(raw2_pack_test tests/pack_test.cpp)
  target_link_libraries(raw2_pack_test PRIVATE raw2_core)
  add_test(NAME raw2_pack_test COMMAND raw2_pack_test)
  add_executable(raw2_master_test tests/master_test.cpp)
  target_link_libraries(raw2_master_test PRIVATE raw2_core)
  add_test(NAME raw2_master_test COMMAND raw2_master_test)
endif()

# ---- bancs d'audit (tools/audit, voir docs/AUDIT.md) ---------------------------------------
if(RAW2_BUILD_AUDIT AND NOT WIN32)
  find_package(Threads REQUIRED)
  foreach(t torture race fuzz)
    add_executable(raw2_${t} tools/audit/${t}.cpp)
    target_link_libraries(raw2_${t} PRIVATE raw2_core Threads::Threads)
  endforeach()
endif()

# ---- entraînement de l'IA (outil, pas nécessaire au plugin) -------------------------------
if(RAW2_BUILD_TRAINER)
  add_executable(raw2_train tools/train_ai.cpp)
  target_link_libraries(raw2_train PRIVATE raw2_core)
  set_target_properties(raw2_train PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin)
  # calibre le volume des presets d'usine avec leurs effets (écrit src/core/FxTrim.inc)
  add_executable(raw2_fx_trim tools/fx_trim.cpp)
  target_link_libraries(raw2_fx_trim PRIVATE raw2_core)
  # écrit le pack de presets « RAW 2 · INSTRUMENTS » (presets bâtis sur la bibliothèque d'instruments)
  add_executable(raw2_inst_pack tools/make_inst_pack.cpp)
  target_link_libraries(raw2_inst_pack PRIVATE raw2_core)
endif()

include(${CMAKE_SOURCE_DIR}/cmake/Raw2Gui.cmake)

# ---- plugin VST3 --------------------------------------------------------------------------
# Paquet : RAW2.vst3/Contents/x86_64-win/RAW2.vst3 (Windows) ou .so (Linux, tests seulement).
if(RAW2_BUILD_VST3)
  add_library(raw2_vst3 MODULE
    src/vst3/Raw2Vst3.cpp
    ${VST3_SDK_DIR}/pluginterfaces/base/funknown.cpp
    ${VST3_SDK_DIR}/pluginterfaces/base/coreiids.cpp)
  target_include_directories(raw2_vst3 PRIVATE ${VST3_SDK_DIR})
  # code Steinberg : avertissements de format sans conséquence avec GCC
  set_source_files_properties(${VST3_SDK_DIR}/pluginterfaces/base/funknown.cpp PROPERTIES COMPILE_OPTIONS "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-w>")
  if(RAW2_HAVE_UI)
    target_link_libraries(raw2_vst3 PRIVATE raw2_ui)
  else()
    target_link_libraries(raw2_vst3 PRIVATE raw2_core)
  endif()
  if(APPLE)
    # RAW2.vst3/Contents/MacOS/RAW2 (paquet macOS), vue Cocoa compilée dans le plugin
    target_sources(raw2_vst3 PRIVATE src/platform/MacView.mm)
    set_source_files_properties(src/platform/MacView.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    string(REPLACE "." "_" RAW2_VER_ID "${PROJECT_VERSION}")
    target_compile_definitions(raw2_vst3 PRIVATE RAW2_OBJC_SUFFIX=vst3_${RAW2_VER_ID})
    target_link_libraries(raw2_vst3 PRIVATE "-framework Cocoa")
    set_target_properties(raw2_vst3 PROPERTIES
      BUNDLE TRUE
      BUNDLE_EXTENSION vst3
      OUTPUT_NAME RAW2
      MACOSX_BUNDLE_GUI_IDENTIFIER dev.abrasion.raw2.vst3
      MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
      MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
      MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/cmake/Raw2Info.plist.in
      LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/VST3
      LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/VST3
      LIBRARY_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/VST3
      LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}/VST3)
  elseif(WIN32)
    set(RAW2_ARCH "x86_64-win")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|arm64|aarch64")
      set(RAW2_ARCH "arm64-win")
    endif()
    set(RAW2_SUFFIX ".vst3")
  else()
    set(RAW2_ARCH "${CMAKE_SYSTEM_PROCESSOR}-linux")
    set(RAW2_SUFFIX ".so")
  endif()
  set(RAW2_BUNDLE ${CMAKE_BINARY_DIR}/VST3/RAW2.vst3)
  if(NOT APPLE)
    set(RAW2_BIN_DIR ${RAW2_BUNDLE}/Contents/${RAW2_ARCH})
    set_target_properties(raw2_vst3 PROPERTIES OUTPUT_NAME RAW2 PREFIX "" SUFFIX ${RAW2_SUFFIX}
      LIBRARY_OUTPUT_DIRECTORY ${RAW2_BIN_DIR}
      LIBRARY_OUTPUT_DIRECTORY_RELEASE ${RAW2_BIN_DIR}
      LIBRARY_OUTPUT_DIRECTORY_DEBUG ${RAW2_BIN_DIR}
      LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO ${RAW2_BIN_DIR}
      ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/implib)
  endif()
  install(DIRECTORY ${RAW2_BUNDLE} DESTINATION VST3)
  if(WIN32 AND RAW2_DEPLOY)
    add_custom_command(TARGET raw2_vst3 POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory ${RAW2_BUNDLE} "$ENV{CommonProgramFiles}/VST3/RAW2.vst3"
      COMMENT "Copie de RAW2.vst3 dans %CommonProgramFiles%\\VST3")
  endif()
endif()

# ---- plugin Audio Unit v2 (macOS) : RAW2.component, instrument « aumu Raw2 Abrs » ----------
if(APPLE AND RAW2_BUILD_AU AND RAW2_HAVE_UI)
  add_library(raw2_au MODULE src/au/Raw2AU.mm src/platform/MacView.mm)
  set_source_files_properties(src/au/Raw2AU.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  string(REPLACE "." "_" RAW2_VER_ID "${PROJECT_VERSION}")
  math(EXPR RAW2_AU_VERSION "(${PROJECT_VERSION_MAJOR} << 16) + (${PROJECT_VERSION_MINOR} << 8) + ${PROJECT_VERSION_PATCH}")
  target_compile_definitions(raw2_au PRIVATE RAW2_OBJC_SUFFIX=au_${RAW2_VER_ID} RAW2_AU_BUNDLE_ID="dev.abrasion.raw2.component")
  target_link_libraries(raw2_au PRIVATE raw2_ui "-framework AudioToolbox" "-framework AudioUnit" "-framework CoreAudio" "-framework Cocoa")
  set_target_properties(raw2_au PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION component
    OUTPUT_NAME RAW2
    MACOSX_BUNDLE_GUI_IDENTIFIER dev.abrasion.raw2.component
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/cmake/Raw2AUInfo.plist.in
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/AU
    LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/AU
    LIBRARY_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/AU
    LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}/AU)
endif()

# ---- appli de démonstration Windows (interface + son, sans DAW) ---------------------------
if(WIN32 AND RAW2_BUILD_PREVIEW)
  add_executable(raw2_preview WIN32 src/preview/PreviewMain.cpp)
  target_link_libraries(raw2_preview PRIVATE raw2_ui ole32)
  set_target_properties(raw2_preview PROPERTIES OUTPUT_NAME RAW2Preview
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin)
  if(MSVC)
    target_link_options(raw2_preview PRIVATE /ENTRY:wWinMainCRTStartup)
  elseif(MINGW)
    target_link_options(raw2_preview PRIVATE -municode)
  endif()
endif()

# ---- test du plugin VST3 dans un hôte minimal ---------------------------------------------
if(RAW2_BUILD_TESTS AND RAW2_BUILD_VST3)
  add_executable(raw2_host_test tests/host_test.cpp
    ${VST3_SDK_DIR}/pluginterfaces/base/funknown.cpp ${VST3_SDK_DIR}/pluginterfaces/base/coreiids.cpp)
  target_include_directories(raw2_host_test PRIVATE ${VST3_SDK_DIR})
  target_link_libraries(raw2_host_test PRIVATE ${CMAKE_DL_LIBS})
  if(WIN32)
    target_compile_definitions(raw2_host_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
  endif()
  if(APPLE)  # ouvre l'éditeur dans une NSWindow
    set_source_files_properties(tests/host_test.cpp PROPERTIES LANGUAGE OBJCXX COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(raw2_host_test PRIVATE "-framework Cocoa")
  endif()
  if(MSVC)
    target_compile_options(raw2_host_test PRIVATE /utf-8 /permissive- /Zc:__cplusplus)
  endif()
  add_test(NAME raw2_host_test COMMAND raw2_host_test $<TARGET_FILE:raw2_vst3>)
endif()
