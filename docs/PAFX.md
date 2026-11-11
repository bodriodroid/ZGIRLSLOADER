# PAFX - "Paradox Archive FX"

PAFX is the render-effects pack of the Android build. It is **not** a game archive (it is not a `.gfs`, it holds no art, no audio, no
level data) and it is **never distributed**: each player builds their own, on their own device, from their own copy of the game's
Core, and seals it with a key that is generated on that device and never leaves it.

```
player's Core (shaders/*.fx)  ──zdroid_pafx pack──▶  fx.pafx  (sealed with the per-install key)
engine's GLSL ES programs  ───────────────────────▶      │
                                                         ▼
                                   FxPack::load(bytes, key) → programs + tuning → GLES pass
```

## What is inside

| entry | what | where it comes from |
|---|---|---|
| `fx/manifest.ini` | format, program list, how many tuning groups were found, fingerprint of the Core `shaders/` folder | the packer |
| `fx/tuning.ini` | the numbers the programs read: flash colour tables (sprite + UI), contrast / brightness / saturation, the "background in a super" tint, the death-portrait colours and stops | **read from the player's own Core** (`TiledSprite_fp.fx`, `Otter_vp.fx`, `Otter_fp.fx`) at pack time |
| `fx/<program>.vert` / `.frag` | GLSL ES 1.00 sources | written for this engine (`src/fx/fx_glsl.cpp`), no tuning number in them |

The split is on purpose: **the code is ours, the numbers are the player's.** The repository, the public zips and the APK contain the
programs and a neutral fallback look (plain grey ramps) but not one flash colour of the game. Without a pack the engine still runs, it
just does not look like the game (`neutralTuning()`).

## Programs (v1)

| program | job |
|---|---|
| `sprite_palette` | fighter sprites straight from the index planes (band = palette row, grad = line bits 7..5 + tone bits 4..0) and a palette texture: tone interpolation, line darkening, outline layer, hue rotation, alpha, plus the super-shadow darkening and the flash remap. Equal to the CPU tables (`buildRgbaTables`) to within one 8-bit step, so palette swaps cost nothing and no per-palette texture is baked |
| `super_outline` | the sprite's coverage in two flat colours blended by a factor (the outline that surrounds a fighter in a super) |
| `floor_shadow` | black silhouette whose alpha fades with the height above the floor; discards outside the fade distance, on the wrong side, and where nothing is drawn |
| `flat_tint` | texture x vertex colour, then the super-shadow / flash (stages, UI cards) |
| `death_portrait`, `death_background` | the "death" look of the UI portrait (stroke / fill from the coverage channel, one widened sample) and its radial background |

Not in v1 (documented, not faked): normal-lit shading (needs the normal plane, which the `.art` decoder does not expose yet), the
"chowder" floor-colour field, per-palette-row hue masks, palettized UI portraits (`Otter_Palettized`), motion blur, `SuperflashCard`.

## File format (version 1, little-endian)

```
header, 64 bytes, plain
  0   "PAFX"
  4   u16 version (1)            6   u16 flags (0)
  8   u8[16] archive id (random per pack)
  24  u32 entry count            28  u32 sealed table size (tag included)
  32  u64 table offset (64)      40  u64 total file size
  48  u8[16] zero
sealed table at 64, then the sealed entries back to back
  record: u16 name length, name, u8 kind, u8 0, u16 0, u64 offset, u32 sealed size, u32 plain size
```

* Cipher: ChaCha20-Poly1305 (RFC 8439, `src/fx/aead.*`, no external library; checked against the RFC vectors and against vectors from an
  independent implementation).
