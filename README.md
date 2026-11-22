# ZGirls Loader (Android: ZGirlsMobile) - Skullgirls 2nd Encore: asset loader + viewer and modloader/port for Android

> Cambios de esta versión: ver `docs/es/CAMBIOS_ZGIRLS.md`.



> Versions: **1.0.0.0 will be the main release**; until then every build is `0.0.0.0-Dev` (no date in the name; `versionCode` keeps increasing so Android installs over the previous build).

# PLEASE USE VERSION 0.0.0.0 AND NOT OLDER VERSIONS. 

**This is not a playable port yet.** It is the foundation: it reads the data of *your own* copy of the game
(extracted from the `.gfs` packages, or downloaded from your own Steam account) and shows it. It ships no game
content and does not touch DRM. See `docs/RE_NOTES.md` for what a playable port still needs.


## Build and run (Linux/macOS/Windows desktop)

```
git clone --depth 1 --branch release-2.30.9 https://github.com/libsdl-org/SDL.git ../SDL
cmake -S . -B build -G Ninja -DZDROID_SDL2_SOURCE_DIR=../SDL
cmake --build build
./build/zdroid_viewer /path/to/unpacked          # the first folder is the data folder; more folders are extra mounts
./build/zdroid_probe  /path/to/unpacked --char annie --png out/ --records   # inventory / dump / diff tool
./build/zdroid_foit   /path/to/characters --states                              # parse every .foit (+ .art/.art-pt/_1p.dds next to it)
./build/zdroid_foit   annie.foit --frames 1,137 --state WalkFwd --out out/       # PNGs with pivot + hit/hurt/push boxes
./build/zdroid_duel   annie.foit --p1 "30:R,1:LP" --gap 180 --trace              # headless fight, tick by tick (U D L R LP MP HP LK MK HK)
```
Fight test (needs your own extracted `annie.foit/.art/.art-pt/_1p.dds` + `.lvl` stages + stage textures): mount them all, open
Play -> Training / Local Versus. Keys (P1): WASD move (double-tap = dash, W = jump), J K L = LP MP HP, U I O = LK MK HK, Enter / Space = restart round, F1 = show hit / hurt / push boxes.
Step by step guide in Spanish: `docs/es/PRUEBA_PELEA.md`.
The data folder may be nested any way (`core/core/...`, `sg_dump/unpacked/sprites/...`): the VFS indexes every file under
all of its path suffixes, case-insensitively, and skips `extract build analysis analysis_v2 SKGSTEAM mods`.
You need the **unpacked** files (`.gfs` packages are not unpacked by this project yet; SkullMod does it).

Tests: `-DZDROID_BUILD_TESTS=ON -DZDROID_TEST_DATA=<core>:<Win>:<ui> -DZDROID_TEST_CHAR=<folder with annie.*> -DZDROID_TEST_ORACLE=<dir from tests/oracle/gen_oracle.py>`
then `ctest`. `python3 -I tests/real_diff.py <dir> annie <zdroid_probe --records output>` re-checks every record against the Python reference.

## Main menu

