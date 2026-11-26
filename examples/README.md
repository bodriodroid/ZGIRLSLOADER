# Examples: how to lay out a pack or a mod

Copy a folder from here into your game's `mods/` folder (`resourcepacks/` or `modscustom/`), edit it, and check it with
`zdroid_probe <game folder>`.  Nothing here carries game art or code of anyone else: the files are tiny text placeholders that show the
layout.  The entries named `EXAMPLE_*` are placeholders: do not leave them in a game you play.  Rules and formats: `docs/MODS.md`.

| folder | what it shows |
|---|---|
| `resourcepacks/Example-Pack/` | a pack that is **mixed** into the game instead of replacing files: `match = path`, `[merge]`, `ignore`, `requires` |
| `resourcepacks/Example-Restoration/` | the layout of a restoration pack (what Ahead of the Curve does): `changes.lua` + `filelist.txt` + files kept as `<archive>.gfs/<path in the game>` |
| `modscustom/Example-Character/` | how a character is stored and altered: recolours (`.pal`, palette texture), `.foit` / `.art` / `.art-pt` |
| `mugen/moves.example.ini` | the `--moves` file of `zdroid_mugen`: which MUGEN action plays where, speeds, attacks |

## Where things go (one line each)

```
<game folder>/mods/
  pal/Filia/MyColour.pal               recolour from the palette editor: shows up after the game's 1p..30p colours (Tab in character select)
  resourcepacks/MyPack/                art / sound / text replacements, mixed or by path   (game < packs)
  modscustom/Filia-Alt/                a character mod: animations, frames, sprites         (game < packs < mods)
```

## The no-mixing-up rule

By default an automatic pack's file replaces **every** game file with that name, in any folder: a `dx1.dds` in two stage folders would both become
the pack's.  Put `match = path` in `pack.ini` and the folder counts: `stages/b/dx1.dds` only touches the game's `.../stages/b/dx1.dds`; a path
that stands for two game files is skipped with a warning.  A `changes.lua` is exact by nature (every rule names one game path).

## Moving a MUGEN character over

A MUGEN character is a `.def` + `.sff` (sprites) + `.air` (animations) + `.cmd` / `.cns` (moves).  The game's own character is a `.foit`
(animations, frames, hitboxes, the script) + `.art` (the sprite banks) + palette textures.  `zdroid_mugen` converts one into the other
(sprites, palettes, animations and, with a small `--moves` file, a playable template: `examples/mugen/moves.example.ini`; see `docs/MODS.md`).
The loader only needs the finished `.foit` / `.art` in a mod folder like `modscustom/Example-Character/`.
