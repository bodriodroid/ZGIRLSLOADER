# ZGirls Loader (Android: ZGirlsMobile) - Skullgirls 2nd Encore: asset loader + viewer, engine ZDroid-Engine

> Cambios de esta versión: ver `docs/es/CAMBIOS_ZGIRLS.md`.

**This is not a playable port yet.** It is the foundation: it reads the data of *your own* copy of the game
(extracted from the `.gfs` packages, or downloaded from your own Steam account) and shows it. It ships no game
content and does not touch DRM. See `docs/RE_NOTES.md` for what a playable port still needs.

## What works (and how it was verified)

| piece | status |
|-------|--------|
| `.foit` + `.art` + `.art-pt` sprite decoder, `.dds` palettes, colour math | **verified on real data**: 1,207 annie frames identical to `tools/reference/sg_extract.py` (fields + CRC of every frame), real frames rendered and inspected |
| `.gbs` UI-scene header/view table, `stages.ini` `music.ini` `toc.ini` `compile.ini` `*.stm` parsers, case-insensitive multi-mount VFS | verified on the real core/Win/ui data (23 scenes, 32 stages, 24 tracks, 556 `.stm` references, 0 unresolved) |
| Steam download (DepotDownloader wrapper: args, prompt/progress parsing, process runner, Steam Guard flow, cancel) | **verified against a fake DepotDownloader** that emits the real prompt/progress formats (read from DepotDownloader's source). **Never run against the real DepotDownloader or Steam** |
| **Main menu** - legal notice splash (loads the data in the background) -> title -> the game's own menu pages, built from `ui/config/MainMenu/*.ini` + `ui/config/Options/*.ini` and the text of `core/localization/<lang>_loc.json` (7 languages; English and Spanish checked on screen). With the UI texture pack mounted it draws the game's own scenes (`Main.gbs`: title logo, film-strip menu, header art, fonts, sub-page bars, modals) read from the user's files | built and exercised headless on Linux (unit tests on the real files + screenshots at 2x, ASan/UBSan clean). **Not yet on a device** (see Android). Without the UI pack it falls back to ZDroid's 8x8 bitmap font and flat panels. The scene animations (`LAGG` chunk) are decoded and played on a 60 Hz clock (`src/core/gbs_anim.*`): title, background, the loading screen and the opening montage run on the game's own animations; the sub-page rows of the menu are still ZDroid's own layout |
| SDL2 viewer (Menu / Home / Browse / Steam / Play screens), headless smoke test | built and exercised on Linux with the dummy video driver; screenshots inspected |
| **Play screen** - a real match on the fight runtime: character select, `.lvl` stage (bounds, spawn points, `2D <texture>` backdrop decoded through the VFS), 60 Hz sim, health/meter/timer HUD, CPU opponent, virtual touch pad | **built and played** headless on Linux (`--screen play --click 320,293`); dummy fighters when no character data is mounted, real decoded sprites when it is. In the arm64 apk |
| **Fight runtime from the character's own data** - `src/core/moveset.*` turns a `.foit` into per-tick timelines (frame + armed hit data), `src/core/duel.*` plays them at a fixed 60 Hz (fixed-point, deterministic, snapshot-safe for rollback): real animations, real hit/hurt/push boxes, damage / hitstop / hitstun / level from the hit data, walk / dash / jump numbers from the scripts, blocking, launch + knock-down + wake-up, KO and win pose | **checked on Annie's real files**: `testMoveSetAndDuel` (walk 5.5/4, dash 14/11, jump 32, gravity 1.5, s.WP = 300 damage / 7 hitstop / 16 hitstun, snapshot round trip never diverges) + `zdroid_duel` headless runner + screenshots. Special moves, throws, projectiles and the exact reaction poses are **not** done yet (see `docs/RE_NOTES.md`) |
| **No Limits Screen** (developer option, off by default) - in a fight the picture takes the shape of the phone's screen (height stays 360, width 640..960) instead of 16:9 with bars; the world uses all of it, the HUD / round banners / menus stay 640x360 in the middle, the touch buttons move to the real edges (`src/core/screen_layout.*`, `docs/NO_LIMITS.md`) | the size formula has 168 checks (`zdroid_layout_tests`); a fight, the HUD, the touch pad and the pause menu captured headless at 640 and 800 wide and inspected. **Not yet on a phone** |
| **3D stages** (developer option "3D stages", Auto by default) - the backdrop of a fight is the stage's own 3D scene (`background.sgi.msb` + `.sgm.msb` meshes + their textures from your Core) seen through the 2D fight camera, instead of the flat picture; falls back to the picture when meshes / textures are missing or the device is too slow (`src/core/sgmesh.*`, `sgscene.*`, `stage3d.*`, `src/app/stage3d_gfx.*`, `docs/STAGES_3D.md`) | both file formats read without exception from 29 scenes / 1793 meshes (`zdroid_sgmesh_tests`, `zdroid_sgscene_tests`); camera, clipping, sorting, tiling in `zdroid_stage3d_tests`; ASG Labs captured headless in 16:9 and wide and compared with the Character Select picture. No animation, lights or skeletons yet; only ASG Labs' textures were at hand. **Not yet on a phone** |
| `.lvl` stage parser (StageSize, Start1/Start2, ambient light, 2D backdrop) | verified on the real `asg_labs_3d.lvl` + 33 more; backdrop decoded (`asg_labs_bg.dds`, DXT5) and drawn |
| Android project (`android/`), CI recipe, DepotDownloader-for-Android recipe | **built**: `assembleDebug` produces an arm64-v8a apk (`libmain.so` + `libSDL2.so`); apk contents and the SDL JNI wiring checked statically. Never *run* on a device. DepotDownloader-for-Android still unbuilt. See `android/README.md` |

11,000+ automated checks with the real data mounted (about 2,400 without it): `ctest` (see below).

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

`zdroid_viewer <data folder>` starts on the notice splash (`docs/DISCLAIMER.md`), then the title, then the menu.
The root list follows the PC edition (Download Content, Achievements, Leaderboards, Extras, Single Player, Versus, Training, Help & Options, Exit; the first three are shown dimmed - they need Lab Zero's online services). Fight entries open the Character Select (fight mode is on by default; `-DZDROID_FIGHT_MODE=OFF` hides it, see `src/app/zd_features.h`). Implemented entries: Arcade Mode / CPU Quick Match (one match vs CPU), Local Versus, Training Room, Controls,
Settings (language, difficulty, rounds, time limit, saved to `menu_settings.txt`), Exit. Everything else the original
menu lists (Story, Survival, Tutorials, Challenges, Trials, Extras, Movelist, online) is shown dimmed with a note.
Without game data a small built-in menu is used and says so. Useful flags for tests: `--lang es`, `--accept-notice`,
`--skip-title`, `--smoke-loading`, `--load-delay-ms N`, `--smoke-scale N` (capture at N x 640x360), `--dt N` (fixed frame time in ms,
so a capture of N frames is repeatable), `--click X,Y` (logical 640x360 coordinates, repeatable),
`--screen menu|splash|attract|charselect|home|browse|steam|play` (`--screen attract --attract-shot N --attract-tick T` jumps into the opening montage).

**Start-up order.** notice -> the game's loading screen (`loading_<lang>`, laid out like the PC edition: line art on top, "LOADING..." and the bomb
lower right) -> the opening montage (`AttractMode.gbs`: 17 shots, any tap / key or Back skips it) -> title -> menu.  Mount the folder with
`Textures/AttractMode`; the montage music (`sg_attract_c.wav` + `sg_attract_c_loop.wav`, converted from `music-win/music`) goes in the music folder, without it the
montage is silent.  Story Mode and the fight entries open the Character Select (Story: no stage, no second fighter; Double and Valentine stay locked until the story unlocks them).

**Character Select** (`src/app/charselect.*`, built from `Charselect.gbs` + its LAGG animations): 1. SELECT SIZE (1 / 2 / 3 medallions, both players' panels) -> 2. the fighter grid
(icons, big portrait, name plate on the pedestal, the character standing on the pedestal, data bar) -> 3. SELECT STAGE (the stage picture crossfades inside the background's window,
name bar with arrows along the bottom; thumbnails `Textures/charselect/stages`, names from the localisation).  Story Mode skips steps 1 and 3.  Touch: tap a medallion / icon / arrow / the
cancel text; tapping the selected one again confirms.  Mount these from your own copy: `ui/Win` (scene, textures, `UISound`), the sprites folder (`character_select_<name>.dds` +
`.spr.msb` + `<name>_1p.dds`, the standing characters) and `core` (`music.ini`).  On confirm the scene's own `CharSelectSnd_<name>_1` animation plays the confirm sound and the announcer
saying the name (`UISound/charconfirm.<name>.p1.wav`, `UISound/JP_chars/...` in Japanese).  Smoke flags: `--screen charselect --cs-step 0|1|2 [--cs-story]`.

**Developer Options** (Settings > Developer Options, and in a match Pause > Help & Options > Developer Options; table in `src/core/dev_options.cpp`, keys `ZD_DEV_*` in the `[options]` of the `.savini`, environment override `ZDROID_DEV_<KEY>` on the desktop): render resolution (33 / 50 / 75 / 100 %), sprite quality (quarter size ... every pixel), smoothing, shadows (Off / Low / Medium / High / Game - the PC engine's flattened-silhouette floor shadow), stage backdrop (Auto never uses the Character Select pictures; Portrait does; Generated), backdrop upscaling (bicubic 2x + sharpening), frame preloading, FPS counter, hit / hurt boxes, round intro, fighter entrances and a stage-info overlay (which picture, its size, render size, sprite level, texture memory). Matches load the stage first, then the fighters, then their sprites (decoded on worker threads).

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
