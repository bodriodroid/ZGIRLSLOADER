// imgscale.h - RGBA image scaling helpers (C++17, no platform dependencies).
//
// The Play screen draws sprites and stage pictures at the size they have on the real screen, not at the 640x360 logical size, so
// it needs three things the box filter in dds.h does not give:
//   * shrinkRgba   - the "mip" of a sprite: a power-of-two reduction done in premultiplied alpha, so the transparent border of a
//                    sprite never bleeds a dark fringe into its edge (used for the sprite quality levels);
//   * upscaleCubic2x + sharpenRgba - the "Enhanced" upscale of a stage picture that is smaller than the screen it is shown on
//                    (Catmull-Rom, then a light unsharp mask).  Nothing is invented: it is only a better interpolation than the
//                    GPU's bilinear one, done once on a worker thread while the loading screen is up.
// All images are w*h*4 bytes, top-down rows, straight (non-premultiplied) alpha - what DdsImage / toRgba() produce.
#pragma once
#include <cstdint>
#include <vector>

namespace zengine {

// Reduces by an integer `factor` (>= 1; 1 = copy).  Output size is ceil(w / factor) x ceil(h / factor); the partial boxes at the
// right / bottom edge average what they cover.  Colour is weighted by alpha (premultiplied average).
std::vector<uint8_t> shrinkRgba(const uint8_t* rgba, int w, int h, int factor, int& outW, int& outH);

// 2x enlargement with a Catmull-Rom (bicubic) kernel in premultiplied alpha; output is (2w) x (2h).  Returns {} when the input
// is empty or the output would be larger than maxPixels (default 4096 x 2304).
std::vector<uint8_t> upscaleCubic2x(const uint8_t* rgba, int w, int h, int64_t maxPixels = 4096LL * 2304LL);

// Unsharp mask on the colour channels (alpha untouched): out = in + amount * (in - blur3x3(in)).  amount 0 = no-op.
void sharpenRgba(uint8_t* rgba, int w, int h, float amount);

// Copies the colour of opaque neighbours into fully transparent pixels (`passes` pixels deep, alpha stays 0).  A sprite that is
// filtered when it is drawn would otherwise pull the (black) colour stored under its transparent border into its edge.
void bleedTransparent(uint8_t* rgba, int w, int h, int passes = 2);

// The PC engine's flat stage backdrops (stages/textures/2D/*.dds) are drawn with the LevelFlatBG program, which reads the texture
// as texture2D(...).argb: the file stores R in its alpha channel, G in red, B in green and keeps 255 in blue (which becomes the
// opaque alpha).  Decoded as plain RGBA such a picture looks blue and half transparent.  unrotateArgb() recognises it - at least
// 99.9% of the stored blue channel is 255 - and rotates it in place; returns false (image untouched) for anything else.
bool unrotateArgb(uint8_t* rgba, int w, int h);

// Which power-of-two reduction a sprite should be stored at.  `pxPerArtPx` = how many screen pixels one art pixel covers at the
// closest camera zoom.  `minLevel` is the floor the sprite-quality setting asks for (0 = no floor).  Returns 0 (full size), 1
// (half), 2 (quarter) or 3 (eighth); a level is only used while the sprite still has >= 1 texel per screen pixel.
int spriteLevelFor(double pxPerArtPx, int minLevel);

}  // namespace zengine
