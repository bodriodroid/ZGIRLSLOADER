# No Limits Screen

A developer option (Settings -> Developer, off by default). In a fight the picture takes the shape of the phone's screen instead of the
fixed 16:9 (640x360) picture with black bars. Nothing is stretched and the phone is not rotated.

## How it works

* The picture is drawn in logical units. The height stays **360**; the width is `360 * screenWidth / screenHeight`, rounded to an even
  number and clamped to **640..960** (`src/core/screen_layout.*`, `layout::compute`). A screen that is not wider than 16:9 keeps 640x360.
* Only while a match is on screen (`PlayScreen::wantsWide()`): menus, loading and every other screen stay 640x360. The viewer sets the
  renderer's logical size each frame and tells the match with `setViewWidth()`.
* **World** (stage backdrop, generated stage, floor, shadows, fighters, camera): uses the whole width. The camera never shows beyond the stage:
  when the stage is narrower than the picture it zooms in just enough.
* **HUD, round banners, K.O. / time-up, pause / options / help menus**: designed for 640x360. They are drawn into a transparent target the size of
  that area and copied to the middle (`beginOverlay()` / `endOverlay()`); the pointer is expressed in 640-space while they are drawn, so their hit
  tests are unchanged. The outermost column is stretched over the side bands, so full-width bars and the dimming behind a menu reach the edges.
* **Touch pad**: `touch::Layout::widened()` moves the buttons of the right half to the real right edge; the stick and the left side stay.
  The player's own `touch_layout.txt` is the 16:9 base and is widened the same way.  A left-handed layout (Extras -> Touch Controls -> Swap hand) has its stick on the right: the stick and its zone go to the real right edge, the buttons on the left stay.
* **Touch input**: SDL already normalises finger positions to the logical picture, so the viewer multiplies by the current logical width.

## Checking it headless

```
ZDROID_DEV_NOLIMITS=On ZDROID_PLAY_DEMO=1 ZDROID_PLAY_SYNCLOAD=1 ZDROID_PLAY_NOINTRO=1 ZDROID_PLAY_PAD=1 \
  zdroid_viewer --smoke out.png --smoke-width 800 --smoke-scale 2 --screen play --fight annie:beowulf: <data dirs>
```

`ZDROID_PLAY_OVERLAY=pause` captures the pause menu. `zdroid_layout_tests` covers the size formula.

## Not done / not verified

* Not run on a real phone (touch mapping, notches and cut-outs are untested; the safe area is the centred 16:9 part, not the display cut-out).
* The software renderer cannot do the premultiplied blend used to lay the 640x360 target over the picture and falls back to plain alpha
  blending (semi-transparent edges come out slightly darker); the GL / GLES renderers use the premultiplied blend.
* On the generated test stage (no real `.lvl` mounted) a platform piece at the left shows cut at the screen edge in the 800-wide capture; not investigated.
