# Third-party notices

| Component | Where | License |
|---|---|---|
| **Oswald** (variable font, weight 500 rasterised into `src/app/credits_font.h` by `tools/gen_credits_font.py`) | `tools/fonts/Oswald-VariableFont_wght.ttf` | SIL Open Font License 1.1 - full text in `tools/fonts/OFL.txt` (it has to travel with the font and with anything derived from it) |
| **SDL2** (linked, not copied) | built from a checkout passed with `-DZDROID_SDL2_SOURCE_DIR` / `-PsdlDir` | zlib license |
| **font8x8** public-domain glyph bitmaps (debug/fallback text) | `src/app/font8x8_basic.h` | Public domain |
| **Gradle wrapper** | `android/gradle/wrapper/` | Apache License 2.0 |

**OtterUI** (MIT) is the scene library the game's `.gbs` files belong to. Its source was read only to check how scenes are evaluated
(animation easing, pivots, text layout); no OtterUI code or data is part of this repository - the readers in `src/core/gbs_*.cpp` are an
independent implementation. The comparison harness is not distributed.

Skullgirls and everything in the game's data belong to Lab Zero Games and their respective authors; none of it is part of
this repository.
