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
#include "CoinConfig.h"

#include <array>
#include <cstddef>

namespace BTC {

namespace {

// The single source of truth for all per-coin behaviour. Adding a new coin = one entry.
CoinConfig makeUnknown() {
    CoinConfig c;
    c.coin = Coin::Unknown;
    c.name = QStringLiteral("Unknown");
    c.displayName = QStringLiteral("Unknown");
    // Be conservative: keep CashTokens enabled by default so a coin we forgot about still
    // deserialises BCH-style blocks (preserves prior behaviour of isBCHCoin() == true for Unknown).
    c.allowCashTokens = true;
    return c;
}

CoinConfig makeBCH() {
    CoinConfig c;
    c.coin = Coin::BCH;
    c.name = QStringLiteral("BCH");
    c.displayName = QStringLiteral("Bitcoin Cash");
    c.subversionPrefixes = {QStringLiteral("/Bitcoin Cash Node:"),
                            QStringLiteral("/BCH Unlimited:"),
                            QStringLiteral("/bchd"),
                            QStringLiteral("/Flowee:")};
    c.mainnetVer = {0, 5};
    c.testnetVer = {111, 196};
    c.regtestVer = {111, 196};
    c.cashAddrPrefixMain = QStringLiteral("bitcoincash");
    c.cashAddrPrefixTest = QStringLiteral("bchtest");
    c.allowCashTokens = true;
    c.blockIdAlgo = PoWHashAlgo::SHA256d;
    c.estimateFee = EstimateFeeStyle::ZeroArg;
    c.peerResourcePath = QStringLiteral(":resources/bch/");
    c.isRPACapable = true;
    c.warnVulnerableElectronCash = true;
    return c;
}

CoinConfig makeBTC() {
    CoinConfig c;
    c.coin = Coin::BTC;
    c.name = QStringLiteral("BTC");
    c.displayName = QStringLiteral("Bitcoin");
    c.subversionPrefixes = {QStringLiteral("/Satoshi:")};
    c.mainnetVer = {0, 5};
    c.testnetVer = {111, 196};
    c.regtestVer = {111, 196};
    c.allowSegWit = true;
    c.blockIdAlgo = PoWHashAlgo::SHA256d;
    c.estimateFee = EstimateFeeStyle::SmartFee;
    c.peerResourcePath = QStringLiteral(":resources/btc/");
    c.warnVulnerableElectrum = true;
    return c;
}

CoinConfig makeLTC() {
    CoinConfig c;
    c.coin = Coin::LTC;
    c.name = QStringLiteral("LTC");
    c.displayName = QStringLiteral("Litecoin");
    c.subversionPrefixes = {QStringLiteral("/LitecoinCore:")};
    c.mainnetVer = {0, 5}; // raw decode tables — LTC's mainnet P2PKH override applies only at encode time
    c.testnetVer = {111, 196};
    c.regtestVer = {111, 196};
    c.litecoinLegacyP2PKHOverride = 48;
    c.allowSegWit = true;
    c.allowMimbleWimble = true;
    c.blockIdAlgo = PoWHashAlgo::SHA256d;
    c.estimateFee = EstimateFeeStyle::SmartFee;
    c.peerResourcePath = QStringLiteral(":resources/ltc/");
    c.warnVulnerableElectrumLTC = true;
    return c;
}

CoinConfig makeDIMI() {
    CoinConfig c;
    c.coin = Coin::DIMI;
    c.name = QStringLiteral("DIMI");
    c.displayName = QStringLiteral("Diminutivecoin");
    c.subversionPrefixes = {QStringLiteral("/Diminutivecoin:")};
    c.mainnetVer = {0x20, 0x1e};
    c.testnetVer = {111, 196};
    c.regtestVer = {111, 196};
    c.hasTransactionTimestamp = true; // Blackcoin-v13-style nTime in CTransaction
    c.blockIdAlgo = PoWHashAlgo::Tribus;
    c.getRawTxVerboseAsInt = true;    // Bitcoin Core 0.13 quirk
    c.estimateFee = EstimateFeeStyle::LegacyOneArg;
    c.peerResourcePath = QString(); // empty -> peering disabled
    return c;
}

CoinConfig makeDGC() {
    CoinConfig c;
    c.coin = Coin::DGC;
    c.name = QStringLiteral("DGC");
    c.displayName = QStringLiteral("Digitalcoin");
    // NB: the `getnetworkinfo` subversion for Digitalcoin Core looks like "/Digitalcoin Core:5.0.3/".
    // Match that prefix (this is NOT the message-signing magic "Digitalcoin Signed Message:").
    c.subversionPrefixes = {QStringLiteral("/Digitalcoin Core:")};
    c.mainnetVer = {0x1e, 0x5};
    c.testnetVer = {111, 196};
    c.regtestVer = {111, 196};
    c.getRawTxVerboseAsInt = true;    // Bitcoin Core 0.13 quirk
    c.allowSegWit = false;
    c.blockIdAlgo = PoWHashAlgo::SHA256d;
    c.estimateFee = EstimateFeeStyle::LegacyOneArg;
    c.peerResourcePath = QString(); // empty -> peering disabled
    return c;
}

CoinConfig makeIL8P() {
    CoinConfig c;
    c.coin = Coin::IL8P;
    c.name = QStringLiteral("IL8P");
    c.displayName = QStringLiteral("Infiniloop");
    c.subversionPrefixes = {QStringLiteral("/Infiniloop:")};
    c.mainnetVer = {0x21, 0x55};
    c.testnetVer = {111, 196};
    c.regtestVer = {111, 196};
    c.hasTransactionTimestamp = true; // POS
    // Blackcoin "protocol v2" lineage: every tx (any version) carried a 4-byte nTime up to the switch
    // that bumped the block header version past 7; blocks with header nVersion <= 7 carry tx nTime,
    // later (BIP9-versioned) blocks do not.
    c.txTimestampMaxBlockVersion = 7;
    // Segwit is active on-chain (coinbase carries a BIP141 witness commitment, OP_RETURN aa21a9ed),
    // so witness txs can appear; enabling is backward-compatible with legacy txs.
    c.allowSegWit = true;
    // Hybrid PoW/PoS: ONLY the genesis block is mined (scrypt(1024,1,1)); every subsequent PoS
    // block's id is plain SHA256d. genesisBlockIdAlgo is applied to the genesis block alone.
    c.blockIdAlgo = PoWHashAlgo::SHA256d;
    c.genesisBlockIdAlgo = PoWHashAlgo::Scrypt;
    c.estimateFee = EstimateFeeStyle::SmartFee;
    c.peerResourcePath = QString(); // empty -> peering disabled
    return c;
}

CoinConfig makeLYNX() {
    CoinConfig c;
    c.coin = Coin::LYNX;
    c.name = QStringLiteral("LYNX");
    c.displayName = QStringLiteral("Lynx core");
    c.subversionPrefixes = {QStringLiteral("/Lynx Core:")};
    c.mainnetVer = {0x2d, 0x16};
    c.testnetVer = {111, 196};
    c.regtestVer = {111, 196};
    c.hasTransactionTimestamp = true; // POS
    // Blackcoin "protocol v2" lineage: every tx (any version) carried a 4-byte nTime up to the switch
    // that bumped the block header version past 7; blocks with header nVersion <= 7 carry tx nTime,
    // later (BIP9-versioned) blocks do not.
    c.txTimestampMaxBlockVersion = 7;
    // Segwit is active on-chain (coinbase carries a BIP141 witness commitment, OP_RETURN aa21a9ed),
    // so witness txs can appear; enabling is backward-compatible with legacy txs.
    c.allowSegWit = true;
    // Hybrid PoW/PoS: ONLY the genesis block is mined (scrypt(1024,1,1)); every subsequent PoS
    // block's id is plain SHA256d. genesisBlockIdAlgo is applied to the genesis block alone.
    c.blockIdAlgo = PoWHashAlgo::SHA256d;
    c.genesisBlockIdAlgo = PoWHashAlgo::Scrypt;
    c.estimateFee = EstimateFeeStyle::SmartFee;
    c.peerResourcePath = QString(); // empty -> peering disabled
    return c;
}


const std::array<CoinConfig, 8> & registry() {
    static const std::array<CoinConfig, 8> kRegistry = {{
        makeUnknown(),
        makeBCH(),
        makeBTC(),
        makeLTC(),
        makeDIMI(),
        makeDGC(),
        makeIL8P(),
        makeLYNX(),
    }};
    return kRegistry;
}

const std::vector<Coin> & registeredCoinList() {
    static const std::vector<Coin> kList = []{
        std::vector<Coin> v;
        v.reserve(registry().size());
        for (const auto &c : registry())
            if (c.coin != Coin::Unknown)
                v.push_back(c.coin);
        return v;
    }();
    return kList;
}

} // namespace

const CoinConfig & GetCoinConfig(Coin c) noexcept
{
    for (const auto &cc : registry())
        if (cc.coin == c)
            return cc;
    return registry().front(); // Unknown
}

const CoinConfig * GetCoinConfigByName(const QString &name) noexcept
{
    if (name.isEmpty()) return nullptr;
    for (const auto &cc : registry())
        if (cc.coin != Coin::Unknown && cc.name == name)
            return &cc;
    return nullptr;
}

const CoinConfig * GetCoinConfigBySubversion(const QString &subversion) noexcept
{
    if (subversion.isEmpty()) return nullptr;
    for (const auto &cc : registry()) {
        if (cc.coin == Coin::Unknown) continue;
        for (const auto &prefix : cc.subversionPrefixes)
            if (!prefix.isEmpty() && subversion.startsWith(prefix))
                return &cc;
    }
    return nullptr;
}

const std::vector<Coin> & AllRegisteredCoins() noexcept { return registeredCoinList(); }

} // namespace BTC
