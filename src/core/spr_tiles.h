// spr_tiles.h - "tile sprite" containers (<name>.spr.msb + <name>.dds), used by the game's loading splash and effects.
//
// Recovered from the real loading_<lang>.spr.msb (all integers 8 bytes big-endian, floats 4 bytes big-endian,
// strings = 8-byte length + bytes):
//
//   string  version            "2.0"
//   string  name               "loading_en"
//   u32     (zeros)            4 bytes
//   string  tile layout        "unigned char tile_x, tile_y, tile_u, tile_v;"   (sic, the game's own typo)
//   u64     bytes per tile     4
//   u64     tile count         21477
//   u64     frame count        28
//   u64     (unknown)          2
//   u64     tile width, tile height   16, 16
//   tile[count]   4 bytes each: tile_x, tile_y = cell inside the frame, tile_u, tile_v = tile column / row in the .dds
//   frame[n]     20 bytes each, u32 BE: first tile, tile count, (flags = 4), then f32 offsetX, offsetY
//   anim[...]    string name, u32 first frame, u32 frame count, u64 last frame      (to the end of the file)
//
// A frame is the grid of its tiles: every tile is a 16x16 block of the .dds drawn at (x*16, y*16).  The offset is where
// the frame sits relative to the sprite's origin (the game places it from code; ZDroid centres it).
// Tiles are copied as they are: the .dds atlas is plain DXT, only the arrangement lives in the .msb.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace zengine {

struct SprTile {
    uint8_t x = 0, y = 0, u = 0, v = 0;
};
struct SprFrame {
    uint32_t first = 0, count = 0, flags = 0;
    float offsetX = 0, offsetY = 0;
    int cols = 0, rows = 0;  // size of the tile grid (max x + 1, max y + 1); filled by the parser
};
struct SprAnim {
    std::string name;
    uint32_t first = 0, count = 0, last = 0;
};

struct SprTiles {
    std::string name;
    int tileW = 16, tileH = 16;
    std::vector<SprTile> tiles;
    std::vector<SprFrame> frames;
    std::vector<SprAnim> anims;
    const SprAnim* anim(const std::string& n) const {
        for (const SprAnim& a : anims)
            if (a.name == n) return &a;
        return nullptr;
    }
};

// Tolerant: on any inconsistency returns false (with a reason in *err) and leaves `out` empty.
bool parseSprTiles(const std::vector<uint8_t>& data, SprTiles& out, std::string* err = nullptr);

// Draws frame `frame` into an RGBA buffer of cols*tileW x rows*tileH (transparent where no tile).  `atlas` is the
// decoded .dds (RGBA, top-down).  Tiles that point outside the atlas are skipped.
bool composeSprFrame(const SprTiles& s, size_t frame, const uint8_t* atlasRgba, int atlasW, int atlasH, std::vector<uint8_t>& rgba,
                     int& outW, int& outH);

}  // namespace zengine
