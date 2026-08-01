# Fulcrum (multi-altcoin fork) — Release Notes

A fork of [Fulcrum](https://github.com/cculianu/Fulcrum), the fast & nimble Electrum server,
extended to serve a range of **scrypt / PoS / Blackcoin-lineage altcoins** in addition to the
upstream BCH / BTC / LTC support — without touching the core indexing engine.

Based on upstream **v2.1.1**.

## Summary

Upstream Fulcrum assumes a Bitcoin-family block/transaction format: SHA256d block IDs, no
per-transaction timestamps, and a fixed set of coins wired in with scattered `if (coin == X)`
checks. This fork replaces that with a **single declarative per-coin table** and a **pluggable
block-ID hash layer**, then uses them to add several hybrid PoW-genesis / PoS coins. It also adds
reverse-proxy (PROXY-protocol) support and Docker/deployment conveniences.

## What's new vs. upstream

### 1. Declarative per-coin configuration (`src/CoinConfig.{h,cpp}`)
- Every per-coin quirk that upstream spread across `BTC.cpp`, `BTC_Address.cpp`, `BitcoinD.cpp`,
  `Controller.h`, `Servers.cpp`, `PeerMgr.cpp`, `SrvMgr.cpp` and `Storage.cpp` is now one struct
  entry in a central registry.
- **Adding a coin = one `makeXxx()` entry** (subversion prefix for auto-detection, address version
  bytes, block-ID hash, tx/block format flags). Nothing else needs editing for already-supported
  formats.
- Coins detected automatically from the daemon's `getnetworkinfo.subversion`.

### 2. Pluggable block-ID hash (`src/PoWHash.{h,cpp}`)
- `enum class PoWHashAlgo { SHA256d, Tribus, Scrypt, … }` + `HashHeaderForAlgo()`.
- Fulcrum identifies blocks by hashing the 80-byte header and comparing to the daemon's
  `getblockhash`; upstream hard-codes SHA256d. This fork dispatches per-coin.
- **Vendored, dependency-free hash implementations** under `src/bitcoin/crypto/`:
  - `scrypt/` — Litecoin-style `scrypt(N=1024, r=1, p=1)` (PBKDF2-HMAC-SHA256 + Salsa20/8 ROMix),
    built on the vendored `CHMAC_SHA256`. **No OpenSSL dependency.**
  - `tribus/` — Tribus (JH/Keccak/Echo) for Diminutivecoin.

### 3. Hybrid PoW-genesis / PoS coins
- Some coins mine **only the genesis block** with an exotic PoW algo but use plain SHA256d for
  every later (PoS) block. New `genesisBlockIdAlgo` applies a special hash to the genesis block
  **alone**, detected by content (`hashPrevBlock == all-zeros`) so no block height has to be
  threaded through the ~15 hashing call sites.

### 4. Per-transaction timestamp (`nTime`) support
- Peercoin/Novacoin/Blackcoin-lineage coins carry a 4-byte `nTime` inside each transaction, which
  is part of the txid hash. Two rules are supported:
  - **DIMI / Blackcoin-more:** `nTime` only on `nVersion == 1` transactions.
  - **Blackcoin "protocol v2" lineage (IL8P/LYNX):** `nTime` on **all** tx versions until a
    protocol switch bumped the **block header version** past 7. This is keyed on the *block header
    version*, not the tx version — a new `SERIALIZE_TRANSACTION_TIMESTAMP_ALLVERS` stream flag plus
    `txTimestampMaxBlockVersion` config drive it, decided per-block in `BTC::Deserialize`.
- The txindex-probe path (which deserializes a single historical transaction standalone) derives
  the correct era from that tx's own block header, so post-sync validation succeeds.

### 5. Coexisting SegWit + PoS + PoS block-signature handling
- SegWit auto-works on PoS coins whose coinbase carries a BIP141 witness commitment
  (`OP_RETURN aa21a9ed…`); enabling is backward-compatible with legacy transactions.
- Trailing PoS block-signature (`vchBlockSig`) is tolerated/ignored on non-MimbleWimble coins.

### 6. New coins supported
In addition to upstream **BCH / BTC / LTC**:
| Coin | Ticker | Block-ID hash | Notable format |
|------|--------|---------------|----------------|
| Diminutivecoin | DIMI | Tribus | tx `nTime` (v1), Core 0.13 RPC quirks |
| Digitalcoin | DGC | SHA256d | legacy `estimatefee` |
| Infiniloop | IL8P | scrypt genesis → SHA256d PoS | era-based tx `nTime`, SegWit |
| Lynx | LYNX | scrypt genesis → SHA256d PoS | era-based tx `nTime`, SegWit |

### 7. Reverse-proxy / PROXY-protocol support
- `src/ProxyProtocol.{h,cpp}` — accepts HAProxy **PROXY protocol v1 and v2**, so the real client
  IP is preserved when running behind Nginx Proxy Manager / HAProxy.
- Docker `ws`/`wss` listeners for terminating WebSocket connections behind a proxy.

### 8. Build & deployment
- **`Fulcrum.pro`:** optional `/opt/fulcrum-sdk` block is guarded by `exists()`, so an Alpine/musl
  SDK build links its static rocksdb/jemalloc/etc., while a plain host clone falls back cleanly to
  the bundled static libs (no more unconditional `-lsnappy/-llz4/-ljemalloc` link failures).
- **Docker:** `contrib/docker/coin-defaults.sh` (per-coin default RPC ports & peering),
  `docker-compose.example.yml`, and an expanded `README.md`.
- Deployment guide: `DEPLOY-Debian-Diminutivecoin.md`, including a worked example of adding a new
  scrypt-genesis/PoS coin end to end.

## Compatibility
- **No change to the on-disk format or behaviour for BCH / BTC / LTC** — all new code paths are
  gated behind per-coin config; the default (`SHA256d`, no tx `nTime`, no genesis special-case)
  reproduces upstream exactly.
- Fulcrum never validates proof-of-work; only the block *identifier* hash matters, so PoS coins
  index correctly despite the engine being PoW-oriented.

## Upgrading / adding a coin
See `DEPLOY-Debian-Diminutivecoin.md` §9. In short: add a `Coin::XXX` enumerator, a `makeXXX()`
entry in the `CoinConfig` registry (bump the `std::array` size), set the subversion prefix, address
version bytes, and block-ID hash. If it needs a new PoW hash, append to `PoWHashAlgo`, add a `case`
to `HashHeaderForAlgo`, and vendor the implementation under `src/bitcoin/crypto/`.
