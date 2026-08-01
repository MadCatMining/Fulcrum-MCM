// Scrypt(N=1024, r=1, p=1) block-header hashing -- the classic "Litecoin scrypt"
// proof-of-work / block-identifier hash used by Litecoin-derived coins (e.g. Infiniloop/IL8P).
//
// Self-contained: PBKDF2-HMAC-SHA256 is built on the vendored CHMAC_SHA256, and the
// Salsa20/8 core + ROMix are implemented here. No OpenSSL dependency.
#pragma once

#include <cstddef>

namespace bitcoin {

/// Computes the scrypt_1024_1_1_256 hash of `len` bytes at `data` and writes 32 bytes to `out`.
/// (For a block header this is the standard 80 bytes.) Output is in internal (little-endian)
/// byte order -- reverse it for display, the same convention as a bitcoin block hash.
void ScryptHash(const void *data, size_t len, unsigned char out[32]);

} // namespace bitcoin
