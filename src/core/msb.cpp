#include "msb.h"

#include <cstring>

namespace zengine {
namespace msb {

const char* Error::what() const {
    switch (kind) {
        case Kind::Eof: return "msb: read past end of buffer";
        case Kind::Padding: return "msb: non-zero padding byte in decode_msb_8 value";
        case Kind::BadLength: return "msb: string length exceeds buffer";
    }
    return "msb: error";
}

uint16_t swap16(uint16_t v) { return static_cast<uint16_t>((v >> 8) | (v << 8)); }

uint32_t swap32(uint32_t v) {
    return ((v >> 24) & 0x000000FFu) | ((v >> 8) & 0x0000FF00u) | ((v << 8) & 0x00FF0000u) |
           ((v << 24) & 0xFF000000u);
}

uint64_t swap64(uint64_t v) {
    return ((v >> 56) & 0x00000000000000FFull) | ((v >> 40) & 0x000000000000FF00ull) |
           ((v >> 24) & 0x0000000000FF0000ull) | ((v >> 8) & 0x00000000FF000000ull) |
           ((v << 8) & 0x000000FF00000000ull) | ((v << 24) & 0x0000FF0000000000ull) |
           ((v << 40) & 0x00FF000000000000ull) | ((v << 56) & 0xFF00000000000000ull);
}

float swapf32(float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, 4);
    bits = swap32(bits);
    float out;
    std::memcpy(&out, &bits, 4);
    return out;
}

void Reader::need(std::size_t n) const {
    if (n > d_.size() - p_) throw Error{Error::Kind::Eof, p_};
}

void Reader::seek(std::size_t off) {
    if (off > d_.size()) throw Error{Error::Kind::Eof, off};
    p_ = off;
}

uint64_t Reader::u64() {
    need(8);
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8) | d_[p_ + i];
    p_ += 8;
    return v;
}

// decode_msb_8<N>: 8 bytes on disk, the leading (8-N) must be zero.
static uint64_t decodeMsbN(Reader& r, int n) {
    const std::size_t at = r.offset();
    const uint64_t v = r.u64();
    const int pad = 8 - n;
    if (pad > 0 && (v >> (8 * n)) != 0) throw Error{Error::Kind::Padding, at};
    return v;
}

uint32_t Reader::u32() { return static_cast<uint32_t>(decodeMsbN(*this, 4)); }
uint16_t Reader::u16() { return static_cast<uint16_t>(decodeMsbN(*this, 2)); }
uint8_t Reader::u8() { return static_cast<uint8_t>(decodeMsbN(*this, 1)); }

float Reader::f32() {
    need(4);
    uint32_t bits = 0;
    for (int i = 0; i < 4; ++i) bits = (bits << 8) | d_[p_ + i];
    p_ += 4;
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

float Reader::f32le() {
    need(4);
    uint32_t bits = 0;
    for (int i = 3; i >= 0; --i) bits = (bits << 8) | d_[p_ + i];
    p_ += 4;
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

std::string Reader::str() {
    const std::size_t at = p_;
    need(3);
    const std::size_t n = (static_cast<std::size_t>(d_[p_]) << 16) |
                          (static_cast<std::size_t>(d_[p_ + 1]) << 8) |
                          static_cast<std::size_t>(d_[p_ + 2]);
    p_ += 3;
    if (n > d_.size() - p_) throw Error{Error::Kind::BadLength, at};
    std::string s(reinterpret_cast<const char*>(d_.data() + p_), n);
    p_ += n;
    return s;
}

std::string Reader::strFixed(std::size_t n) {
    need(n);
    std::string s(reinterpret_cast<const char*>(d_.data() + p_), n);
    p_ += n;
    while (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

std::vector<float> Reader::f32Array(std::size_t n) {
    std::vector<float> out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) out.push_back(f32());
    return out;
}

std::vector<uint8_t> Reader::raw(std::size_t n) {
    need(n);
    std::vector<uint8_t> out(d_.begin() + static_cast<long>(p_),
                             d_.begin() + static_cast<long>(p_ + n));
    p_ += n;
    return out;
}

bool Reader::peekString(std::size_t at, std::string* out) const {
    if (at + 3 > d_.size()) return false;
    const std::size_t n = (static_cast<std::size_t>(d_[at]) << 16) |
                          (static_cast<std::size_t>(d_[at + 1]) << 8) |
                          static_cast<std::size_t>(d_[at + 2]);
    if (n == 0 || at + 3 + n > d_.size()) return false;
    if (out) out->assign(reinterpret_cast<const char*>(d_.data() + at + 3), n);
    return true;
}

}  // namespace msb
}  // namespace zengine
