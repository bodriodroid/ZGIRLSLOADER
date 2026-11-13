# Palette effects (PALFX) - the colour washes of a counter hit, a status, a hit flash

A fighter's script does not name a colour effect.  It keeps the **32-bit hash of an effect's name** in one of four values of the object, and
the picture looks the effect up in the game's table of effects and plays it, one frame per tick.  This page describes what the loader does
(`src/core/palfx.*`, `tests/test_palfx.cpp`); it is written from the table's own header, from measurements on a running fight and from the
game's shaders as they ship in the Core.  No code or address of the game's executable is used or reproduced.

## The four values

| slot (`PALFX*`) | layer | meaning |
|---|---|---|
| `PALFXSTATUS` (79) | status | a long-lived state (a guard, a buff) |
| `PALFXSELF` (73)   | self   | the wash the object wears for itself (counter hit, a charge ...) |
| `PALFXSELF2` (75)  | self2  | a second wash that applies only to the palette indices of its `MASK` |
| `PALFXHIT` (77)    | hit    | a wash on being hit |

The slots hold the raw 32 bits of the hash (scripts copy them around, so they are plain bit patterns in a float slot).  `0` = nothing.
The `*TIME` slots and `PALFXSELF2RATE` are not read yet.

## The hash

`h = 5381; for each character c of the name: h = toupper(c) ^ (h * 33)`, 32-bit, case-insensitive.  Checked against the values a fight
raised: `CounterHit` = `0x5bcff524`, `HatredGuard` = `0x2ec8226e`, `AnnieStarCancel` = `0x138b331a`, `Beo_HypeLv1` = `0xc8efe39d`,
`AnnieFlame` = `0xaecd29ab`.  The 83 effect names of the shipped table have no collisions.

## The table (`characters/_palette_effects.ini`, the player's own data)

```
Name
NoLight                      # optional: the wash ignores the stage lighting
MASK 25 26                   # optional: palette indices the colour pair applies to (the self2 layer)
[high-colour  low-colour] strength [HiMult a/b] [LoMult a/b] [Loop]
...
END
```

* A colour is `R G B` (0..255, may be negative or above 255), `PAL n` (entry n of the character's palette) or `NORMAL` (the character's
  own colour: no wash at that end).  Numbers may be written `a/b`.
* `strength` is 0..255 (0 = the normal colour, 1 = this colour); with `NORMAL` first and no strength it defaults to 0, otherwise to 255.
* `HiMult` / `LoMult` scale the strength at the high / low end (default 1; negative or above 1 are allowed - "interpolates even more
  toward this colour").  **This is the one reading that could not be confirmed from the shader**: the shader blends with one strength per
  object, so how the two multipliers reach it is an interpretation (strength at each end = `strength * mult`).  The fade-out sequences of the
  table (`LoMult -1`, `-1/2`, `-1/4`, `0`) only make sense read this way.
* `Loop` on a frame: playback returns to that frame after the last one.  Without it the effect ends after its last frame (and stays ended
  while the slot still holds the same hash; a different hash, or 0 and then the hash again, restarts it).

## What the shader does with a frame

The Core's sprite shaders blend per pixel: `wash = clamp(mix(lowColour, highColour, intensity))`, then `out = mix(sprite, wash, strength * (1 -
lighting))` unless `NoLight`.  `intensity` is the pixel's own tone.  Pixels whose palette index is in the `MASK` use the second colour pair
instead (the self2 layer) and the rest use the first (status / self / hit, the topmost active one).

## What the loader draws

* **CPU route** (default): the CPU cannot vary the wash by pixel, so the whole sprite is washed toward the middle of the two ends: a colour
  multiply `(1 - a) + a * colour`, plus an additive pass for the brighter colours.  The layers status, self, hit are laid over each other;
  a masked self2 is not drawn.  `PAL n` colours are band n of the fighter's palette at the middle tone.
* **GPU route** (option "GPU effects", off by default): not wired yet; it needs the two colour pairs, the strengths and the mask as
  uniforms of the effect programs (`Palfx::primary()` / `secondary()` already return exactly that).

Capture aid: `ZDROID_PLAY_PALFXLOG=1` (or `palfxlog` in `zgirls_flags.txt`) prints when an object's effect starts.
