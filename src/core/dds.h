// sg_loader core - .dds image decoding for the game's texture data (stages, UI, fonts).
//
// The character sprites are index planes + a palette; everything else in the game is a plain
// DirectDraw surface. Real data seen so far (743 files across Textures/, Fonts/ and the two
// 3750x2000 stage backgrounds) is DXT5, so DXT1/3/5 and the uncompressed 32/24/16-bpp layouts
// are what this supports. This is only a reader: it never writes game files.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace zengine {

enum class DdsFormat : int {
    Unknown = 0,
    DXT1,
    DXT3,
    DXT5,
    BGRA8,   // 32 bpp, masks 00FF0000/0000FF00/000000FF (+ optional alpha)
    RGBA8,   // 32 bpp, red first
    BGR8,    // 24 bpp
    RGB565,  // 16 bpp
    L8,      // 8 bpp luminance
    A8,      // 8 bpp alpha
};

struct DdsInfo {
    int width = 0, height = 0, mipLevels = 1;
    DdsFormat format = DdsFormat::Unknown;
    bool cubeMap = false;
    const char* formatName() const;
};

struct DdsImage : DdsInfo {
    std::vector<uint8_t> rgba;  // width*height*4, top-down rows, straight (non-premultiplied) alpha
    bool empty() const { return rgba.empty(); }
};

// Parses the 128-byte header (no pixel data touched).
bool ddsHeader(const uint8_t* data, size_t size, DdsInfo& out, std::string* err = nullptr);

// Decodes one mip level to RGBA8. With maxSize > 0 the smallest mip whose largest side is still
// >= maxSize is decoded instead of mip 0 (cheap previews of a 3750x2000 stage background).
bool decodeDds(const std::vector<uint8_t>& data, DdsImage& out, std::string* err = nullptr, int maxSize = 0);

// Box-filter resample; used for previews so a huge texture does not have to be blitted 1:1.
std::vector<uint8_t> resampleRgba(const uint8_t* rgba, int w, int h, int nw, int nh);

// Largest (outW,outH) <= (boxW,boxH) that keeps the aspect ratio of (w,h).
void fitInto(int w, int h, int boxW, int boxH, int& outW, int& outH);

}  // namespace zengine
