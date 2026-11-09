#include "aead.h"

#include <cstdio>
#include <cstring>
#include <random>

namespace zengine {
namespace fx {
namespace {

inline uint32_t rotl(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }
inline uint32_t ld32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
inline void st32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}
inline void st64(uint8_t* p, uint64_t v) {
    st32(p, static_cast<uint32_t>(v));
    st32(p + 4, static_cast<uint32_t>(v >> 32));
}

#define QR(a, b, c, d)                \
    a += b; d ^= a; d = rotl(d, 16);  \
    c += d; b ^= c; b = rotl(b, 12);  \
    a += b; d ^= a; d = rotl(d, 8);   \
    c += d; b ^= c; b = rotl(b, 7)

}  // namespace

void chacha20Block(const AeadKey& key, uint32_t counter, const AeadNonce& nonce, uint8_t out[64]) {
    uint32_t s[16] = {0x61707865u, 0x3320646eu, 0x79622d32u, 0x6b206574u};
    for (int i = 0; i < 8; ++i) s[4 + i] = ld32(key.data() + 4 * i);
    s[12] = counter;
    for (int i = 0; i < 3; ++i) s[13 + i] = ld32(nonce.data() + 4 * i);
    uint32_t x[16];
    std::memcpy(x, s, sizeof(x));
    for (int r = 0; r < 10; ++r) {
        QR(x[0], x[4], x[8], x[12]);
        QR(x[1], x[5], x[9], x[13]);
        QR(x[2], x[6], x[10], x[14]);
        QR(x[3], x[7], x[11], x[15]);
        QR(x[0], x[5], x[10], x[15]);
        QR(x[1], x[6], x[11], x[12]);
        QR(x[2], x[7], x[8], x[13]);
        QR(x[3], x[4], x[9], x[14]);
    }
    for (int i = 0; i < 16; ++i) st32(out + 4 * i, x[i] + s[i]);
}

void chacha20Xor(const AeadKey& key, uint32_t counter, const AeadNonce& nonce, const uint8_t* in, size_t len, uint8_t* out) {
    uint8_t ks[64];
    for (size_t off = 0; off < len; off += 64, ++counter) {
        chacha20Block(key, counter, nonce, ks);
        const size_t n = len - off < 64 ? len - off : 64;
        for (size_t i = 0; i < n; ++i) out[off + i] = in[off + i] ^ ks[i];
    }
}

// Poly1305 with 5 x 26-bit limbs (the usual 32-bit formulation).
void poly1305(const uint8_t key[32], const uint8_t* msg, size_t len, uint8_t tag[16]) {
    const uint32_t r0 = ld32(key + 0) & 0x3ffffff;
    const uint32_t r1 = (ld32(key + 3) >> 2) & 0x3ffff03;
    const uint32_t r2 = (ld32(key + 6) >> 4) & 0x3ffc0ff;
    const uint32_t r3 = (ld32(key + 9) >> 6) & 0x3f03fff;
    const uint32_t r4 = (ld32(key + 12) >> 8) & 0x00fffff;
    const uint32_t s1 = r1 * 5, s2 = r2 * 5, s3 = r3 * 5, s4 = r4 * 5;
    uint32_t h0 = 0, h1 = 0, h2 = 0, h3 = 0, h4 = 0;

    auto block = [&](const uint8_t* m, uint32_t hibit) {
        h0 += ld32(m + 0) & 0x3ffffff;
        h1 += (ld32(m + 3) >> 2) & 0x3ffffff;
        h2 += (ld32(m + 6) >> 4) & 0x3ffffff;
        h3 += (ld32(m + 9) >> 6) & 0x3ffffff;
        h4 += (ld32(m + 12) >> 8) | hibit;
        const uint64_t d0 = static_cast<uint64_t>(h0) * r0 + static_cast<uint64_t>(h1) * s4 + static_cast<uint64_t>(h2) * s3 + static_cast<uint64_t>(h3) * s2 + static_cast<uint64_t>(h4) * s1;
        uint64_t d1 = static_cast<uint64_t>(h0) * r1 + static_cast<uint64_t>(h1) * r0 + static_cast<uint64_t>(h2) * s4 + static_cast<uint64_t>(h3) * s3 + static_cast<uint64_t>(h4) * s2;
        uint64_t d2 = static_cast<uint64_t>(h0) * r2 + static_cast<uint64_t>(h1) * r1 + static_cast<uint64_t>(h2) * r0 + static_cast<uint64_t>(h3) * s4 + static_cast<uint64_t>(h4) * s3;
        uint64_t d3 = static_cast<uint64_t>(h0) * r3 + static_cast<uint64_t>(h1) * r2 + static_cast<uint64_t>(h2) * r1 + static_cast<uint64_t>(h3) * r0 + static_cast<uint64_t>(h4) * s4;
        uint64_t d4 = static_cast<uint64_t>(h0) * r4 + static_cast<uint64_t>(h1) * r3 + static_cast<uint64_t>(h2) * r2 + static_cast<uint64_t>(h3) * r1 + static_cast<uint64_t>(h4) * r0;
        uint64_t c = d0 >> 26;
        h0 = static_cast<uint32_t>(d0) & 0x3ffffff;
        d1 += c; c = d1 >> 26; h1 = static_cast<uint32_t>(d1) & 0x3ffffff;
        d2 += c; c = d2 >> 26; h2 = static_cast<uint32_t>(d2) & 0x3ffffff;
        d3 += c; c = d3 >> 26; h3 = static_cast<uint32_t>(d3) & 0x3ffffff;
        d4 += c; c = d4 >> 26; h4 = static_cast<uint32_t>(d4) & 0x3ffffff;
        h0 += static_cast<uint32_t>(c) * 5;
        c = h0 >> 26; h0 &= 0x3ffffff;
        h1 += static_cast<uint32_t>(c);
    };

    while (len >= 16) {
        block(msg, 1u << 24);
        msg += 16;
        len -= 16;
    }
    if (len) {
        uint8_t last[16] = {0};
        std::memcpy(last, msg, len);
        last[len] = 1;
        block(last, 0);
    }

    // fully carry, then reduce modulo 2^130 - 5
    uint32_t c = h1 >> 26; h1 &= 0x3ffffff;
    h2 += c; c = h2 >> 26; h2 &= 0x3ffffff;
    h3 += c; c = h3 >> 26; h3 &= 0x3ffffff;
    h4 += c; c = h4 >> 26; h4 &= 0x3ffffff;
    h0 += c * 5; c = h0 >> 26; h0 &= 0x3ffffff;
    h1 += c;

    uint32_t g0 = h0 + 5; c = g0 >> 26; g0 &= 0x3ffffff;
    uint32_t g1 = h1 + c; c = g1 >> 26; g1 &= 0x3ffffff;
    uint32_t g2 = h2 + c; c = g2 >> 26; g2 &= 0x3ffffff;
    uint32_t g3 = h3 + c; c = g3 >> 26; g3 &= 0x3ffffff;
    uint32_t g4 = h4 + c - (1u << 26);
    const uint32_t mask = (g4 >> 31) - 1;  // all ones when h >= p
    g0 &= mask; g1 &= mask; g2 &= mask; g3 &= mask; g4 &= mask;
    const uint32_t nmask = ~mask;
    h0 = (h0 & nmask) | g0; h1 = (h1 & nmask) | g1; h2 = (h2 & nmask) | g2; h3 = (h3 & nmask) | g3; h4 = (h4 & nmask) | g4;

    // h = h % 2^128, then add the pad
    const uint32_t w0 = h0 | (h1 << 26);
    const uint32_t w1 = (h1 >> 6) | (h2 << 20);
    const uint32_t w2 = (h2 >> 12) | (h3 << 14);
    const uint32_t w3 = (h3 >> 18) | (h4 << 8);
    uint64_t f = static_cast<uint64_t>(w0) + ld32(key + 16);
    st32(tag + 0, static_cast<uint32_t>(f));
    f = static_cast<uint64_t>(w1) + ld32(key + 20) + (f >> 32);
    st32(tag + 4, static_cast<uint32_t>(f));
    f = static_cast<uint64_t>(w2) + ld32(key + 24) + (f >> 32);
    st32(tag + 8, static_cast<uint32_t>(f));
    f = static_cast<uint64_t>(w3) + ld32(key + 28) + (f >> 32);
    st32(tag + 12, static_cast<uint32_t>(f));
}

namespace {
void macData(const uint8_t* aad, size_t aadLen, const uint8_t* ct, size_t ctLen, std::vector<uint8_t>& m) {
    auto pad = [&m]() {
        while (m.size() % 16) m.push_back(0);
    };
    m.insert(m.end(), aad, aad + aadLen);
    pad();
    m.insert(m.end(), ct, ct + ctLen);
    pad();
    uint8_t l[16];
    st64(l, aadLen);
    st64(l + 8, ctLen);
    m.insert(m.end(), l, l + 16);
}
}  // namespace

void aeadSeal(const AeadKey& key, const AeadNonce& nonce, const uint8_t* aad, size_t aadLen, const uint8_t* plain, size_t plainLen,
              std::vector<uint8_t>& out) {
    uint8_t block0[64];
    chacha20Block(key, 0, nonce, block0);
    out.assign(plainLen + kAeadTagSize, 0);
    chacha20Xor(key, 1, nonce, plain, plainLen, out.data());
    std::vector<uint8_t> m;
    m.reserve(aadLen + plainLen + 48);
    macData(aad, aadLen, out.data(), plainLen, m);
    poly1305(block0, m.data(), m.size(), out.data() + plainLen);
}

bool aeadOpen(const AeadKey& key, const AeadNonce& nonce, const uint8_t* aad, size_t aadLen, const uint8_t* in, size_t inLen,
              std::vector<uint8_t>& out) {
    out.clear();
    if (inLen < kAeadTagSize) return false;
    const size_t ctLen = inLen - kAeadTagSize;
    uint8_t block0[64], tag[16];
    chacha20Block(key, 0, nonce, block0);
    std::vector<uint8_t> m;
    m.reserve(aadLen + ctLen + 48);
    macData(aad, aadLen, in, ctLen, m);
    poly1305(block0, m.data(), m.size(), tag);
    uint8_t diff = 0;
    for (size_t i = 0; i < kAeadTagSize; ++i) diff |= static_cast<uint8_t>(tag[i] ^ in[ctLen + i]);
    if (diff != 0) return false;
    out.resize(ctLen);
    chacha20Xor(key, 1, nonce, in, ctLen, out.data());
    return true;
}

bool secureRandom(uint8_t* out, size_t len) {
    if (std::FILE* f = std::fopen("/dev/urandom", "rb")) {
        const size_t n = std::fread(out, 1, len, f);
        std::fclose(f);
        if (n == len) return true;
    }
    try {
        std::random_device rd;
        for (size_t i = 0; i < len; ++i) out[i] = static_cast<uint8_t>(rd());
        return true;  // random_device is the OS source on every platform we build for
    } catch (...) {
        return false;
    }
}

}  // namespace fx
}  // namespace zengine
