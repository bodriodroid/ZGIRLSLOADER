---
name: zgirlsloader-engineering
description: Use when inspecting, modifying, testing, documenting, or extending ZGirlsLoader / ZDroid-Engine, including its Android frontend, asset readers, VFS, fight runtime, mod loader, resource packs, stages, and compatibility work.
---

# ZGirlsLoader Engineering Skill

## Mission and project identity

You are contributing to **ZGirlsLoader**, the Android-oriented ZGirls Loader project in `bodriodroid/ZGIRLSLOADER`, whose engine is named **ZDroid-Engine**.

Treat the repository as an independent C++17 engine/asset-loader project that reads legitimate files extracted from a user's own copy of **Skullgirls 2nd Encore**. It is **not** Skullgirls Mobile, is **not** the original Lab Zero engine, and must not be described as a complete playable port unless the repository's current implementation and tests prove that claim.

Do not conflate these distinct things:
- **ZGirlsLoader / ZDroid-Engine**: this repository's loader, VFS, readers, runtime, tools, and SDL2/Android application.
- **SkullMod**: a separate tool/ecosystem that can unpack game packages. Do not claim ZGirlsLoader implements SkullMod's package extraction unless code proves it.
- **SKGLoader / legacy ModLoader**: legacy mod-folder behavior referenced by the project; inspect `docs/MODLOADER.md` and current implementation before changing compatibility.
- **Skullgirls Mobile**: a different game/product and not the target runtime.

## Required working protocol

1. **Inspect before editing.** Read the relevant source, tests, CMake target definitions, and documentation. Search for the actual implementation rather than inferring behavior from a README.
2. **Establish the baseline.** Check the current branch, working tree, recent relevant commits, and existing tests before making changes.
3. **Use a dedicated branch.** Never commit directly to `main`. Create a focused branch such as `feat/<topic>`, `fix/<topic>`, or `docs/<topic>`.
4. **Keep changes scoped.** Avoid unrelated refactors, generated build output, extracted game data, proprietary assets, APKs, credentials, and local machine files.
5. **Preserve public interfaces.** Before changing a format, API, mount order, command-line interface, or save/config behavior, search all call sites and tests. Update documentation and tests together.
6. **Test honestly.** Run the narrowest relevant test first, then the full available suite when feasible. Report exactly what ran, what passed, what failed, and what could not be run. Never claim device validation from a desktop/headless build.
7. **Review the diff.** Check for accidental binary files, game data, secrets, unrelated changes, and stale documentation. Summarize behavior changes and remaining limitations.
8. **Do not merge or force-push.** Prepare a branch/commit or pull request for review. Do not rewrite shared history.

If the repository state conflicts with this skill, the checked-out source and tests take precedence. Update this skill when architecture or verified behavior changes.

## Architecture map

The high-level flow is:

`Application / SDL2 frontend → zengine_core → VFS → format readers and game-data layer → runtime → renderer`

Important areas (verify exact current contents before editing):

- `src/core/`: portable C++17 engine library, VFS, parsers/readers, game data, mod mounting, fight logic, and platform-independent runtime.
- `src/app/`: SDL2 application/frontend, screens, UI, audio, character select, play screen, and rendering integration.
- `src/fx/`: effects and supporting math/packing/storage code.
- `src/tools/`: host-side inspection, conversion, diagnostics, and headless runtime tools (names/targets are defined in `CMakeLists.txt`).
- `tests/`: unit, integration, deterministic runtime, parser/fuzz smoke, and real-data comparison tests.
- `android/`: Gradle Android application and native SDL integration. Consult `android/README.md` for exact SDK/NDK requirements.
- `docs/`: format notes, mod documentation, runtime notes, and user/developer guides.
- `tools/reference/`: reference utilities used for comparisons. Treat reference behavior as an oracle only where tests explicitly establish it.

The CMake project declares `zengine_core` as a static library and conditionally builds the SDL2 viewer, host tools, tests, and Android native target. Read `CMakeLists.txt` before adding or moving compilation units.

## Data formats and subsystems

The project has readers or partial support for several game-data formats. Never assume every reader is complete or writable. Inspect the relevant implementation and tests before claiming support.

