// Tribus hashing wrapper. See tribus.h.
#include "tribus.h"

#include "sph_jh.h"
#include "sph_keccak.h"
#include "sph_echo.h"

#include <cstring>

namespace bitcoin {

void TribusHash(const void *data, size_t len, unsigned char out[32]) {
    unsigned char h[64];
    sph_jh512_context     ctx_jh;
    sph_keccak512_context ctx_keccak;
    sph_echo512_context   ctx_echo;

    sph_jh512_init(&ctx_jh);
    sph_jh512(&ctx_jh, data, len);
    sph_jh512_close(&ctx_jh, h);

    sph_keccak512_init(&ctx_keccak);
    sph_keccak512(&ctx_keccak, h, sizeof(h));
    sph_keccak512_close(&ctx_keccak, h);

    sph_echo512_init(&ctx_echo);
    sph_echo512(&ctx_echo, h, sizeof(h));
    sph_echo512_close(&ctx_echo, h);

    std::memcpy(out, h, 32);
}

} // namespace bitcoin
