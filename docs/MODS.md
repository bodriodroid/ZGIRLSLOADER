# The `mods/` folder

On every start ZDroid-Engine makes sure this exists inside the game folder you pointed it at (it is created automatically;
the base game files are never modified):

```
<game folder>/mods/
  README.txt
  mods.ini            optional switches (see below)
  pal/                custom palettes  (*.pal)
  modscustom/         mods that change the game
  stages/             custom stages
  sdvoice_japanese/   Japanese voices
  resourcepacks/      art / sound replacement packs
  .cache/             generated files (baked palettes) - safe to delete
```

Mount order, later wins: **game < resourcepacks < modscustom < stages < sdvoice_japanese**.
The base-game scan skips `mods/`, so nothing in it is mistaken for game data; mods are mounted on purpose, in that order.

## pal/ — custom palettes

Drop the `*.pal` files saved by the community palette editor (sub-folders allowed).  The 16-byte name in the header says which
character it is for (`Filia`, `MsFortune`, `BigBand` ...).  Each palette shows up after the game's own `1p..30p` colours of that
character, labelled `mod:<file name>`:

* Character Select, fighter step: **Tab** (or a tap on the standing character) steps through the colours; the picked colour
  paints the standing sprite and is reported on confirm.
* Viewer / fights: the palette list of the character contains them (`mod:<file name>`).

Format (see `src/core/palmod.h`): `char[16] name · u32 numColors · u8 hueInc · u8 hueInterval · u32[numColors-1] colours
(0xAARRGGBB) · u32 outline · u32 superShadow1 · u32 superShadow2`.  Colours are grouped by *region* (hair, skin, shirt ...),
light → dark; the number of key colours per region comes from a table of the game build (`src/core/pal_regions.inc`).  The game
stretches the n keys of a region over the 16 tones of its palette-texture row (tone 15 = first key, tone 0 = last key, linear,
truncated) — checked against Filia's own `filia_1p.dds`: 98.7 % of the channels identical, the rest off by 1.  A `.pal` is baked
into `mods/.cache/pal/<char>__<file>.dds`, a normal palette texture, so every screen that reads `<name>_Np.dds` uses it unchanged.

A `.pal` whose number of colours does not match the table (made for another game version) is skipped with a warning
(`zdroid_probe <game folder>` prints them).  Known gap: the table is the one of build 3.7.14, which differs from the user's data by one
region for Squigly and Marie; palettes for those two need a table update.  The hue-shift bytes and the two super-shadow colours are
read and kept, not used yet.

## modscustom/ — mods

One folder per mod; inside, mirror the game's layout (`mymod/characters/filia.foit`, `mymod/UISound/x.wav` ...).  Optional
`mod.ini`: `name`, `version`, `author`, `description`, `priority` (higher wins, default 100), `enabled`.

## resourcepacks/ — replacement packs

Same as a mod but mounted **under** `modscustom`, so a pack never beats a gameplay mod.  Optional `pack.ini` (same keys).
Delete the folder and the original content is back; nothing in the core / `.dds` files is touched.

## stages/ — custom stages

One folder per stage with its `.lvl` + textures; the `.lvl` is then found like the game's own and appears in the stage list.
Optional `stage.ini` (`name = ...`) and a select-screen thumbnail `stage_<id>.png` / `.dds` (`<id>` = folder name).  The stage is
added to the SELECT STAGE step after the game's own.

## sdvoice_japanese/ — Japanese voices

Same layout as the game's sound folders.  Only mounted while the voice language is Japanese: `--lang jp`, or
`japanese_voices = 1` in `mods.ini` (`0` forces it off, `auto` follows the language).

## music.ini — a theme for any stage
Any pack or mod folder may carry its own `music.ini`, in the game's own format; it is read together with the game's (yours wins for
the same stage):

```
Track: My Tune
Stage: Training_Stage, Arena          # stage ids as in stages.ini (optional)
Files: my_tune_intro, my_tune_loop    # intro played once, then the loop; .wav only
```
File names are matched by their letters and digits only, so `My Tune Intro .wav` also answers to `my_tune_intro`.  The loader's own
test-level theme ("Hymn For The Weekend (Final Remake)" for Training Stage, test1 and test2) plays when its two files are in your data.

## mods.ini

```
enabled = 0              # switch everything in mods/ off
disabled = id1, id2      # switch single mods / packs / stages off
japanese_voices = auto   # auto | 1 | 0
```

Not supported yet: `.zip` packs (unzip them into the folder); the old `SKGLoader/mods/<id>/` folders keep working (see MODLOADER.md).

---

### Resumen (es)

Al arrancar se crea sola `mods/` dentro de la carpeta del juego con `pal`, `modscustom`, `stages`, `sdvoice_japanese` y
`resourcepacks`. Los `.pal` se hornean a un `.dds` de paleta normal y salen como colores extra de su personaje (Tab en el
selector). Orden: juego < resourcepacks < modscustom < stages < voces japonesas. Los archivos originales nunca se tocan.