Known areas include:
- `.foit`: character/frame/state data and fight-runtime inputs.
- `.art` and `.art-pt`: sprite/art data.
- `.dds`: textures and palette textures.
- `.gbs`: UI scene structures and related animation/text chunks.
- `.lvl`: stage configuration and 2D backdrop references.
- `.msb` families: stage/mesh/scene-related structures; consult `src/core/msb.*`, `sgmesh.*`, `sgscene.*`, and their tests.
- Text/config data such as `stages.ini`, `music.ini`, `toc.ini`, `compile.ini`, `*.stm`, and save/settings INI files.

The repository's README documents verified examples and known gaps. Use cautious status language:
- **Implemented** means code exists.
- **Tested** means a named test or reproducible check exercises it.
- **Verified on real data** means the test explicitly uses legitimate extracted data.
- **Device-tested** means it was actually run on Android hardware. A successful APK build or headless test is not device testing.
- **Partial / experimental** means behavior is incomplete or only a subset of the format/runtime is implemented.

Never invent binary-format fields, offsets, endianness, alignment, compression, or version rules. Derive them from source, controlled test fixtures, and documented evidence. Add malformed/truncated-input tests for parser changes.

## VFS and mount behavior

The VFS supports multiple mounts and case-insensitive/path-suffix lookup as documented by the repository. Inspect `src/core/vfs.*` and its tests before changing lookup, collision, or path normalization rules.

The documented mod mount precedence is:

`game < resourcepacks < modscustom < stages < sdvoice_japanese`

Later mounts win for colliding paths. Preserve this order unless a deliberate compatibility change is approved and covered by tests. The base-game scan intentionally skips the `mods/` directory; mods are mounted explicitly.

Do not silently change path case handling, mount priority, recursive scanning, cache invalidation, or collision resolution. Add regression tests for each affected rule.

## Mod and resource-pack system

Read `docs/MODS.md`, `docs/MODLOADER.md` (if present), and the current `modloader` / `modsdir` implementation before modifying this subsystem.

Documented structure:

```text
<game folder>/mods/
  README.txt
  mods.ini
  pal/
  modscustom/
  stages/
  sdvoice_japanese/
  resourcepacks/
  .cache/
```

Known documented conventions:
- `pal/`: custom `.pal` palettes, with generated cache output under `mods/.cache/`.
- `modscustom/`: gameplay/content mods, mirroring game paths; optional `mod.ini`.
- `resourcepacks/`: replacement packs mounted below `modscustom`; optional `pack.ini`.
- `stages/`: custom stage folders with `.lvl`, textures, optional `stage.ini`, and select-screen thumbnail.
- `sdvoice_japanese/`: Japanese voice overrides, conditionally mounted for Japanese voice language.
- `mods.ini`: global enable/disable and Japanese voice selection.
- `music.ini`: stage music mapping in the game's documented format.

Do not claim ZIP installation is supported: current documentation says ZIP packs are not supported and must be unpacked into folders. Do not claim GFS package extraction is implemented by ZGirlsLoader: the README says the project expects unpacked files and that GFS packages are not unpacked by this project.

When adding a mod feature:
1. Define its folder and metadata contract.
2. Define deterministic priority/collision behavior.
3. Validate malformed metadata and paths.
4. Ensure disabling/removing a mod restores base-game resolution.
5. Add tests for discovery, activation, priority, and path traversal rejection.
6. Update `docs/MODS.md` and any CLI/help text.

Treat mod content as untrusted input. Reject path traversal, unsafe output paths, oversized allocations, invalid lengths, and malformed metadata. Do not execute scripts or binaries supplied by mods unless an explicitly reviewed sandboxed design exists.

## Fight runtime and gameplay code

The README describes a data-driven fight runtime using `.foit` character data, move timelines, hit/hurt/push boxes, fixed 60 Hz simulation, and deterministic/snapshot-oriented logic. It also documents unfinished areas such as special moves, throws, projectiles, and exact reaction poses.

Before changing fight behavior, inspect:
- `src/core/foit*`
- `src/core/moveset.*`
- `src/core/duel.*`, `fight.*`, and related runtime files
- `src/app/play.*` and HUD/input integration
- matching tests and headless tools (for example `zdroid_duel`, `zdroid_fight`, or `zdroid_sim`, if still present)

