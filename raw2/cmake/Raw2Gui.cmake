# Interface : bibliothèque raw2_ui (dessin + éditeur) et cibles qui l'utilisent.
set(RAW2_UI_SOURCES
  src/ui/Ui.cpp
  src/ui/Analyzer.cpp
  src/ui/Editor.cpp
  src/ui/EditorPages.cpp
  src/ui/EditorMix.cpp
  src/ui/EditorEfx.cpp
    src/ui/EditorEnv.cpp
    src/ui/EditorRack.cpp
    src/ui/EditorJeu.cpp
    src/ui/EditorSynth.cpp
  src/ui/EditorWt.cpp
  src/ui/EmbeddedFonts.cpp
  ${RAW2_FONTS_CPP})

if(WIN32)
  add_library(raw2_ui STATIC ${RAW2_UI_SOURCES} src/ui/D2DCanvas.cpp src/platform/Win32View.cpp)
  target_link_libraries(raw2_ui PUBLIC raw2_core d2d1 dwrite user32 gdi32 ole32 shell32 uuid)
  set(RAW2_HAVE_UI ON)
elseif(APPLE)
  # CoreGraphics + CoreText ; la vue Cocoa (MacView.mm) est compilée dans chaque plugin
  add_library(raw2_ui STATIC ${RAW2_UI_SOURCES} src/ui/CGCanvas.cpp)
  target_link_libraries(raw2_ui PUBLIC raw2_core "-framework CoreFoundation" "-framework CoreGraphics" "-framework CoreText")
  set(RAW2_HAVE_UI ON)
  # captures d'écran hors écran (contrôle visuel en intégration continue)
  add_executable(raw2_snap tools/snap.cpp)
  target_link_libraries(raw2_snap PRIVATE raw2_ui "-framework ImageIO")
else()
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(CAIRO QUIET IMPORTED_TARGET cairo cairo-ft freetype2)
  endif()
  if(CAIRO_FOUND)
    add_library(raw2_ui STATIC ${RAW2_UI_SOURCES} src/ui/CairoCanvas.cpp)
    target_link_libraries(raw2_ui PUBLIC raw2_core PkgConfig::CAIRO)
    set(RAW2_HAVE_UI ON)
    # outil de capture d'écran (développement)
    add_executable(raw2_snap tools/snap.cpp)
    target_link_libraries(raw2_snap PRIVATE raw2_ui)
  else()
    message(STATUS "RAW2 : Cairo introuvable, interface non compilée (Linux sert seulement aux tests)")
  endif()
endif()
