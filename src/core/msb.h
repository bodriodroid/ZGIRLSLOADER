// msb.h - Reverge engine "MSB" (most-significant-byte) binary primitives.
//
// Recovered by reverse-engineering SkullGirls.exe (PE32, 2nd Encore "2872-RETAIL").
// See docs/BINARY_RE.md for the full derivation.  Summary of the wire format:
//
//   * Every scalar is stored BIG-ENDIAN.
//   * Integers are widened to 8 bytes.  The reference implementation is
//     re::`anonymous-namespace'::decode_msb_8<N>(const unsigned char*) which reads
//     exactly 8 bytes, asserts that the leading 8-N bytes are zero, and returns the
//     trailing N bytes assembled most-significant-byte first.  Confirmed templates:
//       decode_msb_8<0x4> -> uint32   (used for counts / indices)
//       decode_msb_8<0x1> -> uint8    (used for string lengths)
//   * Strings are a 3-byte (24-bit) big-endian length followed by that many raw
//     bytes.  No terminator, no padding.  This was corrected against real
//     Skullgirls data: BGMeshShape.sgs.msb stores "joint1" as 00 00 06 6a 6f 69 6e 74 31,
//     and BGMesh1Shape.sgm.msb stores "asg_labs_ground" as 00 00 0f 61 73 67 ...
//     An 8-byte length prefix would read 0x0000066a6f696e74 and blow up immediately,
//     so decode_msb_8<0x1> is NOT what the shipped reader uses for string lengths.
//   * Floats are 4 bytes, IEEE-754 big-endian.
//
// The engine names these re::msb_reader / re::binary_reader; the concrete assets are
// *.sgm.msb (sg::mesh), *.sgs.msb (re::warp::skeleton) and *.sga.msb (sg::anm).

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace zengine {
namespace msb {

// Thrown when a read would run past the end of the buffer, or when a decode_msb_8
// integer has non-zero padding bytes (which the original engine treats as a hard
// assertion failure).
struct Error {
    enum class Kind { Eof, Padding, BadLength } kind;
    std::size_t offset;
    const char* what() const;
};

class Reader {
  public:
    explicit Reader(const std::vector<uint8_t>& data) : d_(data) {}
    explicit Reader(std::vector<uint8_t>&& data) : d_(std::move(data)) {}

    std::size_t offset() const { return p_; }
    std::size_t size() const { return d_.size(); }
    std::size_t remaining() const { return d_.size() - p_; }
    bool eof() const { return p_ >= d_.size(); }
    void seek(std::size_t off);
    void skip(std::size_t n) { seek(p_ + n); }
    const std::vector<uint8_t>& bytes() const { return d_; }

    // decode_msb_8<0x8>: full 64-bit big-endian value.
    uint64_t u64();
    // decode_msb_8<0x4>: 8 bytes on disk, top 4 must be zero.
    uint32_t u32();
    // decode_msb_8<0x2>: 8 bytes on disk, top 6 must be zero.
    uint16_t u16();
    // decode_msb_8<0x1>: 8 bytes on disk, top 7 must be zero (string lengths).
    uint8_t u8();

    int32_t i32() { return static_cast<int32_t>(u32()); }
    int64_t i64() { return static_cast<int64_t>(u64()); }

    // 4-byte IEEE-754 big-endian.
    float f32();
    float f32le();  // little-endian variant, used by a few non-msb sidecar blobs

    std::string str();                 // 8-byte length prefix + raw bytes
    std::string strFixed(std::size_t n);  // exactly n raw bytes, trailing NULs trimmed

    std::vector<float> f32Array(std::size_t n);
    std::vector<uint8_t> raw(std::size_t n);

    // Peek the next length-prefixed string without consuming it.
    bool peekString(std::size_t at, std::string* out) const;

  private:
    void need(std::size_t n) const;
    const std::vector<uint8_t> d_;
    std::size_t p_ = 0;
};

// Byte-order helpers exposed for tests and for the little-endian sidecar formats.
uint16_t swap16(uint16_t v);
uint32_t swap32(uint32_t v);
uint64_t swap64(uint64_t v);
float swapf32(float v);

}  // namespace msb
}  // namespace zengine
