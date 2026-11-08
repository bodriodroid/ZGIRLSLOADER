> The newer `<game folder>/mods/` layout (palettes, stages, resource packs, voices) is described in [MODS.md](MODS.md).

# SKGLoader mod loader

ZDroid-Engine loads mods from a folder called **`SKGLoader`** that sits next to the
game data.  There is no archive format and no build step: a mod is a folder with a
`mod.ini`, and any file inside it shadows the file of the same name in the game.

```
<gameDir>/                      <- whatever you pointed the loader at
  data/characters/filia.foit    <- original
  SKGLoader/
    mods.ini                    <- optional global switch
    mods/
      hd-filia/
        mod.ini
        data/characters/filia.foit      <- wins over the original
        data/characters/filia_1p.dds
      stage-swap/
        mod.ini
        stages/asg_labs_3d.lvl
```

`SKGLoader/mods/hd-filia/data/characters/filia.foit` replaces the game's
`filia.foit`.  The mirror path does not have to be exact: the VFS indexes every
file by **path suffix, case-insensitively**, so `data/characters/filia.foit`,
`characters/filia.foit` and even a bare `filia.foit` all override the same file.

## mod.ini

Flat `key = value` text (no JSON), so it can be edited on a phone:

```ini
# ZDroid-Engine mod descriptor - edit with any text editor
id = hd-filia
name = HD Filia
version = 0.3
author = you
description = sharper portraits, recoloured palette 3
priority = 100
enabled = 1
```

| key | meaning |
|---|---|
| `id` | folder name is used when this is missing |
| `name` | display name |
| `version`, `author`, `description` | free-form |
| `priority` | **higher wins** when two mods replace the same file (default `100`) |
| `enabled` | `1` / `0` (also accepts `false`, `no`, `off`) |

## SKGLoader/mods.ini

Optional global switch:

```ini
enabled = 1
disabled = broken-mod, old-portraits
```

`enabled = 0` turns everything off without touching any `mod.ini` (useful when the
game stops loading and you need a clean baseline).  `disabled` is a comma-separated
blacklist.

## Load order

1. every game folder you mounted, in order
2. enabled mods sorted by `priority` ascending, then `id`

Later mounts override earlier ones, so a high-priority mod beats a low-priority one
beating the base game.

## Command line

```sh
zdroid_mods --list    <gameDir>                # what is installed and what wins
zdroid_mods --create  <gameDir> <id> [name]    # scaffold mod.ini + README + data/
zdroid_mods --enable  <gameDir> <id>
zdroid_mods --disable <gameDir> <id>
zdroid_mods --resolve <gameDir> filia.foit     # which real file will be loaded
```

`--resolve` prints both the winning file and whether it came from the base game or
from a mod, which is the fastest way to debug "my texture is not changing".

## On Android

The APK is a plain SDL app with file access, so the same folders apply.  With the
game unpacked under `/sdcard/sg_dump/unpacked`, put mods in
`/sdcard/sg_dump/unpacked/SKGLoader/mods/<id>/`.  The loader mounts them
automatically when it mounts the data root (`GameData::mountMods`).

## Current limitations

* File-level replacement only.  Patching the *inside* of a `.foit` / `.gfs` needs
  the container formats to be fully reversible, which they are not yet
  (see `docs/BINARY_RE.md`).
* No dependency or conflict declaration between mods; `priority` is the only
  arbitration mechanism.
* Mods are not signed or sandboxed.  A mod can shadow any file the loader can read.