* Sub key per pack: first 32 bytes of `ChaCha20Block(installKey, counter = id[12..16], nonce = id[0..12])`.
* Nonces: table `{0 x 8, 0xFFFFFFFF}`, entry *i* `{0 x 8, i}`. AAD: the 64 header bytes (+ the entry's own record for entries), so an
  entry cannot be moved between packs or swapped inside one; the total size in the header catches truncation and padding.
* Names, sizes and kinds are inside the sealed table: a pack reveals nothing about its content without the key.
* The key is 32 random bytes in a file (`loadOrCreatePafxKey`), created on first use with mode 0600. Keep it in the app's private
  storage; it is the only secret, and it is what makes a pack from one install useless on another.

This is integrity + confidentiality against casual copying of an install's files, not DRM: whoever has the device has the key.

## Tool

```
zdroid_pafx pack  --core <Core folder> --out fx.pafx [--key install.key]
zdroid_pafx info  fx.pafx [--key install.key]
zdroid_pafx dump  fx.pafx --dir <folder> [--key install.key]      # debugging: writes the entries as plain files
zdroid_pafx gen-check <folder> [--core <Core folder>]             # cases for the GLES check below
```

`pack` fingerprints the whole `shaders/` folder of the Core (so a loader can tell the pack was made from the Core that is installed now),
reads the three `.fx` sources it takes numbers from, and writes the sealed pack. Anything it cannot find is reported and keeps the neutral
value; a Core with none of the three files is refused.

## How the programs are checked

`src/fx/fx_math.*` has every effect written out in C++ (it is also the CPU path for builds without a GLES pass).
`tools/fx_gles_check.py` renders every program of the pack with a real GLES driver (Mesa llvmpipe works headless) over a synthetic
index texture and palette and compares each pixel with that reference: the largest difference is **1/255** for all 11 cases, both with the
neutral numbers and with the numbers read from a real Core. The CI job `fx-gles` runs it on every push; `tests/test_fx.cpp` covers the
cipher, the container (wrong key, damage anywhere, truncation, swapped entries), the number extraction and the maths; the fuzz harness
feeds the reader arbitrary bytes.

## In the live game: GPU effects (developer option, default Off)

`src/app/fx_gl.*` (`FxRenderer`) runs the programs inside SDL's own OpenGL ES 2 renderer, the one Android uses. SDL keeps a private cache
of the GL state it last set and trusts it on the next draw, so the renderer works in *passes*: a `Pass` lets SDL finish its queued drawing
and apply its viewport / clip / render target, reads what SDL set, draws with its own programs and puts every piece of GL state it touched
back (program, buffer, texture units, blend, vertex attributes) before SDL draws again. `init()` fails on any other SDL renderer, and the
game keeps its CPU path.

* **Skin** (`src/app/play.cpp`) draws fighters with the GPU route when the option is on, the pack is ready and the sprite level is 0: the
  index planes become RGBA8 textures (G = grad, B = band), the palette a tones x bands texture, and one pass draws every sprite
  (`sprite_palette`, or `sprite_palette_smooth`, the bilinear version, for Smooth) plus the floor shadows (`floor_shadow`). If a pass cannot
  start (another GL context, device reset, a texture the GPU refuses) the Skin drops the route and goes on with the CPU one.
* **The pack on the device** (`src/fx/fx_store.*`): `loadOrBuildFxPack` keeps `fx/fx.pafx` + `fx/pafx.key` in the app's private folder.
  It is reused when it opens with its key, has every program and its fingerprint matches the Core; it is rebuilt (atomically) when it is
  missing, the Core changed, it was built before a program was added, or it is unreadable. A missing Core never replaces a pack a Core made.
* **Option**: Developer Options > "GPU effects" (`ZD_DEV_GPUFX`, env `ZDROID_DEV_GPUFX`), Off by default; the stage-info overlay says whether the
  route is active and, if not, why.
* **Verification** (headless, Mesa llvmpipe through SDL's offscreen video driver): `tests/test_fx_gl.cpp` (58 checks, CI job `gl-renderer`)
  compares sprites, smooth sprites, shadows and the outline with the C++ maths across window scale, viewport, clip, logical size and render
  targets, and checks that SDL's own draws around a pass and the whole GL state are untouched. On real art (a `ZDROID_SMOKE_GL=1` viewer
  capture of a fight) the GPU route differs from the CPU one by at most 16/255 in a handful of pixels. **Nobody has run it on a real phone yet:
  that is why the option is Off.**

## What is not wired yet

* Nothing triggers the effects: the fight simulation has no super / darkening / hue-shift state yet, so the flash, the super shadow, the
  `_palette_effects.ini` tints (AllBlack, Dead_Pallor, Armor ...) and the animated hue-shift palettes are available to the GPU route but not played.
* Normal-lit shading (the `.art` decoder does not expose the normal plane), the "chowder" field, per-row hue masks, Otter's palettized portraits,
  the super-flash card and motion blur are not in v1.
* Only SDL's `opengles2` renderer, only sprite level 0; the shadows are sampled nearest.
* A "build effects pack" button next to the data-folder picker (today the pack is built the first time the option is switched on).
