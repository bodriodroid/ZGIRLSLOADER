// aead.h - ChaCha20-Poly1305 (RFC 8439), self-contained.
//
// Used by the PAFX container (pafx.h) to seal each entry with a per-install key.  No external crypto library: the
// algorithm is public (RFC 8439) and small enough to keep in-tree; tests/test_fx.cpp checks it against the RFC's Poly1305
// vector and against vectors produced with an independent implementation.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace zengine {
namespace fx {

using AeadKey = std::array<uint8_t, 32>;
using AeadNonce = std::array<uint8_t, 12>;
constexpr size_t kAeadTagSize = 16;

// One ChaCha20 block (64 bytes of key stream) for (key, counter, nonce).
void chacha20Block(const AeadKey& key, uint32_t counter, const AeadNonce& nonce, uint8_t out[64]);

// XORs `data` with the ChaCha20 key stream starting at block `counter`.
void chacha20Xor(const AeadKey& key, uint32_t counter, const AeadNonce& nonce, const uint8_t* in, size_t len, uint8_t* out);

// Poly1305 one-time authenticator.
void poly1305(const uint8_t key[32], const uint8_t* msg, size_t len, uint8_t tag[16]);

// out = ciphertext || 16-byte tag.
void aeadSeal(const AeadKey& key, const AeadNonce& nonce, const uint8_t* aad, size_t aadLen, const uint8_t* plain, size_t plainLen,
              std::vector<uint8_t>& out);

// `in` = ciphertext || tag.  False when the tag does not verify (wrong key, wrong nonce / aad, or modified data); `out` is
// cleared in that case.
bool aeadOpen(const AeadKey& key, const AeadNonce& nonce, const uint8_t* aad, size_t aadLen, const uint8_t* in, size_t inLen,
              std::vector<uint8_t>& out);

// Fills `out` from the operating system's random source (getrandom / /dev/urandom).  False if none was available.
bool secureRandom(uint8_t* out, size_t len);

}  // namespace fx
}  // namespace zengine
