cmake_minimum_required(VERSION 3.16)
project(ZGirlsLoader VERSION 0.6.0 LANGUAGES CXX)   # ZGirls Loader (engine: ZDroid-Engine)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(ZDROID_BUILD_TESTS  "Build the unit tests"        ON)
option(ZDROID_BUILD_VIEWER "Build the SDL2 viewer"       ON)
option(ZDROID_SANITIZE     "Build everything with AddressSanitizer + UndefinedBehaviorSanitizer (CI / debugging)" OFF)
option(ZDROID_FUZZ         "Build the libFuzzer harness (clang) instead of the fuzz smoke test" OFF)
option(ZDROID_FIGHT_MODE   "Expose the fight mode (Play screen) in the menu (real .foit moves, 60 Hz) - on by default" ON)
set(ZDROID_SDL2_SOURCE_DIR "" CACHE PATH "SDL2 source tree to build as a subproject (otherwise find_package(SDL2))")
set(ZDROID_TEST_DATA "" CACHE PATH "Folder holding the extracted game zips (core/, Win/, ui/) for data tests")
set(ZDROID_TEST_FOIT "" CACHE PATH "Folder with filia.foit (or characters/filia.foit) for the fight runtime test")
set(ZDROID_TEST_ORACLE "" CACHE PATH "Folder produced by tests/oracle/gen_oracle.py")
set(ZDROID_TEST_CHAR "" CACHE PATH "Folder with a real character's .foit/.art/.art-pt/_1p.dds")
set(ZDROID_TEST_MSB "" CACHE PATH "Folder holding real *.sgs.msb / *.sga.msb / *.sgm.msb assets")
set(ZDROID_TEST_MUSIC "" CACHE PATH "Folder with the real menu_intro.wav + menu_loop.wav (audio tests)")

if(ZDROID_SANITIZE AND NOT ANDROID AND NOT MSVC)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
  add_link_options(-fsanitize=address,undefined)
endif()

# ---- core library: no platform dependencies
add_library(zengine_core STATIC
  src/core/vfs.cpp
  src/core/text_formats.cpp
  src/core/gbs.cpp
  src/core/gbs_scene.cpp
  src/core/gbs_anim.cpp
  src/core/gbs_text.cpp
  src/core/menu_list_anim.cpp
  src/core/art.cpp
  src/core/foit.cpp
  src/core/foit_vm.cpp
  src/core/foit_rt.cpp
  src/core/foit_duel.cpp
  src/core/dds.cpp
  src/core/msb.cpp
  src/core/warp.cpp
  src/core/level.cpp
  src/core/fight.cpp
  src/core/moveset.cpp
  src/core/duel.cpp
  src/core/arcade.cpp
  src/core/touch_pad.cpp
  src/core/modloader.cpp
  src/core/modsdir.cpp
  src/core/errcode.cpp
  src/core/modcheck.cpp
  src/core/ramprobe.cpp
  src/core/savini.cpp
  src/core/music_lib.cpp
  src/core/hud_portrait.cpp
  src/core/imgscale.cpp
  src/core/dev_options.cpp
  src/core/palmod.cpp
  src/core/gamedata.cpp
  src/core/menu_data.cpp
  src/core/spr_tiles.cpp
  src/core/story.cpp
  src/core/steam_dl.cpp
  src/core/steam_proc.cpp
  src/core/steam_task.cpp)
target_include_directories(zengine_core PUBLIC src/core)
find_package(Threads REQUIRED)
target_link_libraries(zengine_core PUBLIC Threads::Threads)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  # keep the palette maths bit-identical to the Python reference (no fused multiply-add)
  target_compile_options(zengine_core PRIVATE -ffp-contract=off -Wall -Wextra)
endif()

