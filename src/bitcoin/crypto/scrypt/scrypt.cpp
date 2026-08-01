// Scrypt(N=1024, r=1, p=1) hashing wrapper. See scrypt.h.
//
// PBKDF2-HMAC-SHA256 is built on the vendored CHMAC_SHA256; the Salsa20/8 core and ROMix
// below are the well-known public-domain reference (Colin Percival / ArtForz / pooler),
// as used by Litecoin's src/crypto/scrypt.cpp.
#include "scrypt.h"

#include "bitcoin/crypto/hmac_sha256.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace bitcoin {

namespace {

inline uint32_t le32dec(const void *pp) {
    const uint8_t *p = static_cast<const uint8_t *>(pp);
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

inline void le32enc(void *pp, uint32_t x) {
    uint8_t *p = static_cast<uint8_t *>(pp);
    p[0] = uint8_t(x & 0xff);
    p[1] = uint8_t((x >> 8) & 0xff);
    p[2] = uint8_t((x >> 16) & 0xff);
    p[3] = uint8_t((x >> 24) & 0xff);
}

inline void be32enc(void *pp, uint32_t x) {
    uint8_t *p = static_cast<uint8_t *>(pp);
    p[0] = uint8_t((x >> 24) & 0xff);
    p[1] = uint8_t((x >> 16) & 0xff);
    p[2] = uint8_t((x >> 8) & 0xff);
    p[3] = uint8_t(x & 0xff);
}

// PBKDF2-HMAC-SHA256 (only the c==1 code path is exercised by scrypt, but the general
// iteration count is handled for completeness).
void PBKDF2_SHA256(const uint8_t *passwd, size_t passwdlen, const uint8_t *salt, size_t saltlen,
                   uint64_t c, uint8_t *buf, size_t dkLen) {
    for (size_t i = 0; i * 32 < dkLen; ++i) {
        uint8_t ibe[4];
        be32enc(ibe, uint32_t(i + 1));

        CHMAC_SHA256 prf(passwd, passwdlen);
        prf.Write(salt, saltlen);
        prf.Write(ibe, sizeof(ibe));
        uint8_t U[32];
        prf.Finalize(U);

        uint8_t T[32];
        std::memcpy(T, U, sizeof(T));

        for (uint64_t j = 1; j < c; ++j) {
            CHMAC_SHA256 prf2(passwd, passwdlen);
            prf2.Write(U, sizeof(U));
            prf2.Finalize(U);
            for (int k = 0; k < 32; ++k)
                T[k] ^= U[k];
        }

        size_t clen = dkLen - i * 32;
        if (clen > 32) clen = 32;
        std::memcpy(&buf[i * 32], T, clen);
    }
}

inline uint32_t rotl32(uint32_t x, int b) { return (x << b) | (x >> (32 - b)); }

// Apply the 8-round Salsa20 core to B ^= Bx (in place on B).
void xor_salsa8(uint32_t B[16], const uint32_t Bx[16]) {
    uint32_t x[16];
    for (int i = 0; i < 16; ++i)
        x[i] = (B[i] ^= Bx[i]);

    for (int i = 0; i < 8; i += 2) {
        // Column round
        x[ 4] ^= rotl32(x[ 0] + x[12],  7); x[ 8] ^= rotl32(x[ 4] + x[ 0],  9);
        x[12] ^= rotl32(x[ 8] + x[ 4], 13); x[ 0] ^= rotl32(x[12] + x[ 8], 18);
        x[ 9] ^= rotl32(x[ 5] + x[ 1],  7); x[13] ^= rotl32(x[ 9] + x[ 5],  9);
        x[ 1] ^= rotl32(x[13] + x[ 9], 13); x[ 5] ^= rotl32(x[ 1] + x[13], 18);
        x[14] ^= rotl32(x[10] + x[ 6],  7); x[ 2] ^= rotl32(x[14] + x[10],  9);
        x[ 6] ^= rotl32(x[ 2] + x[14], 13); x[10] ^= rotl32(x[ 6] + x[ 2], 18);
        x[ 3] ^= rotl32(x[15] + x[11],  7); x[ 7] ^= rotl32(x[ 3] + x[15],  9);
        x[11] ^= rotl32(x[ 7] + x[ 3], 13); x[15] ^= rotl32(x[11] + x[ 7], 18);
        // Row round
        x[ 1] ^= rotl32(x[ 0] + x[ 3],  7); x[ 2] ^= rotl32(x[ 1] + x[ 0],  9);
        x[ 3] ^= rotl32(x[ 2] + x[ 1], 13); x[ 0] ^= rotl32(x[ 3] + x[ 2], 18);
        x[ 6] ^= rotl32(x[ 5] + x[ 4],  7); x[ 7] ^= rotl32(x[ 6] + x[ 5],  9);
        x[ 4] ^= rotl32(x[ 7] + x[ 6], 13); x[ 5] ^= rotl32(x[ 4] + x[ 7], 18);
        x[11] ^= rotl32(x[10] + x[ 9],  7); x[ 8] ^= rotl32(x[11] + x[10],  9);
        x[ 9] ^= rotl32(x[ 8] + x[11], 13); x[10] ^= rotl32(x[ 9] + x[ 8], 18);
        x[12] ^= rotl32(x[15] + x[14],  7); x[13] ^= rotl32(x[12] + x[15],  9);
        x[14] ^= rotl32(x[13] + x[12], 13); x[15] ^= rotl32(x[14] + x[13], 18);
    }

    for (int i = 0; i < 16; ++i)
        B[i] += x[i];
}

} // namespace

void ScryptHash(const void *data, size_t len, unsigned char out[32]) {
    const uint8_t *input = static_cast<const uint8_t *>(data);

    uint8_t B[128];
    PBKDF2_SHA256(input, len, input, len, 1, B, sizeof(B));

    uint32_t X[32];
    for (int k = 0; k < 32; ++k)
        X[k] = le32dec(&B[4 * k]);

    // Scratchpad V: N=1024 blocks of 32 words (128 bytes) each = 128 KiB.
    std::vector<uint32_t> V(32u * 1024u);

    for (int i = 0; i < 1024; ++i) {
        std::memcpy(&V[i * 32], X, sizeof(X));
        xor_salsa8(&X[0], &X[16]);
        xor_salsa8(&X[16], &X[0]);
    }
    for (int i = 0; i < 1024; ++i) {
        uint32_t j = 32 * (X[16] & 1023);
        for (int k = 0; k < 32; ++k)
            X[k] ^= V[j + k];
        xor_salsa8(&X[0], &X[16]);
        xor_salsa8(&X[16], &X[0]);
    }

    for (int k = 0; k < 32; ++k)
        le32enc(&B[4 * k], X[k]);

    PBKDF2_SHA256(input, len, B, sizeof(B), 1, out, 32);
}

} // namespace bitcoin
