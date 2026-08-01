//
// Fulcrum - A fast & nimble SPV Server for Bitcoin Cash
// Copyright (C) 2019-2026 Calin A. Culianu <calin.culianu@gmail.com>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program (see LICENSE.txt).  If not, see
// <https://www.gnu.org/licenses/>.
//
#pragma once

#include <QByteArray>
#include <QString>

#include <cstddef>
#include <cstdint>

namespace BTC {

/// Algorithms that can compute a block *identifier* hash from an 80-byte block header.
/// Fulcrum never validates proof-of-work; this enum exists solely to dispatch the per-coin
/// block-ID hash. Most coins use SHA256d even when mined with an exotic algorithm; the only
/// reason to pick a non-default algo here is when the block ID itself is the PoW hash
/// (e.g. Diminutivecoin uses Tribus).
///
/// Each enum value is stable; new algos are appended. CoinConfig entries pick one.
enum class PoWHashAlgo : uint16_t {
    SHA256d = 0,            // Bitcoin / BCH / LTC / nearly every altcoin's block ID
    Tribus,                 // Diminutivecoin (vendored under bitcoin/crypto/tribus/)
    // --- the following are reserved values; vendored incrementally as coins need them ---
    Scrypt, ScryptN, ScryptN11,
    NeoScrypt,
    Quark, Qubit,
    X11, X13, X15, X16R, X16Rv2, X16RT, X16S, X17, X21S, X22i, X25X,
    Lyra2REv2, Lyra2z,
    Yescrypt, YescryptR8, YescryptR16, YescryptR32, YesPoWer,
    GhostRider,
    Skein, Skein2,
    Keccak, Allium, TimeTravel,
    Argon2, Argon2d,
    Blake2B, Blake2S, Blake3,
    BMW512, HMQ1725, C11, CPUPower,
};

/// Computes the block-ID hash of `len` bytes at `header` using `algo`. Returns 32 bytes in
/// internal (little-endian) byte order (the same convention as `BTC::Hash`).
/// Throws InternalError if the algorithm is not yet vendored.
QByteArray HashHeaderForAlgo(PoWHashAlgo algo, const void *header, std::size_t len);

/// Human-readable algorithm name, e.g. "SHA256d" / "Tribus" / "Scrypt".
QString AlgoName(PoWHashAlgo algo);

} // namespace BTC
