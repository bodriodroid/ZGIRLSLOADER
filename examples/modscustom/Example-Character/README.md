# Example-Character: how a character is stored and altered

A character mod mirrors the game's layout.  For Filia the game files are:

```
characters/filia.foit          animations, frames, hitboxes, the script            (changes how she plays / what frames exist)
characters/filia.art           the sprite banks                                    (changes how she looks)
characters/filia.art-pt        the second sprite bank: entrances, portraits ...    (not every character has one)
characters/filia_alt1.snd-wav  her voice bank
sprites/filia_1p.dds ... _30p  the palette textures: 16 tones x (1 + regions), one file per colour
```

## 1. A recolour with no files of the game: a `.pal`  (the easy way)

Make a `.pal` with the community palette editor (or `zdroid_palgen`) and put it in `mods/pal/Filia/MyColour.pal`.  It appears after the game's
`1p..30p` colours, labelled `mod:MyColour`.  Nothing else is needed; the `.pal` holds colours only.

## 2. A recolour as a palette texture

Copy the game's `sprites/filia_1p.dds`, repaint its 16-tone rows, and save it under the name of the colour it should replace:

```
modscustom/Filia-Recolor/
  mod.ini
  sprites/filia_5p.dds         replaces colour 5 of Filia
```

## 3. Changed frames or animations

```
modscustom/Filia-Alt/
  mod.ini
  characters/filia.foit        your edited copy
  characters/filia.art         the sprite banks that go with that .foit
  characters/filia.art-pt      only if you changed it
```

A `.foit` and the `.art` it points to are a pair: ship both from the same source.  A mod sits above the resource packs, so it wins over a
restoration pack that also carries `characters/filia.foit`.  `zdroid_probe <game folder>` lists who beats whom.

## 4. A MUGEN character

See the end of `examples/README.md`: convert it to `.foit` + `.art` first (a separate tool), then use the layout of section 3 with the new
character's name.
