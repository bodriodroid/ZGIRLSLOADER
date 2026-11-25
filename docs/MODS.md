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

A `.pal` whose number of colours does not match the table is skipped with a warning (`zdroid_probe <game folder>` prints them).
The community's `.pal` files follow build 3.7.14; the game data this loader reads has one region more for Squigly (a 4-key one at region 14)
and one fewer for Marie (one of the five-key regions 65..70).  `palmod::regionCounts` is the table of the data (measured on the shipped
`<name>_1p.dds`: every row of both fits it), `palmod::legacyRegionCounts` the 3.7.14 one.  A `.pal` made for the data bakes row for row; one
made for 3.7.14 is laid out with its own table and then moved onto the data's regions, so no colour slips a row (the region Squigly gained
copies the one before it; for Marie the dropped region cannot be told from the counts, so the five-key regions 65..70 may shift by one).
The two super-outline colours (`shadow1` / `shadow2`) are used
by the super move picture (see `docs/PAFX.md`); the hue-shift bytes are read and kept, not played yet.

### Making palettes from your own colours: `zdroid_palgen`

`zdroid_palgen --sprites <folder with <name>_1p.dds> --out <folder> [--theme claude|dark|all] [--only Name,Name] [--swatch file.png]`
reads each character's default palette texture, recolours its regions and writes `<out>/<Character>/<Theme>.pal` — the layout of
`mods/pal`, so the result can be copied straight in.  The built-in theme is the colours of the Claude chat (`src/core/palgen.*`):
every region is classified from its 16 tones — already (nearly) black stays black, (nearly) white becomes white, skin becomes a lighter
grey, everything else the normal grey — and the luminance of every tone goes through the role's ramp, so the shading of the art is kept.
The iris regions listed in `src/core/pal_theme_hints.inc` become the Claude orange (#D97757) and the ties black; the other region numbers
of a character are not guessed.  `Claude` is the normal one, `ClaudeDark` the same a step darker.  A character whose texture does not have
the rows of the region table (Squigly and Marie here) is skipped with a line saying why.

The output is derived from your game's palette textures: keep it for yourself, do not publish it.  Only the generator is in the repository.

## modscustom/ — mods

One folder per mod; inside, mirror the game's layout (`mymod/characters/filia.foit`, `mymod/UISound/x.wav` ...).  Optional
`mod.ini`: `name`, `version`, `author`, `description`, `priority` (higher wins, default 100), `enabled`.

## resourcepacks/ — resource packs (two modes)

A pack is a folder (or a `.zip`, unpacked like a mod) in `resourcepacks/`.  It is mounted **under** `modscustom`, so a pack never beats a
gameplay mod.  Optional `pack.ini` (same keys as `mod.ini`).  A pack works in one of two modes, decided by one file:

| the pack has | mode | what changes |
|---|---|---|
| a `changes.lua` | **rules** | only what the script says: each rule names a game file and the pack file that answers for it.  The pack's other files do nothing by themselves. |
| no `changes.lua` | **automatic (by name)** | every file of the pack replaces the game's file(s) with the same **name**, whatever the folder (`filia_1p.dds` answers to `characters/filia_1p.dds`) |

In both modes:

* The game's files are **never written to**.  A rule or a name only changes what a lookup resolves to, in memory; delete the folder and
  everything is back.  Anything a script writes goes to `mods/.cache/changes/<pack>/` and nowhere else.
* **`.foit`, `.art` and `.art-pt` are not part of a resource pack** (that is a mod: put it in `modscustom/`).  Automatic mode leaves them
  out (the Mods screen warns with the first names), a rule that targets one is refused, and they cannot block the pack.
* The pack is checked before it is loaded like any other folder (damaged files → error 4, native code → error 5).
* The Mods screen / `zdroid_probe` show the mode of each pack.

### changes.lua (rules mode)

A Lua 5.1 script run once at start in a sandbox (no `require`, no `os.execute`, no `io.popen`; `io.open` / `os.remove` only reach the
pack's scratch folder; a script that runs too long is stopped).  If it fails, **nothing** of that pack is applied (all or nothing) and
the error is shown.  Two styles, both accepted:

1. The community loader's style (SkullLoader "CCLib"): `ModInfo`, `Mod.install()` (or a global `install()` / `init()`), calling
   `CCLib.gfsAddFile(archive, "game/path.ext", "file of the pack")`.  Supported: `gfsAddFile`, `gfsAddFiles` (a folder), `gfsExtractFile(s)`
   (copy a game file, or what an earlier rule made of it, to a scratch path so the script can edit it), `gfsCommitChanges` (no-op),
   `addLocalization(key, text)` (adds the key to a *copy* of every `*_loc.json`), `addNewPermission` (accepted, nothing to do),
   `hasMod` / `getModVersion`, `getWorkingDirectory`, `work_dir` / `data01_dir` / `data02_dir`.  `gbsMerge` and the memory functions are
   refused with a warning (they would change the running game, not its files).  The archive name is accepted and ignored.
2. Plain tables, returned or in a global `Changes` / `changes` / `Rules`:

```lua
return {
  ["characters/filia_1p.dds"] = "mine/filia.dds",                  -- game path = file of the pack
  { game = "UISound/hit.wav", source = "sfx/hit.wav" },
  { "ui/Win/Main.gbs", "ui/Main.gbs" },
}
```

Paths of the pack are relative to its folder (`<work_dir>\\mods\\<name>\\x` is understood too).  A game path may not leave the game
(`..` or an absolute path is refused).

**Not verified against a real AOTC `changes.lua`**: the format above is read from the community loader's documentation plus plain tables.
If a real pack does something this does not cover, send its `changes.lua` (and the pack's folder listing) and it gets fixed.

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
A stage's own `.lvl` wins: its `Music_Intro` / `Music_Loop` lines name the theme it plays (file stems, matched the same way), and `music.ini` only fills in for a stage whose `.lvl`
names none (or whose files are not in your data).  To change a stage's music with a mod, ship its `.lvl` with the lines changed, or give the stage no music lines and a `Stage:` entry here.
File names are matched by their letters and digits only, so `My Tune Intro .wav` also answers to `my_tune_intro`.  The loader's own
test-level theme ("Hymn For The Weekend (Final Remake)" for Training Stage, test1 and test2) plays when its two files are in your data.

## mods.ini

```
enabled = 0              # switch everything in mods/ off
disabled = id1, id2      # switch single mods / packs / stages off
japanese_voices = auto   # auto | 1 | 0
```

The old `SKGLoader/mods/<id>/` folders keep working (see MODLOADER.md).

## Shipping a mod as a `.zip`

Drop `mymod.zip` into `modscustom/`, `resourcepacks/` or `stages/`.  At the next start it is unpacked next to itself (`mymod.zip` →
`mymod/`) and loaded like any folder: `mod.ini` / `pack.ini`, the on/off switch of the Mods screen, the checks and the conflict list all
work the same.  Details:

* A single wrapper folder (`mymod-master/…`, what GitHub's "Download ZIP" makes) is taken off; `__MACOSX/` and `.DS_Store` are skipped.
  A mod that is already inside a wrapper *folder* works too: its `mod.ini` is found one level down.
* `mymod/.zipsrc` remembers the zip's size and date: an unchanged zip is not unpacked again, a replaced zip is.  A folder with the same
  name that you made yourself is never overwritten (a warning says so).
* Stored and deflate entries only (what every zip tool writes).  Not supported, and reported instead of guessed: encryption, ZIP64
  (> 4 GiB), other compression methods.  Every entry's size and CRC-32 are checked; one file may not expand past 1 GiB, the zip not
  past 4 GiB; paths that would leave the folder (`../`, absolute, `C:`) are refused.  Reader: `src/core/zipread.h`.
* The same safety rules as for folders: a mod with native code or scripts (`.so .dll .exe .sh .bin …`) is listed but **not loaded**
  (error code 5), a damaged texture / sound is error code 4 — see `src/core/modcheck.h`.

## For mod authors

* Mirror the game's layout or not: files are matched by path *suffix*, case-insensitive (`characters/filia_1p.dds` and
  `data/characters/filia_1p.dds` replace the same file).
* `mod.ini` keys: `name`, `version`, `author`, `description`, `priority` (higher is mounted later and wins), `enabled`, `id`.
  Two mods that hold the same file: the later one wins, and the Mods screen / `zdroid_probe` list who beats whom.
* Test a mod without the app: `zdroid_probe <game folder>` prints the mounted mods, warnings and conflicts.
* The folder is `<game folder>/mods/`.  On Android the game folder is the one chosen at setup, or the app's own folder
  `Android/data/org.bodriodroid.zgirlsmobile/files/` (the Mods screen shows the exact path).

**AOTC (Skullgirls: Ahead of the Curve)** is a resource pack: put it in `resourcepacks/` (folder or `.zip`).  With a `changes.lua` it loads in
rules mode, without one by name (see above).  Not tested against a real copy yet; anything that needs the original loader's native code
is refused by design.

---

### Resumen (es)

Al arrancar se crea sola `mods/` dentro de la carpeta del juego con `pal`, `modscustom`, `stages`, `sdvoice_japanese` y
`resourcepacks`. Un mod puede ser una carpeta o un `.zip` (se descomprime solo, quitando la carpeta envoltorio). Los `.pal` se hornean a un `.dds` de paleta normal y salen como colores extra de su personaje (Tab en el
selector). Orden: juego < resourcepacks < modscustom < stages < voces japonesas. Los archivos originales nunca se tocan.

**Resource packs (AOTC es uno):** dos modos. Con `changes.lua` → solo cambia lo que dicen sus reglas (cada regla nombra un archivo del juego y
el archivo del pack que lo reemplaza; todo en memoria, el juego original no se toca). Sin `changes.lua` → automático: cada archivo del pack
reemplaza a los del juego con el mismo **nombre**. En ambos modos `.foit`, `.art` y `.art-pt` no se incluyen en un pack (eso son mods,
van a `modscustom/`). Si `changes.lua` falla, no se aplica nada de ese pack.
