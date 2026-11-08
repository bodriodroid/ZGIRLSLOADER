// sg_loader core - reader for the game's compiled UI scene files (*.gbs, magic "CSGG").
//
// What is decoded (checked against all 23 files in the user's Win/ folder):
//   0x00 "CSGG"  0x04 total file size  0x08 float 0.95  0x0C scene id
//   0x10 u32 count0   0x14 u32 texture count   0x18 u32 count2   0x1C u32 view count
//   0x20 u32 count4 (non-zero only in files that carry a trailing section after the views)
//   0x24 u32 0        0x28/0x2C/0x30 u32 section offsets, relative to 0x38
//   0x34 u32 payload size = file size - 0x3C - count4*68       payload = [0x38, 0x38+payload size)
//   after the payload: count4 opaque 68-byte entries, then the footer
//   size-4 u32 0x12344321 footer
//   Views are `view count` consecutive "WVGG" chunks starting at 0x38 + off2:
//     [tag u32 'WVGG'][chunk size u32][parent id i32 (-1)][name char[64]] ...
// Everything inside a view (layers, nodes, animations) is NOT decoded yet and is kept as raw bytes.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace zengine {

struct GbsView {
    std::string name;
    uint32_t offset = 0;  // file offset of the WVGG chunk header
    uint32_t size = 0;    // chunk size including its 8-byte header
};

struct Gbs {
    uint32_t totalSize = 0;
    float formatVersion = 0.f;
    uint32_t sceneId = 0;
    uint32_t count0 = 0;
    uint32_t textureCount = 0;
    uint32_t count2 = 0;
    uint32_t viewCount = 0;
    uint32_t count4 = 0;
    uint32_t off0 = 0, off1 = 0, off2 = 0;  // payload-relative section starts
    uint32_t payloadSize = 0;
    std::vector<GbsView> views;
    uint32_t tailOffset = 0;  // the count4*68 opaque bytes between the payload and the footer
    uint32_t tailSize = 0;
    // Best effort: path-like strings ending in an image extension found before the views.
    std::vector<std::string> textureNames;
};

constexpr uint32_t kGbsMagic = 0x47475343;   // "CSGG" little-endian
constexpr uint32_t kGbsFooter = 0x12344321;
constexpr uint32_t kGbsTailEntrySize = 68;  // size of each count4 entry in the trailing section
constexpr uint32_t kGbsViewTag = 0x47475657; // "WVGG"

// Parses and validates. On failure returns false and fills *err (if given).
bool parseGbs(const uint8_t* data, size_t size, Gbs& out, std::string* err = nullptr);

}  // namespace zengine
