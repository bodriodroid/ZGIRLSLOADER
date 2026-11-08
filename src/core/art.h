// sg_loader core - sprite data: <char>.foit + <char>.art + <char>.art-pt  ->  index planes -> RGBA.
//
// This is a C++ port of the format knowledge in the user's own tools/reference/sg_extract.py
// (record scan, RLE, 16x16 block placement, .dds palettes, palette-gradient colour math).
// tests/ cross-checks this code byte-for-byte against that script.
//
// A frame is two 8-bit planes of (cells_w*16) x (cells_h*16) pixels:
//   band : palette row (0 = outline layer)       grad : bits 7..5 = line darkening 0..7, bits 4..0 = tone*2
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace zengine {

// Record tag byte. Real data (annie, 1032 records) shows four values: 0x3A, 0xBA (what sg_extract.py accepts)
// and also 0x7A and 0xFA (175 more records with the identical layout; all of them validate). So the tag is
//   bits 0-5 == 0x3A  (record signature)  |  bit 7 = silhouette layer  |  bit 6 = "tone rows interpolated": the grad plane
//   of such a record stores the tone bits of the even rows only (see interpolateOddToneRows; found in the Sprite Workshop
//   decoder, it is what caused the black horizontal bars when these records were drawn as stored).
constexpr uint8_t kTagColor = 0x3A;
constexpr uint8_t kTagSilhouette = 0xBA;
constexpr uint8_t kTagSignatureMask = 0x3F;
constexpr uint8_t kTagSilhouetteBit = 0x80;
constexpr uint8_t kTagFlagBit = 0x40;

// RLE: 00-7F n+1 literals | 80-FC repeat 1 byte (c&7F)+1 | FD n V | FE u16be V | FF u24be V
// Decodes src[start,end) and succeeds only if the stream ends exactly at `end` and never exceeds `limit` bytes.
bool rleDecode(const uint8_t* src, size_t srcLen, size_t start, size_t end, size_t limit, std::vector<uint8_t>& out);

struct ArtRecord {
    enum class Src : uint8_t { Art, ArtPt };
    enum class Category : uint8_t { Sprite, Extra, Silhouette };
    Src src = Src::Art;
    uint8_t tag = 0;
    uint16_t w = 0, h = 0;  // size in 16x16 cells
    uint16_t n = 0;         // cells actually stored
    std::vector<uint8_t> mask;  // bit j (LSB first, row-major) set = cell j stored
    uint32_t A = 0, B = 0;  // two u32 before the record (probable pivot, meaning not confirmed)
    uint32_t foitOff = 0;   // offset of the 0x41 marker inside the .foit
    uint32_t off = 0;       // offset of the compressed data inside .art / .art-pt
    uint32_t sizeNormal = 0, sizeGrad = 0, sizeBand = 0;  // compressed sizes of the three planes
    int pixelWidth() const { return w * 16; }
    int pixelHeight() const { return h * 16; }
    uint32_t compressedSize() const { return sizeNormal + sizeGrad + sizeBand; }
    bool silhouette() const { return (tag & kTagSilhouetteBit) != 0; }
    bool flagged() const { return (tag & kTagFlagBit) != 0; }  // 0x7A / 0xFA
    Category category() const {
        return src == Src::ArtPt ? Category::Extra : (silhouette() ? Category::Silhouette : Category::Sprite);
    }
};

struct ArtPlanes {
    int width = 0, height = 0;
    std::vector<uint8_t> band;  // width*height
    std::vector<uint8_t> grad;  // width*height
};

// Rebuilds the tone bits (4..0) of rows 1,3,..,13 of every 16-row tile of a flagged record's grad strips as the average of the
// rows above and below (bits 7..5, the line level, are kept).  `sw` = strip width in bytes = n*16.  decode() calls it for
// records with flagged(); exposed for tests.
void interpolateOddToneRows(std::vector<uint8_t>& grad, size_t sw);

// Assembles `n` stored 16x16 cells (strips of 16 px wide per cell, as produced by rleDecode) into full planes of
// (w*16) x (h*16) px.  mask bit j (LSB first, row-major) = cell j is stored.  Empty cells stay 0.
void placeCells(int w, int h, int n, const uint8_t* mask, size_t maskLen, const std::vector<uint8_t>& grad,
                const std::vector<uint8_t>& band, ArtPlanes& out);

// Holds one character's files in memory and the validated record list.
class ArtFile {
public:
    // Scans the .foit for image records, validating each one by decoding its three planes
    // (exactly the heuristic of sg_extract.find_records). `artPt` may be empty.
    bool load(std::vector<uint8_t> foit, std::vector<uint8_t> art, std::vector<uint8_t> artPt);

    const std::vector<ArtRecord>& records() const { return records_; }
    // Decodes the band/grad planes of record i and assembles the full frame (empty cells = 0).
    bool decode(size_t i, ArtPlanes& out) const;

    bool hasArt() const { return !art_.empty(); }
    bool hasArtPt() const { return !artPt_.empty(); }
    size_t artSize() const { return art_.size(); }
    size_t artPtSize() const { return artPt_.size(); }
    // Fraction of each data file covered by valid records (sanity metric printed by the probe tool).
    double coverageArt() const;
    double coveragePt() const;

private:
    std::vector<uint8_t> foit_, art_, artPt_;
    std::vector<ArtRecord> records_;
};

// ---- palettes (.dds, uncompressed 32 bpp) ------------------------------------------------
struct Palette {
    int tones = 0;  // columns (16 in the game)
    int bands = 0;  // rows
    std::vector<uint8_t> rgb;  // bands*tones*3
    const uint8_t* at(int band, int tone) const { return &rgb[(static_cast<size_t>(band) * tones + tone) * 3]; }
};
bool readDds(const uint8_t* d, size_t size, Palette& out, std::string* err = nullptr);
// Same fallback palette sg_extract uses when no .dds is found (for previews only).
Palette fallbackPalette();

// Colour of (band, grad) as in sg_extract.key_rgb: tone interpolation then line darkening.
void keyRgb(const Palette& pal, int band, int g, double out[3]);

// 256 bands x 4 channels (R,G,B,A) x 256 grad values, index ((band*4+ch)*256+g). Band 0 = outline (black, alpha=line/7).
std::vector<uint8_t> buildRgbaTables(const Palette& pal);
std::vector<uint8_t> toRgba(const ArtPlanes& planes, const std::vector<uint8_t>& tables);

// Dependency-free PNG writer (uncompressed deflate). Returns the file bytes.
std::vector<uint8_t> encodePngRgba(const uint8_t* rgba, int w, int h);

}  // namespace zengine
