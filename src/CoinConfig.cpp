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

CoinConfig makeLynxFamily(Coin coin, const char *name, const char *displayName,
                          const char *subversionPrefix, uint8_t p2pkh, uint8_t p2sh); // defined below

// Digitalcoin was rebased onto the Lynx multi-chain codebase (`$NAME=digitalcoin`), so it now has the
// same tx/block format as IL8P / the Lynx family: PoS with era-based tx nTime (header nVersion <= 7),
// SegWit, smart fees. ONE difference: it kept its original 2013 genesis (hash 5e039e1c…f9c8), whose
// block-id is plain SHA256d of the header (scrypt was only its PoW hash, 000006cb…), so unlike IL8P
// there is NO scrypt-genesis override. Network params (from the Lynx chain spec): p2p 7999, rpc 7998,
// pubkey prefix 30 ('D'), script prefix 5, magic fbc0b6db.
CoinConfig makeDGC() {
    CoinConfig c = makeLynxFamily(Coin::DGC, "DGC", "Digitalcoin", "/Digitalcoin:", 0x1e, 0x05);
    // The Lynx-based daemon reports e.g. `/Digitalcoin:28.4.1/`. The legacy pre-rebase daemon
    // (`/Digitalcoin Core:`) is deliberately NOT matched: its chain/format is no longer supported.
    c.genesisBlockIdAlgo.reset();   // genesis block-id is SHA256d, same as every later block
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
    // A `$NAME=Lynx` build reports `/Lynx:`; a default build (no $NAME) reports `/Lynx Core:` — match both.
    c.subversionPrefixes = {QStringLiteral("/Lynx:"), QStringLiteral("/Lynx Core:")};
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


// Shared factory for the Lynx multi-chain family. These chains are built from the same Lynx codebase
// via `export $NAME=<chain>` before compiling; each reports subversion `/<Chain>:` and differs ONLY in
// identity and base58 address bytes. They all share Lynx's format: a scrypt-mined genesis whose block-id
// is scrypt while every subsequent (PoS) block's id is plain SHA256d; a per-tx nTime carried on blocks
// whose header nVersion <= 7 (Blackcoin "protocol v2" era rule); active SegWit; and Core-style smart fees.
CoinConfig makeLynxFamily(Coin coin, const char *name, const char *displayName,
                          const char *subversionPrefix, uint8_t p2pkh, uint8_t p2sh) {
    CoinConfig c;
    c.coin = coin;
    c.name = QString::fromLatin1(name);
    c.displayName = QString::fromLatin1(displayName);
    c.subversionPrefixes = {QString::fromLatin1(subversionPrefix)};
    c.mainnetVer = {p2pkh, p2sh};
    c.testnetVer = {111, 196};
    c.regtestVer = {111, 196};
    c.hasTransactionTimestamp = true;             // POS
    c.txTimestampMaxBlockVersion = 7;             // tx nTime on blocks with header nVersion <= 7
    c.allowSegWit = true;                         // BIP141 witness commitment present on-chain
    c.blockIdAlgo = PoWHashAlgo::SHA256d;         // PoS blocks
    c.genesisBlockIdAlgo = PoWHashAlgo::Scrypt;   // genesis block alone is scrypt-mined
    c.estimateFee = EstimateFeeStyle::SmartFee;
    c.peerResourcePath = QString();               // empty -> peering disabled
    return c;
}

const std::array<CoinConfig, 17> & registry() {
    static const std::array<CoinConfig, 17> kRegistry = {{
        makeUnknown(),
        makeBCH(),
        makeBTC(),
        makeLTC(),
        makeDIMI(),
        makeDGC(),
        makeIL8P(),
        makeLYNX(),
        // Lynx multi-chain family (subversion = /<Chain>:, address bytes {p2pkh, p2sh}):
        makeLynxFamily(Coin::Alioth,     "ALIOTH",     "Alioth",     "/Alioth:",     0xd7, 0xef),
        makeLynxFamily(Coin::Borrelly,   "BORRELLY",   "Borrelly",   "/Borrelly:",   0xbd, 0x57),
        makeLynxFamily(Coin::Cassiopeia, "CASSIOPEIA", "Cassiopeia", "/Cassiopeia:", 0xfc, 0xc1),
        makeLynxFamily(Coin::Delphinus,  "DELPHINUS",  "Delphinus",  "/Delphinus:",  0xa3, 0x89),
        makeLynxFamily(Coin::Enceladus,  "ENCELADUS",  "Enceladus",  "/Enceladus:",  0x55, 0x76),
        makeLynxFamily(Coin::Fenrir,     "FENRIR",     "Fenrir",     "/Fenrir:",     0x05, 0xef),
        makeLynxFamily(Coin::Galatea,    "GALATEA",    "Galatea",    "/Galatea:",    0x4a, 0x33),
        makeLynxFamily(Coin::Halley,     "HALLEY",     "Halley",     "/Halley:",     0x36, 0x3f),
        makeLynxFamily(Coin::Indus,      "INDUS",      "Indus",      "/Indus:",      0x0c, 0x0b),
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