`zdroid_viewer <data folder>` starts on the loader's own splash (its logo, `android/branding/boot_logo.png`, and a four-stage loader), then - only the first time - the notice (`docs/DISCLAIMER.md`), then the photosensitivity warning, the opening montage and the menu.
The root list follows the PC edition (Download Content, Achievements, Leaderboards, Extras, Single Player, Versus, Training, Help & Options, Exit; the first three are shown dimmed - they need Lab Zero's online services). Fight entries open the Character Select (fight mode is on by default; `-DZDROID_FIGHT_MODE=OFF` hides it, see `src/app/zd_features.h`). Implemented entries: Arcade Mode / CPU Quick Match (one match vs CPU), Local Versus, Training Room, Controls,
Settings (language, difficulty, rounds, time limit, saved to `menu_settings.txt`), Exit. Everything else the original
menu lists (Survival, online) is shown dimmed with a note.
Without game data a small built-in menu is used and says so. Useful flags for tests: `--lang es`, `--accept-notice`,
`--skip-title`, `--smoke-loading`, `--load-delay-ms N`, `--smoke-scale N` (capture at N x 640x360), `--dt N` (fixed frame time in ms,
so a capture of N frames is repeatable), `--click X,Y` (logical 640x360 coordinates, repeatable),
`--screen menu|splash|attract|charselect|home|browse|steam|play` (`--screen attract --attract-shot N --attract-tick T` jumps into the opening montage).

**Tutorials, Trials, Challenges (Training menu).**  The lists come from your own copy of the game: `training/trainingTOC.ini` and `trialsTOC.ini` -> the chapter `.stm` files -> one fight `.ini` per lesson, and `ui/config/MainMenu/challenges.ini`.
A lesson is the game's own Lua script (`LuaFile` of that `.ini`) running on the real data-driven fight: `src/core/lua_host.*` rebuilds the script API (sprites over the fight's objects, `ui.*`, the dummy's pad, forbidden move tags, saved states), `src/core/lesson.*` plugs it into the duel and `src/app/lesson_hud.*` draws the objective list, hint line and explanation pages with the game's texts.  Nothing of the scripts is bundled.
Completed lessons are remembered in `zgirls.savini` (`lesson.<fight ini>`).  Challenges are team fights in the game; the duel engine is one against one, so they are played as a row of single fights against the enemy team (the player's life carries over); life / meter set-up, regeneration / drain, locked move kinds, the time limit and the CPU script apply, assists and the `AllowDamage` limits do not (yet).
Smoke: `--lessons tutorials[:chapter]|trials|challenges`, `--lesson training/beginner/basicmovement.ini`, `--challenge 1[:fighter[:enemy]]`, `ZDROID_LESSON_AUTOPAGE=1` turns the explanation pages by itself.

**Start-up order.** splash + loader (logo, four stages: opening the data, mods and resource packs, indexing, preparing the interface and audio; at least 2.6 s) -> the notice (red; only until it
has been accepted once: `[loader] notice=1` in `zgirls.savini`, delete it to see it again) -> the photosensitivity warning (`FrontCredits.gbs`) -> the opening montage (`AttractMode.gbs`: 17 shots, any tap / key or Back skips it; it hands over to the menu) -> the menu.  There is no title screen and no logo loader (the publisher marks are never shown).
The game's own loading screen (`loading_<lang>`: line art, "LOADING..." and the bomb lower right) is the one shown before a match; `--screen attract` jumps into the montage.  Mount the folder with
`Textures/AttractMode`; the montage music (`sg_attract_c.wav` + `sg_attract_c_loop.wav`, converted from `music-win/music`) goes in the music folder, without it the
montage is silent.  Story Mode and the fight entries open the Character Select (Story: no stage, no second fighter; Double and Valentine stay locked until the story unlocks them).

**Character Select** (`src/app/charselect.*`, built from `Charselect.gbs` + its LAGG animations): 1. SELECT SIZE (1 / 2 / 3 medallions, both players' panels) -> 2. the fighter grid
(icons, big portrait, name plate on the pedestal, the character standing on the pedestal, data bar) -> 3. SELECT STAGE (the stage picture crossfades inside the background's window,
name bar with arrows along the bottom; thumbnails `Textures/charselect/stages`, names from the localisation).  Story Mode skips steps 1 and 3.  Touch: tap a medallion / icon / arrow / the
cancel text; tapping the selected one again confirms.  Mount these from your own copy: `ui/Win` (scene, textures, `UISound`), the sprites folder (`character_select_<name>.dds` +
`.spr.msb` + `<name>_1p.dds`, the standing characters) and `core` (`music.ini`).  On confirm the scene's own `CharSelectSnd_<name>_1` animation plays the confirm sound and the announcer
saying the name (`UISound/charconfirm.<name>.p1.wav`, `UISound/JP_chars/...` in Japanese).  Smoke flags: `--screen charselect --cs-step 0|1|2 [--cs-story]`.

**Hit sparks** (`src/app/spark_fx.*`, `tests/test_spark_fx.cpp`): the fight raises one event per hit / block (`src/core/foit_rt.*`: spark class = the hit's `f12`, the point where the hit and hurt boxes touch, which way the attacker faced); `SparkFx` runs the effect objects of the user's own `hitspark.foit` in a world of its own (its `None` state dispatches on slot 168, each effect steps FX32FRAME and ends itself) and draws their pictures (`data/sprites/<name>.spr.msb` + `.dds`, halved when composed, cropped, linear scaled, hot spot = the frame offset) in front of the fighters. The state -> picture table is read off the names and frame counts, not confirmed against the shipped engine; effects that are text / marks (`PBGC_*`, hearts, points) are not drawn. Debug: `ZDROID_FX_LOG=1`, `ZDROID_FX_LOGDRAW=1`, `ZDROID_PLAY_NOSPARKS=1` to turn them off.

**Colour editor** (`src/app/colour_editor.*`, `src/core/colour_edit.*`, `src/core/fav_colours.*`): hold the standing character on the Character Select (key `C` on desktop; a short tap still steps to the next colour) to open it. Tap a part of the fighter (or step through the parts), move hue / saturation / brightness with the part's shading kept, or pick one single shade of its ramp; whole-character hue, pastel / vivid / dark / light, random, and "back to the original" are one tap. "Save" stores your favourite in the app's own folder (`favcolours/<character>/<name>.zgcol` + a baked palette) and it joins the colour list of that character. It is not the mod layer and never touches `mods/` or the game files. How it works: a palette row is the linear interpolation of a few key colours, recovered by least squares (exact to <= 1 level on the real palettes) and edited; 124 checks in `zdroid_colour_tests`. Not yet tried on a phone.

**Developer Options** (Settings > Developer Options, and in a match Pause > Help & Options > Developer Options; table in `src/core/dev_options.cpp`, keys `ZD_DEV_*` in the `[options]` of the `.savini`, environment override `ZDROID_DEV_<KEY>` on the desktop): render resolution (33 / 50 / 75 / 100 %), sprite quality (quarter size ... every pixel), smoothing, shadows (Off / Low / Medium / High / Game - the PC engine's flattened-silhouette floor shadow), fighter lighting (the stage's `.lvl` lights on the fighters: ambient + diffuse + rim, through the sprites' normal plane and the palette's lit flags), stage backdrop (Auto never uses the Character Select pictures; Portrait does; Generated), backdrop upscaling (bicubic 2x + sharpening), frame preloading, FPS counter, hit / hurt boxes, round intro, fighter entrances and a stage-info overlay (which picture, its size, render size, sprite level, texture memory). Matches load the stage first, then the fighters, then their sprites (decoded on worker threads).

**Scene mode.** Mount the folder that holds the UI pack too (`ui/Win/Main.gbs`, `ui/Win/Textures/**`, `ui/Win/Fonts/**`,
all from your own copy). The menu then reads `Main.gbs` (`src/core/gbs_scene.*` parses it, `src/app/game_ui.*` draws it,
`src/app/menu_scene.cpp` drives it): the active entry is the big header image in the film band, the other entries are
the scene's `MenuItem0..10` labels on the vertical strip, descriptions use the game's fonts. Nothing from the game is
bundled in this repository. Format notes: `docs/RE_NOTES.md`.
Touch: tap a row to select it, tap again to confirm; with a mouse the row under the pointer is selected.

## Steam downloader

The Steam screen runs DepotDownloader for app 245170 with **your** account; Steam decides whether you own the game
(no anonymous/leaked manifests, no account sharing). The password and Steam Guard codes go only to the child's stdin
(never on a command line, redacted from logs). DepotDownloader is found via `ZDROID_DEPOTDOWNLOADER`,
`$ZDROID_NATIVE_LIB_DIR/libdepotdownloader.so`, `--exe`, or `PATH`. Files land in `<data folder>/SKGSTEAM`; the login token
lives in the app's own prefs folder. The download is the packed `.gfs` form: it still has to be unpacked.

## Layout

```
src/core     portable C++17 library (vfs, text formats, gbs, gbs_scene, art decoder, foit (frames/states/boxes), gamedata, level, menu_data, steam_*)
src/app      SDL2 front end: zdroid_viewer.cpp (Menu/Home/Browse/Steam) + ui.{h,cpp} (immediate-mode UI) +
             menu.{h,cpp} (notice, title, menu) + game_ui.{h,cpp} (draws .gbs scenes) + menu_scene.cpp (menu on the scenes) + play.{h,cpp} (the Play screen) + public-domain font8x8
src/tools    zdroid_probe, zdroid_foit (character body inspector), zdroid_dds, zdroid_msb, zdroid_fight, zdroid_mods, zdroid_pafx, zdroid_palgen (themed .pal palettes, see docs/MODS.md)
tests        unit tests, oracle generator, real-data diff
tools/reference/sg_extract.py   the original extractor this code is checked against
android      Gradle project (builds an arm64 apk; see android/README.md)   docs/RE_NOTES.md   reverse-engineering notes

## The `mods/` folder

The game folder gets a `mods/` folder automatically with `pal` (custom `.pal` palettes), `modscustom`, `stages`,
`sdvoice_japanese` and `resourcepacks`; the base game is never modified.  Details in [docs/MODS.md](docs/MODS.md).

## Android

git clone --depth 1 --branch release-2.30.9 https://github.com/libsdl-org/SDL.git ../SDL
cd android && ./gradlew -PsdlDir=../SDL assembleDebug     # -> app/build/outputs/apk/debug/app-debug.apk
```
Needs Android SDK 34 + NDK 26.1.10909125. The menu work in this version was compiled and tested on desktop Linux only: the Android SDK/NDK could not be downloaded where it was written, so the apk with the menu has not been built (the CI recipe in `.github/workflows` or the command above does it). `libdepotdownloader.so` (Steam screen) is optional and still unbuilt.

Controls in the apk: the Play screen puts a d-pad and six attack buttons on the glass (multi-touch, so you can
walk and attack at the same time); the Android back button leaves the match.
```
Third-party: SDL2 (zlib), font8x8 by Daniel Hepper (public domain; `char` -> `unsigned char` so it compiles as C++),
DepotDownloader (GPL-2.0; run as a separate program, not linked).
