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

### Converting a MUGEN character: `zdroid_mugen`

`zdroid_mugen <character folder> <out folder> [--scale N] [--preview] [--moves <file>]` reads a MUGEN 1.x character (`.def`, `.sff` v1, the `.act` palettes,
`.air`) and writes `<name>.art`, `<name>.foit`, one palette texture `<name>_<N>p.dds` per `.act`, PNG previews and a `manifest.json`.

* **Sprites** are converted losslessly: a sprite's MUGEN palette index becomes the *band* (the row of the palette texture) and the grad plane
  stays 0, so swapping palettes is exact.  `--scale N` enlarges the pixel art N times first (nearest neighbour; MUGEN sprites are small next to
  the game's characters) and scales axes, offsets and boxes with it.
* **The `.foit`** (`src/core/mugen_foit.*`, written by `src/core/foit_write.*`) has one frame per distinct `.air` frame (the sprite in image slot 0
  at the `.air` offset minus the sprite's axis; a mirrored copy of the sprite for `H` / `V` flips; clsn2 -> hurt boxes, clsn1 -> hit boxes) and
  one animation state per action: `Stand` is action 0, the others `Action_<id>`, and an action that loops from a later frame gets an extra
  `<name>_Loop` state.  `-1` (hold forever) becomes 65535 ticks.  Without `--moves` there are no commands, no transitions and no move scripts:
  MUGEN's CNS / CMD is a different language from the game's slot scripts, so that character animates but does not play.
* **`--moves <file>`: a playable template** (`src/core/mugen_moves.*`).  The CNS / CMD logic is *not* translated; instead the character's own
  animations are put into a fixed set of gameplay states whose scripts, transitions and hit data use the slots and ops of the engine's own
  runtime: `Stand`, `WalkFwd`, `WalkBack`, `Crouch`, `JumpStart_N/F/B` -> `Jump_N/F/B` -> `JumpAir_N/F/B` -> `JumpLand`, `s.Hit` / `c.Hit` /
  `j.Hit` (+ `s/c/j.HitStun` loops) and one state per attack.  Which action plays where, and the numbers, come from a small text file
  (see `examples/mugen/moves.example.ini`; every key is optional and a move whose action is missing is skipped and reported):

  ```ini
  [Moves]                         ; action numbers of the .air
  stand = 0   walkfwd = 20   walkback = 21   crouch = 11
  jumpstart = 40   jump = 41, 42, 43   land = 47      ; jump: neutral, forward, back
  hit = 5000   crouchhit = 5010   airhit = 5020
  walkspeed = 6   walkbackspeed = 4.5   jumpspeed = 32   jumpforward = 8   jumpback = 7   life = 14300
  push = 38, 40                   ; how far the body pushes the opponent (back, front); default = the stand hurt box, usually wider (arms)

  [Attack s.Jab]                  ; one section per attack; the posture decides where it can start
  posture = stand                 ; stand | crouch | air
  button = LP MP HP               ; any of LP MP HP LK MK HK
  anim = 200, 201                 ; actions played in a row; their clsn1 boxes are the hit boxes
  damage = 350  hitstop = 7  hitstun = 18  level = 3  pushback = 8
  ```

  From a standing state, a button without *down* starts a `stand` attack and *down* + button a `crouch` one; an `air` attack starts in the
  air and, if its animation ends first, the character keeps falling.  A character made this way walks, crouches, jumps, attacks and takes
  hits (checked with `zdroid_sim` against a shipped character: damage taken, the reaction states, back to `Stand`); it has **no blocking,
  knock-down, specials, supers or KO** yet.  Size matters: a hit box only connects with a hurt box at its height, so a character scaled much
  smaller than its opponent can be passed over by high attacks.
* **Playing it**: put the output in a mod folder as a *new* character (`characters/<name>.foit` + `<name>.art`, `sprites/<name>_<N>p.dds`, a
  `mod.ini`; nothing is needed in `.art-pt`, every frame is in the `.art`).  The game's Character Select is a fixed grid with five cells the
  game reserves and leaves empty (the two top corners and the bottom row): a mod's characters take them, in order, in every mode except
  Story (Story only knows the game's own characters).  The icon, the big picture, the name plates and the coin are drawn from the
  character's own standing pose and its first colour; the pose stands on the pedestal and Tab / a tap on it steps through its colours,
  exactly like a game character.  A character needs at least `<name>_1p.dds` to get a cell, and only the first five (by name) fit: the
  **Extras -> Custom Fight** screen lists every fighter of the mounted data (the game's and the mod's) with a preview of its standing pose,
  its colours, the mode (training / vs CPU / two players), the stage, and FIGHT.  The movement buttons are the match's usual touch pad.
* **Checks**: every sprite is decoded through the engine's own decoder under every palette and compared with the `.sff` (0 differing pixels
  required), then the `.foit` is parsed back and every `.air` frame is checked in it (picture, position, duration, box counts).  `--preview`
  writes what the engine draws for every node of every state into `preview/`.

`foit::write` is the exact inverse of `foit::parse`: all 20 character files of the PC build are written back byte for byte (the test
`zdroid_foit_write_tests` does that when `ZDROID_TEST_CHARS` names the folder with the game's `.foit` files).  Convert only characters you
may use; the output is derived from them, so it is not published here.

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
* **`.foit`, `.art` and `.art-pt` change how a character plays**, so automatic (by name) mode leaves them out (the Mods screen warns with
  the first names; put them in `modscustom/`).  A `changes.lua` rule that **names** one is accepted: that is how a restoration pack such as
  Ahead of the Curve carries them (checked with the real AOTC 0.18.0: its 171 files are staged and the character files replaced; the
  game files it expects are byte-identical to the 3.7.20 build).  `modscustom/` mods still sit above packs, so a custom character wins.
* The pack is checked before it is loaded like any other folder (damaged files → error 4, native code → error 5).
* The Mods screen / `zdroid_probe` show the mode of each pack.

### pack.ini: mixing packs ([merge], requires, conflicts)

By default a pack file **replaces** the game's file.  For text that wipes everything the pack does not mention, and two packs on the same
file cannot both be used.  So a pack can say how its files are **mixed** with what is already there (the game's file, or what an earlier
pack made of it — packs stack in priority order, each one on top of the result of the one before):

```ini
name = My pack
priority = 20                       ; higher loads later (default 100)
match = path                        ; files replace only the game path their folder mirrors (default: by name)
ignore = notes/*, *.psd             ; pack files that are not taken at all
requires  = aotc, other             ; not loaded unless these pack ids are on (and a warning if they load after this one)
conflicts = bigtranslation          ; not loaded while these are on

[merge]
loc = keys                          ; every *_loc.json  (this is the DEFAULT, you do not need the line)
ini = replace                       ; every .ini        (the default)
ui/config/MainMenu/challenges.ini = records ChalName
*.txt = append
```

| rule | what it does |
|---|---|
| `replace` | the pack's file takes the other one's place (what a pack always did) |
| `keys` | key by key.  `*.json`: flat `{"key": "text"}`; a key the pack has replaces that key, a new key is added, a `null` never blanks a text.  Text files: `Key: value` / `Key = value` lines; a key that repeats (`Choice:`) is replaced as a group in the place of its first line |
| `records <Key>` | for the game's `.ini` lists of blank-line separated records (`challenges.ini`, the option menus): the record whose `<Key>` line has the same value is replaced in place, a new one is added at the end |
| `append` | the pack's lines are added after the other file's |

A pattern is `*suffix`, or a path / path tail (`challenges.ini`, `MainMenu/challenges.ini`); the words `loc` and `ini` stand for
`*_loc.json` and `*.ini`.  A path pattern beats a word; among paths the first in the file wins.  No rule: `replace`, except `*_loc.json`
which is `keys`.

* **Automatic (by name) packs** replace — and mix into — **every** game file with that name, whatever its folder.  So a `dx1.dds` in two
  stage folders would both become the pack's.  To avoid that, put `match = path` in `pack.ini` (mode **path**): the pack's folders then
  count, `stages/b/dx1.dds` only touches the game's `.../stages/b/dx1.dds`.  A path that stands for two different game files (a bare
  `dx1.dds` that exists twice) is skipped with a warning, never guessed.  In path mode `.foit` / `.art` / `.art-pt` are taken too (the path
  names them).  `changes.lua` is the other exact way: its rules name the game path.
* The combined file is written to `mods/.cache/changes/<pack>/merge/`; the game's files are never written to.  If a pack file is not
  valid for its rule (not JSON for `keys`, ...) it is **not** mixed: it replaces whole, and the Mods screen / `zdroid_probe` log says so.
* `zdroid_probe` shows `mixed into` instead of `wins over` for a file two packs share that was mixed.
* The game's `.ini` files are record lists, not key/value tables, so `keys` on them is only right for single-record or flat files;
  use `records <Key>` for lists.  (Verified against the game's own `challenges.ini`: the same record in, the file comes out unchanged.)
* Not done yet: mixing of binary files (textures, palettes, `.gbs` scenes) — there the highest priority still wins whole.

### changes.lua (rules mode)

A Lua 5.1 script run once at start in a sandbox (no `require`, no `os.execute`, no `io.popen`; `io.open` / `os.remove` only reach the
pack's own folder and its scratch folder (files have `read`, `lines`, `write`, `seek`, `close`); a script that runs too long is stopped).  If it fails, **nothing** of that pack is applied (all or nothing) and
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

**AOTC (Skullgirls: Ahead of the Curve)** is a restoration pack: put it in `resourcepacks/` (folder or `.zip`) as `AOTC/` with its
`AOTC.lua` renamed to `changes.lua`, its `filelist.txt` and the `<archive>.gfs/` folders beside it.  Checked against the real 0.18.0: its script
runs unmodified in this sandbox (it reads `filelist.txt` with `io.open` + `f:lines()`), all 171 rules apply (156 textures / sounds / scenes / text
and 15 `.foit` / `.art` / `.art-pt`), and every game file it expects is byte-identical to the 3.7.20 data this loader reads.  Its own `.exe` /
`.dll` installer is not used and is refused by design.  Layout examples: `examples/` in the repository.

---

### Resumen (es)

Al arrancar se crea sola `mods/` dentro de la carpeta del juego con `pal`, `modscustom`, `stages`, `sdvoice_japanese` y
`resourcepacks`. Un mod puede ser una carpeta o un `.zip` (se descomprime solo, quitando la carpeta envoltorio). Los `.pal` se hornean a un `.dds` de paleta normal y salen como colores extra de su personaje (Tab en el
selector). Orden: juego < resourcepacks < modscustom < stages < voces japonesas. Los archivos originales nunca se tocan.

**Resource packs (AOTC es uno):** dos modos. Con `changes.lua` → solo cambia lo que dicen sus reglas (cada regla nombra un archivo del juego y
el archivo del pack que lo reemplaza; todo en memoria, el juego original no se toca). Sin `changes.lua` → automático: cada archivo del pack
reemplaza a los del juego con el mismo **nombre**. En modo automático `.foit`, `.art` y `.art-pt` no se incluyen (cambian cómo juega un personaje: van a `modscustom/`),
pero una regla de `changes.lua` que los **nombra** sí se acepta (así los lleva AOTC). `match = path` en `pack.ini` evita las
mezclas por nombre: cada archivo solo toca la ruta que replica su carpeta. `[merge]` mezcla textos (`_loc.json` clave por clave por defecto,
`.ini` por registros). Ejemplos de carpetas: `examples/`. Si `changes.lua` falla, no se aplica nada de ese pack.