# Command-line tools: host only.  The Android build only needs libmain.so, so
# skipping them keeps the APK build (and the .so list) small.
if(NOT ANDROID)
  add_executable(zdroid_probe src/tools/zdroid_probe.cpp)
  target_link_libraries(zdroid_probe PRIVATE zengine_core)

  # What the fight runtime makes of a character's .foit (missing states, the 18 normals, timeline sanity).
  add_executable(zdroid_moves src/tools/zdroid_moves.cpp)
  target_link_libraries(zdroid_moves PRIVATE zengine_core)
  add_executable(zdroid_cmds src/tools/zdroid_cmds.cpp)
  target_link_libraries(zdroid_cmds PRIVATE zengine_core)
  add_executable(zdroid_stages src/tools/zdroid_stages.cpp)
  target_link_libraries(zdroid_stages PRIVATE zengine_core)
  add_executable(zdroid_gbs src/tools/zdroid_gbs.cpp)
  target_link_libraries(zdroid_gbs PRIVATE zengine_core)
  add_executable(zdroid_foitdump src/tools/zdroid_foitdump.cpp)
  target_link_libraries(zdroid_foitdump PRIVATE zengine_core)

  # Inspect real Reverge "msb" assets (skeleton / animation / mesh).
  add_executable(zdroid_msb src/tools/zdroid_msb.cpp)
  target_link_libraries(zdroid_msb PRIVATE zengine_core)

  # Headless determinism harness for the match runtime (GGPO synctest equivalent).
  add_executable(zdroid_fight src/tools/zdroid_fight.cpp)
  target_link_libraries(zdroid_fight PRIVATE zengine_core)

  # SKGLoader mod folder inspector (list / create / enable / disable / resolve).
  add_executable(zdroid_mods src/tools/zdroid_mods.cpp)
  target_link_libraries(zdroid_mods PRIVATE zengine_core)

  # Inspect <char>.foit + .art + .art-pt: parse check, image validation, frame / state-strip PNGs.
  add_executable(zdroid_foit src/tools/zdroid_foit.cpp)
  target_link_libraries(zdroid_foit PRIVATE zengine_core)
  add_executable(zdroid_sim src/tools/zdroid_sim.cpp)
  target_link_libraries(zdroid_sim PRIVATE zengine_core)

  add_executable(zdroid_duel src/tools/zdroid_duel.cpp)
  target_link_libraries(zdroid_duel PRIVATE zengine_core)

  # The data-driven fight (foit_duel.h) through the GGPO-shaped sessions: round flow, synctest, deep rollback.
  add_executable(zdroid_foitduel src/tools/zdroid_foitduel.cpp)
  target_link_libraries(zdroid_foitduel PRIVATE zengine_core)

  # Decode a single .dds into a .png (also produces the DDS preview screenshots).
  add_executable(zdroid_dds src/tools/zdroid_dds.cpp)
  target_link_libraries(zdroid_dds PRIVATE zengine_core)

  # Writes one palettized HUD portrait (character_<name>_colors|lines|shade.pcx + a palette) as .png for inspection.
  add_executable(zdroid_hudart src/tools/zdroid_hudart.cpp)
  target_link_libraries(zdroid_hudart PRIVATE zengine_core)
endif()

if(ZDROID_BUILD_TESTS AND NOT ANDROID)
  enable_testing()
  add_executable(zdroid_tests tests/test_main.cpp)
  target_link_libraries(zdroid_tests PRIVATE zengine_core)
  add_test(NAME zdroid_tests COMMAND zdroid_tests)
  add_executable(zdroid_duel_tests tests/test_duel_rules.cpp)
  target_link_libraries(zdroid_duel_tests PRIVATE zengine_core)
  add_test(NAME zdroid_duel_tests COMMAND zdroid_duel_tests)
  set_tests_properties(zdroid_tests PROPERTIES ENVIRONMENT
    "ZDROID_TEST_DATA=${ZDROID_TEST_DATA};ZDROID_TEST_ORACLE=${ZDROID_TEST_ORACLE};ZDROID_TEST_CHAR=${ZDROID_TEST_CHAR};ZDROID_TEST_MSB=${ZDROID_TEST_MSB};ZDROID_TEST_DDS=${ZDROID_TEST_DDS};ZDROID_TEST_FOIT=${ZDROID_TEST_FOIT}")
endif()

