// Tribus hashing (JH-512 -> Keccak-512 -> Echo-512), used by Diminutivecoin and
// other coins as the block-header proof-of-work / identifier hash.
//
// The underlying JH/Keccak/Echo implementations are the public-domain sphlib
// sources (Thomas Pornin) vendored in this directory.
#pragma once

#include <cstddef>

namespace bitcoin {

/// Computes the Tribus hash of `len` bytes at `data` and writes 32 bytes to `out`.
/// Output is in internal (little-endian) byte order -- reverse it for display, the
/// same convention as a bitcoin block hash.
void TribusHash(const void *data, size_t len, unsigned char out[32]);

} // namespace bitcoin
