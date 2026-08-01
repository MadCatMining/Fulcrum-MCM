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
#include "PoWHash.h"

#include "Common.h"

#include "bitcoin/crypto/scrypt/scrypt.h"
#include "bitcoin/crypto/tribus/tribus.h"
#include "bitcoin/hash.h"

namespace BTC {

namespace {
    [[noreturn]] void notImplemented(PoWHashAlgo algo) {
        throw InternalError(QString("PoW hash algorithm '%1' is not yet vendored in this build").arg(AlgoName(algo)));
    }

    QByteArray sha256d(const void *data, std::size_t len) {
        bitcoin::CHash256 h;
        QByteArray ret(int(h.OUTPUT_SIZE), Qt::Initialization::Uninitialized);
        h.Write(reinterpret_cast<const uint8_t *>(data), len);
        h.Finalize(reinterpret_cast<uint8_t *>(ret.data()));
        return ret;
    }

    QByteArray tribus(const void *data, std::size_t len) {
        QByteArray ret(32, Qt::Initialization::Uninitialized);
        bitcoin::TribusHash(data, len, reinterpret_cast<unsigned char *>(ret.data()));
        return ret;
    }

    QByteArray scrypt(const void *data, std::size_t len) {
        QByteArray ret(32, Qt::Initialization::Uninitialized);
        bitcoin::ScryptHash(data, len, reinterpret_cast<unsigned char *>(ret.data()));
        return ret;
    }
}

QByteArray HashHeaderForAlgo(PoWHashAlgo algo, const void *header, std::size_t len)
{
    switch (algo) {
    case PoWHashAlgo::SHA256d: return sha256d(header, len);
    case PoWHashAlgo::Tribus:  return tribus(header, len);
    case PoWHashAlgo::Scrypt:  return scrypt(header, len);
    default: break;
    }
    notImplemented(algo);
}

QString AlgoName(PoWHashAlgo algo)
{
    switch (algo) {
    case PoWHashAlgo::SHA256d:      return QStringLiteral("SHA256d");
    case PoWHashAlgo::Tribus:       return QStringLiteral("Tribus");
    case PoWHashAlgo::Scrypt:       return QStringLiteral("Scrypt");
    case PoWHashAlgo::ScryptN:      return QStringLiteral("Scrypt-N");
    case PoWHashAlgo::ScryptN11:    return QStringLiteral("ScryptN11");
    case PoWHashAlgo::NeoScrypt:    return QStringLiteral("NeoScrypt");
    case PoWHashAlgo::Quark:        return QStringLiteral("Quark");
    case PoWHashAlgo::Qubit:        return QStringLiteral("Qubit");
    case PoWHashAlgo::X11:          return QStringLiteral("X11");
    case PoWHashAlgo::X13:          return QStringLiteral("X13");
    case PoWHashAlgo::X15:          return QStringLiteral("X15");
    case PoWHashAlgo::X16R:         return QStringLiteral("X16R");
    case PoWHashAlgo::X16Rv2:       return QStringLiteral("X16Rv2");
    case PoWHashAlgo::X16RT:        return QStringLiteral("X16RT");
    case PoWHashAlgo::X16S:         return QStringLiteral("X16S");
    case PoWHashAlgo::X17:          return QStringLiteral("X17");
    case PoWHashAlgo::X21S:         return QStringLiteral("X21S");
    case PoWHashAlgo::X22i:         return QStringLiteral("X22i");
    case PoWHashAlgo::X25X:         return QStringLiteral("X25X");
    case PoWHashAlgo::Lyra2REv2:    return QStringLiteral("Lyra2REv2");
    case PoWHashAlgo::Lyra2z:       return QStringLiteral("Lyra2z");
    case PoWHashAlgo::Yescrypt:     return QStringLiteral("Yescrypt");
    case PoWHashAlgo::YescryptR8:   return QStringLiteral("YescryptR8");
    case PoWHashAlgo::YescryptR16:  return QStringLiteral("YescryptR16");
    case PoWHashAlgo::YescryptR32:  return QStringLiteral("YescryptR32");
    case PoWHashAlgo::YesPoWer:     return QStringLiteral("YesPoWer");
    case PoWHashAlgo::GhostRider:   return QStringLiteral("GhostRider");
    case PoWHashAlgo::Skein:        return QStringLiteral("Skein");
    case PoWHashAlgo::Skein2:       return QStringLiteral("Skein2");
    case PoWHashAlgo::Keccak:       return QStringLiteral("Keccak");
    case PoWHashAlgo::Allium:       return QStringLiteral("Allium");
    case PoWHashAlgo::TimeTravel:   return QStringLiteral("TimeTravel");
    case PoWHashAlgo::Argon2:       return QStringLiteral("Argon2");
    case PoWHashAlgo::Argon2d:      return QStringLiteral("Argon2d");
    case PoWHashAlgo::Blake2B:      return QStringLiteral("Blake2B");
    case PoWHashAlgo::Blake2S:      return QStringLiteral("Blake2S");
    case PoWHashAlgo::Blake3:       return QStringLiteral("Blake3");
    case PoWHashAlgo::BMW512:       return QStringLiteral("BMW512");
    case PoWHashAlgo::HMQ1725:      return QStringLiteral("HMQ1725");
    case PoWHashAlgo::C11:          return QStringLiteral("C11");
    case PoWHashAlgo::CPUPower:     return QStringLiteral("CPUPower");
    }
    return QStringLiteral("Unknown");
}

} // namespace BTC