# Parser fuzzing: always built as a deterministic smoke test; with -DZDROID_FUZZ=ON (clang) as a libFuzzer binary instead.
if(ZDROID_BUILD_TESTS AND NOT ANDROID)
  if(ZDROID_FUZZ AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_executable(zdroid_fuzz tests/fuzz/fuzz_formats.cpp)
    target_compile_definitions(zdroid_fuzz PRIVATE ZDROID_LIBFUZZER=1)
    target_compile_options(zdroid_fuzz PRIVATE -fsanitize=fuzzer,address,undefined)
    target_link_options(zdroid_fuzz PRIVATE -fsanitize=fuzzer,address,undefined)
    target_link_libraries(zdroid_fuzz PRIVATE zengine_core)
  else()
    add_executable(zdroid_fuzz_smoke tests/fuzz/fuzz_formats.cpp)
    target_link_libraries(zdroid_fuzz_smoke PRIVATE zengine_core)
    add_test(NAME zdroid_fuzz_smoke COMMAND zdroid_fuzz_smoke)
  endif()
endif()

if(ZDROID_FIGHT_MODE)
  add_compile_definitions(ZDROID_FIGHT_MODE=1)
endif()

if(ZDROID_BUILD_VIEWER)
  if(ZDROID_SDL2_SOURCE_DIR)
    if(ANDROID)
      set(SDL_SHARED ON CACHE BOOL "" FORCE)    # SDLActivity loads libSDL2.so, then libmain.so
      set(SDL_STATIC OFF CACHE BOOL "" FORCE)
      set(ZDROID_SDL2_TARGET SDL2)
    else()
      set(SDL_SHARED OFF CACHE BOOL "" FORCE)
      set(SDL_STATIC ON CACHE BOOL "" FORCE)
      set(ZDROID_SDL2_TARGET SDL2-static)
    endif()
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    add_subdirectory(${ZDROID_SDL2_SOURCE_DIR} sdl2 EXCLUDE_FROM_ALL)
  else()
    find_package(SDL2 CONFIG QUIET)
    if(SDL2_FOUND)
      set(ZDROID_SDL2_TARGET SDL2::SDL2)
    else()
      message(WARNING "SDL2 not found: skipping the viewer (use -DZDROID_SDL2_SOURCE_DIR=<SDL2 checkout>)")
    endif()
  endif()
  if(ZDROID_SDL2_TARGET)
    # viewer sources: the loader screens (zdroid_viewer.cpp) + the UI layer + the Play screen
    set(ZDROID_VIEWER_SOURCES src/app/zdroid_viewer.cpp src/app/ui.cpp src/app/play.cpp src/app/play_hud.cpp src/app/play_menu.cpp src/app/gallery.cpp src/app/credits.cpp src/app/menu.cpp src/app/game_ui.cpp src/app/menu_scene.cpp src/app/audio.cpp src/app/splash.cpp src/app/charselect.cpp src/app/attract.cpp src/app/char_sprite.cpp src/app/app_paths.cpp src/app/zg_progress.cpp src/app/story_mode.cpp src/app/confirm_dialog.cpp src/app/err_overlay.cpp)
    if(ANDROID)
      # SDLActivity calls SDL_main() inside a library that must be called "main"
      add_library(main SHARED ${ZDROID_VIEWER_SOURCES})
      target_link_libraries(main PRIVATE zengine_core ${ZDROID_SDL2_TARGET} log)
    else()
      add_executable(zdroid_viewer ${ZDROID_VIEWER_SOURCES})
      target_link_libraries(zdroid_viewer PRIVATE zengine_core ${ZDROID_SDL2_TARGET})
      if(TARGET SDL2main)
        target_link_libraries(zdroid_viewer PRIVATE SDL2main)
      elseif(TARGET SDL2::SDL2main)
        target_link_libraries(zdroid_viewer PRIVATE SDL2::SDL2main)
      endif()
      if(NOT WIN32)
        target_link_libraries(zdroid_viewer PRIVATE dl)
      endif()
      if(ZDROID_BUILD_TESTS)
        # audio: wav loop chunk, decoding and the mixer (driven by hand, no sound device needed)
        add_executable(zdroid_audio_tests tests/test_audio.cpp src/app/audio.cpp)
        target_include_directories(zdroid_audio_tests PRIVATE src/app)
        target_link_libraries(zdroid_audio_tests PRIVATE zengine_core ${ZDROID_SDL2_TARGET})
        if(NOT WIN32)
          target_link_libraries(zdroid_audio_tests PRIVATE dl)
        endif()
        # the seven-segment error / RAM display (software renderer on an off-screen surface)
        add_executable(zdroid_overlay_tests tests/test_err_overlay.cpp src/app/err_overlay.cpp src/app/ui.cpp)
        target_include_directories(zdroid_overlay_tests PRIVATE src/app)
        target_link_libraries(zdroid_overlay_tests PRIVATE zengine_core ${ZDROID_SDL2_TARGET})
        if(NOT WIN32)
          target_link_libraries(zdroid_overlay_tests PRIVATE dl)
        endif()
        add_test(NAME zdroid_overlay_tests COMMAND zdroid_overlay_tests)
        set_tests_properties(zdroid_overlay_tests PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy")
        add_test(NAME zdroid_audio_tests COMMAND zdroid_audio_tests)
        set_tests_properties(zdroid_audio_tests PROPERTIES ENVIRONMENT "SDL_AUDIODRIVER=dummy;ZDROID_TEST_MUSIC=${ZDROID_TEST_MUSIC}")
      endif()
      if(ZDROID_BUILD_TESTS AND ZDROID_TEST_CHAR)
        # headless render of the Browse and Home screens (SDL dummy video driver + software renderer)
        foreach(scr menu browse home play)
          add_test(NAME viewer_smoke_${scr}
                   COMMAND zdroid_viewer ${ZDROID_TEST_CHAR} --smoke ${CMAKE_CURRENT_BINARY_DIR}/smoke_${scr}.png
                           --screen ${scr} --char annie --frame 1)
          set_tests_properties(viewer_smoke_${scr} PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;HOME=${CMAKE_CURRENT_BINARY_DIR}"
            PASS_REGULAR_EXPRESSION "smoke: wrote")
        endforeach()
      endif()
    endif()
  endif()
endif()
