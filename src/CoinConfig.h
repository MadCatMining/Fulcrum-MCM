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

#include "BTC.h"
#include "PoWHash.h"

#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>
#include <vector>

namespace BTC {

/// Per-net base58 version-byte pair (P2PKH, P2SH).
struct Base58VerBytes {
    uint8_t p2pkh = 0;
    uint8_t p2sh = 0;
};

/// Style of the `estimatefee` family of RPCs the daemon supports.
/// (`hasEstimateSmartFee` / `isTwoArgEstimateSmartFee` are derived from this + the daemon
/// version at runtime in BitcoinD.cpp.)
enum class EstimateFeeStyle : uint8_t {
    ZeroArg,        ///< BCHN/ABC newer (>=0.20.2): estimatefee with no args
    LegacyOneArg,   ///< pre-smart-fee bitcoind path (estimatefee N)
    SmartFee,       ///< Core/LTC family — actual one-vs-two-arg picked by runtime version check
};

/// All per-coin parameters in a single declarative table. Centralises everything that used
/// to be scattered across BTC.cpp / BTC_Address.cpp / BitcoinD.cpp / Controller.h /
/// Servers.cpp / PeerMgr.cpp / SrvMgr.cpp / Storage.cpp as `if (coin == Coin::X)`.
///
/// To add a new coin: append a `Coin::XXX` enumerator in BTC.h and a `CoinConfig` entry in
/// CoinConfig.cpp's registry. Nothing else needs to change as long as the coin uses an
/// already-vendored block-ID hash and an already-handled tx/block format. If it needs a new
/// PoW hash, append it to `PoWHashAlgo` and wire it into `HashHeaderForAlgo`.
struct CoinConfig {
    // ---- identity ----
    Coin coin = Coin::Unknown;
    QString name;                 ///< short name used by Storage.meta.coin ("BCH", "BTC", "LTC", "DIMI")
    QString displayName;          ///< human-readable ("Bitcoin Cash" / "Diminutivecoin")
    QStringList subversionPrefixes; ///< match against `getnetworkinfo.subversion` for auto-detection

    // ---- address encoding ----
    Base58VerBytes mainnetVer{0, 5};
    Base58VerBytes testnetVer{111, 196};
    Base58VerBytes regtestVer{111, 196};
    QString cashAddrPrefixMain;   ///< e.g. "bitcoincash"; empty if coin has no cashaddr
    QString cashAddrPrefixTest;   ///< e.g. "bchtest"; empty if coin has no cashaddr
    /// LTC's quirky "Litecoin-style legacy" mainnet P2PKH override (0 if unused). When non-zero
    /// `Address::toLitecoinString()` substitutes this byte for mainnet P2PKH only.
    uint8_t litecoinLegacyP2PKHOverride = 0;

    // ---- transaction & block format flags (drive bitcoin::CTransaction / CBlock deser) ----
    bool allowSegWit = false;
    bool allowMimbleWimble = false;
    bool allowCashTokens = false;
    bool hasTransactionTimestamp = false; ///< Diminutivecoin / Peercoin / Novacoin tx nTime
    /// nTime-on-ALL-tx-versions coins (Blackcoin "protocol v2" lineage, e.g. Infiniloop/IL8P) carried a
    /// per-tx nTime on every tx version until a switch that bumped the block header version. When set,
    /// txs in a block whose header nVersion <= this value carry nTime (any tx version); later blocks do
    /// not. Leave unset for the DIMI/Blackcoin-more rule (nTime only on nVersion==1). Requires
    /// hasTransactionTimestamp = true. Decided per-block from the header version in BTC::Deserialize.
    std::optional<int32_t> txTimestampMaxBlockVersion;
    bool hasCoinStake = false;            ///< PoS marker; reserved (today: future-proofing only)
    bool hasPoSBlockSig = false;          ///< PoS block-signature trailer; reserved
    uint32_t headerExtraBytes = 0;        ///< extra bytes after the 80-byte header (0 today)

    // ---- block-ID hash ----
    PoWHashAlgo blockIdAlgo = PoWHashAlgo::SHA256d;
    /// Some hybrid PoW/PoS coins mine ONLY the genesis block with an exotic PoW algo but use a
    /// different (usually SHA256d) block-ID hash for every subsequent (PoS) block. When set, this
    /// algo is applied to the genesis block ALONE -- identified purely by content (hashPrevBlock ==
    /// all-zeros) -- so no height needs to be threaded through the HashBlockHeader() call sites.
    /// Leave unset (nullopt) for the normal case where the genesis uses `blockIdAlgo` like every
    /// other block. Example: Infiniloop (IL8P) -- genesis is scrypt, all later blocks are SHA256d.
    std::optional<PoWHashAlgo> genesisBlockIdAlgo;

    // ---- BitcoinD RPC quirks ----
    bool getRawTxVerboseAsInt = false;    ///< Bitcoin Core 0.13 quirk (DIMI)
    EstimateFeeStyle estimateFee = EstimateFeeStyle::ZeroArg;
    bool initialHasDSProofRPC = false;    ///< initial state before runtime probing
    bool initialHasSubmitPackageRPC = false;
    bool sendRawTxRequiresMaxBurnAmount = false; ///< Core ≥ 0.25.0 — gated on runtime version too
    bool lacksGetZmqNotifications = false;

    // ---- peer discovery & server-side bits ----
    /// Qt-resource path prefix for peer seed JSON files (e.g. ":resources/bch/"). Empty disables peering.
    QString peerResourcePath;
    /// Optional default donation address (cosmetic).
    QString defaultDonationAddress;
    /// True iff RPA index auto-enables for this coin under Options::Rpa::Auto.
    bool isRPACapable = false;
    /// True if vulnerable-Electrum (BTC), vulnerable-Electrum-LTC, or vulnerable-Electron-Cash (BCH) warnings apply.
    bool warnVulnerableElectrum = false;
    bool warnVulnerableElectrumLTC = false;
    bool warnVulnerableElectronCash = false;
};

/// Always-non-null lookup: returns the Unknown entry as a safe default for unrecognised coins.
const CoinConfig & GetCoinConfig(Coin) noexcept;

/// Lookup by storage-name ("BCH" / "BTC" / "LTC" / "DIMI" / "DGC" ). Returns nullptr if no match.
const CoinConfig * GetCoinConfigByName(const QString &name) noexcept;

/// First registered coin whose `subversionPrefixes` contains a prefix the given subversion starts with.
/// Returns nullptr if no match.
const CoinConfig * GetCoinConfigBySubversion(const QString &subversion) noexcept;

/// All registered, non-Unknown coins (for iteration / detection / diagnostics).
const std::vector<Coin> & AllRegisteredCoins() noexcept;

} // namespace BTC
