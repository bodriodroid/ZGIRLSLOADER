# Asset supportability (what the engine can read today)

Checked by running the built tools on the actual files, not by reading the format names off the
extensions.  Commands used:

```
build/zdroid_probe   <folder>              # mounts + inventories a folder
build/zdroid_dds     <file.dds> <out.png>  # decodes any DDS
build/zdroid_msb     --dir <folder>        # parses every .msb in a folder
build/zdroid_viewer  <folder> --smoke out.png --screen play
```

## Summary

| asset | files | readable | used by |
|-------|-------|----------|---------|
| `*.lvl` stage definitions (stages.zip) | 34 | **yes** - new `src/core/level.{h,cpp}` | Play screen: bounds, spawn points, ambient tint, `2D <texture>` backdrop |
| stage `*.dds` backdrops (textures.zip) | 13 | **yes** - existing DXT1/3/5 decoder | Play screen backdrop (verified: `asg_labs_bg.dds`, DXT5) |
| stage `*.msb` mesh/skeleton/anim (asg_labs_3d.zip) | 46 | **yes** - existing `msb.cpp` + `zdroid_msb` | inspected only; a 2D renderer has nothing to draw them with |
| UI `*.dds` (Textures.zip, ui.zip `true_color_hud/`) | 744 | **yes** - same decoder | decoded / exported to PNG; not yet skinned into the UI |
| font atlases `*.dds` (Fonts.zip) | 54 | **image only** - no glyph metrics in the archive | not usable as text yet (see below) |
| `config/*.ini` (ui.zip) | ~127 | indexed, not parsed | different per-file formats; only stages/music/toc/compile/stm are parsed today |

## Details

### `.lvl` - stage definitions (supported, new)

`StageSize: 3750 2000`, `BottomClearance`, `Start1/Start2`, `ShadowDist`, `Light: Amb r g b`,
`2D <texture path>`, `CAMERA <fov>`. All 34 supplied stages parse; the two spawn points are converted
into the fight runtime's centred coordinate space (`toFightX`), so the fighters no longer spawn at
hand-picked positions. `zdroid_probe` prints one line per stage.

Lines are `Key: value` **or** `Key value`; `#` comments and the 3D-only blocks
(`3D`, `ForceReplace`, `Replace*`) are skipped.

### Stage and UI `.dds` (supported)

`decodeDds` handles DXT1/2/3/4/5 and uncompressed BGRA8/RGBA8/BGR8/RGB565/L8/A8, with mip selection.
Verified on `asg_labs_bg.dds` (DXT5), `Arcade_Buttons_LP_01.dds` (DXT5 96x96),
`true_color_hud/dead_annie.dds` (DXT1 720x720).

The Play screen looks the backdrop up through the VFS: the texture named by the `.lvl`, then
`<stage>_bg.dds`, then `stage_<stage>.dds`, then `<stage>.dds` - so a partially extracted data folder
still gets its background.

### `.msb` - stage meshes / skeletons / animations (readable)

`zdroid_msb --dir asg_labs_3d` parses all 46 files: `.sgm.msb` meshes (version 2.0, vertex
declarations, joint indices), `.sgs.msb` skeletons (joints + parent + matrix), `.sga.msb` animations.
Nothing renders them: the Play screen is a 2D sprite renderer, and drawing these would need a 3D path.
(Note, 26-oct: the stage `.msb` files are not in the data folders used for the current work, so this parse has not been re-verified; the readers cover the container, skeleton and animation tracks only - there is no vertex / index decoder for `.sgm.msb` yet.)

### Font atlases (images only - blocked on metrics)

`Fonts.zip` holds 54 `.dds` atlases and nothing else - no `.fnt`, no glyph-rect table, no advances,
and `ui.zip` has no font metrics either. They decode fine as pictures
(`Big Options w Wings_0.dds` = DXT5 1536x1536, `ComboText_0.dds` = DXT5 768x768), but without a
metrics file there is no way to know where each glyph is, so the game's own typography cannot be
reproduced yet. The viewer keeps using the public-domain 8x8 font.

To unblock: supply the file that maps glyph -> rectangle (the game's font definition, whatever
extension it uses) and text can be drawn from these atlases.
