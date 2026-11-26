// art_write.h - the other direction of art.h: pixels -> the sprite data of a <char>.art.
//
// An image is stored as 16x16-pixel cells; only the cells that have something in them are kept (mask).  Each kept cell is 256 bytes per plane,
// the cells side by side in strips of 16 rows (see placeCells), and each of the three planes (normal, grad, band) is run-length coded (rleDecode).
// The planes of one image sit one behind the other in the .art; the .foit remembers where (foit::Image::off / size[]).
//   band = row of the palette texture (0 = outline layer; band 0 with grad 0 draws nothing)    grad = line level (bits 7..5) | tone * 2 (bits 4..0)
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

#include "foit.h"

namespace zengine {

// The run-length code of art.cpp's rleDecode (literals 00-7F, repeats 80-FC / FD / FE / FF).  rleDecode(rleEncode(x)) == x.
std::vector<uint8_t> rleEncode(const uint8_t* data, size_t n);

// Encodes a width x height image (band/grad planes, width*height each) and appends its three RLE planes to `art`.
// out gets the record the .foit needs (cells, w, h, n, mask, off, size[], flags 0x30 = data + external).  A picture with nothing in it gives
// out.n == 0 and appends nothing (a placeholder).  The normal plane is written flat ("straight on").  false only for bad sizes.
bool encodeArtImage(int width, int height, const std::vector<uint8_t>& band, const std::vector<uint8_t>& grad, std::vector<uint8_t>& art,
                    foit::Image& out);

// A palette texture the engine reads (readDds): uncompressed A8R8G8B8, `tones` columns x `bands` rows; rgba = bands*tones*4 (R,G,B,A).
// Alpha 255 = the colour takes the stage's light.
std::vector<uint8_t> makePaletteDds(int tones, int bands, const std::vector<uint8_t>& rgba);

}  // namespace zengine