Preserve deterministic fixed-tick behavior. Avoid frame-time-dependent gameplay logic, nondeterministic iteration, unseeded randomness, floating-point behavior that breaks replay/snapshot equivalence, and hidden global state. Add repeatability/snapshot tests when changing simulation state.

Do not describe a feature as fully implemented merely because a parser can read its data. Distinguish parsing, runtime interpretation, rendering, input integration, and device validation.

## Android and rendering

The frontend uses SDL2; Android builds use the Gradle project and native CMake integration. Check `android/README.md`, Gradle configuration, CMake options, SDL version, ABI filters, and JNI/native entry points before changing build integration.

Keep platform-specific code out of `zengine_core` where practical. Do not assume the development environment has Android SDK/NDK or a physical device. If unavailable, report that limitation and still run host-side tests where relevant.

For renderer changes, test both headless/software paths and relevant SDL/GLES paths when available. Do not claim visual parity from compilation alone; use screenshots or on-device checks and describe their scope.

## Build and test workflow

Use the repository's documented commands and current CMake options. A typical desktop build (when SDL2 is available) is:

```sh
cmake -S . -B build -G Ninja -DZDROID_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

If SDL2 is not installed, use the documented `ZDROID_SDL2_SOURCE_DIR` option with a compatible SDL2 checkout. Do not download dependencies or change dependency versions without checking the project instructions and existing CI.

For sanitizer/debug builds, inspect the current `ZDROID_SANITIZE` option. For parser changes, run the fuzz smoke test or the configured libFuzzer target where supported.

Tests that need legitimate game data may use CMake cache variables such as:
- `ZDROID_TEST_DATA`
- `ZDROID_TEST_CHAR`
- `ZDROID_TEST_ORACLE`
- `ZDROID_TEST_STAGES` / `ZDROID_TEST_MSB`
- `ZDROID_TEST_MUSIC`

Never add extracted game data to the repository or CI artifacts. If the data is unavailable, run data-independent tests and clearly mark data-dependent tests as not run.

For Android, use the exact Gradle command and SDK/NDK versions in `android/README.md`. An APK build is not proof of device execution.

## Legal, privacy, and security boundaries

- Work only with game files the user is entitled to access and has supplied/extracted from their own copy.
- Do not add, redistribute, commit, or generate downloadable bundles of proprietary game assets, audio, textures, sprites, executable code, manifests, or other copyrighted game content.
- Do not bypass DRM, license checks, authentication, Steam ownership checks, or access controls. Do not use leaked manifests or credentials.
- The Steam downloader path is sensitive: README documentation describes fake-runner testing and explicitly says not to run against the real DepotDownloader or Steam during those tests. Never trigger real downloads, logins, or credential prompts as part of automated tests.
- Never place passwords, Steam Guard codes, access tokens, signing keys, or personal data in source, logs, commits, issues, or test fixtures.
- Treat archive entries, mod metadata, filenames, and binary files as untrusted input. Defend against path traversal, integer overflow, decompression bombs, out-of-bounds reads, excessive memory use, and malformed lengths.
- Do not remove notices, disclaimers, attribution, or third-party license information.

## Code quality conventions

- Follow the existing C++17 style and local naming conventions.
- Prefer small, reviewable changes over broad rewrites.
- Keep parsers bounded and explicit; validate every length/count before allocation or indexing.
- Avoid undefined behavior, unchecked casts, signed/unsigned mistakes, and assumptions about host endianness.
- Keep core logic testable without SDL or Android where practical.
- Add or update tests for bug fixes and behavior changes; include regression cases.
- Keep CLI tools' output stable where tests or scripts consume it.
- Update docs when changing user-visible behavior, format support, build requirements, or limitations.
- Do not add dependencies for trivial functionality.

## Required final report for every task

When finishing a task, report:
1. **What changed** — files and behavior.
2. **Why** — the issue or requirement addressed.
3. **Evidence** — exact build/test commands and outcomes.
4. **Limitations** — tests not run, unavailable data/devices, partial behavior.
5. **Git state** — branch and commit SHA; do not imply merge unless it happened.

Never fabricate test results, compatibility, format support, or successful execution.
