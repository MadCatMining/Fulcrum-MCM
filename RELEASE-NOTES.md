# Fulcrum-MCM — Release Notes

**Fulcrum-MCM** is a fork of [Fulcrum](https://github.com/cculianu/Fulcrum), the fast & nimble
Electrum server, extended to serve a range of **scrypt / PoS / Blackcoin-lineage altcoins** in
addition to the upstream BCH / BTC / LTC support — without touching the core indexing engine.

**Forked from upstream Fulcrum `v2.1.1`.** Fulcrum-MCM keeps its **own independent version line**,
starting at **`1.0.0`**: the numbering reflects *this fork's* changes (new functionality → minor,
fixes → patch, breaking changes → major), while the upstream release it derives from is recorded
here, in `src/Common.h` (`UPSTREAM_BASE_VERSION`), and in the binary's `--version` output
(`Forked from Fulcrum v2.1.1`). Servers advertise themselves to Electrum clients as
`Fulcrum-MCM <version>`.

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
| Digitalcoin² | DGC | SHA256d (all blocks, incl. genesis) | no tx `nTime`, SegWit, smart fees |
| Infiniloop | IL8P | scrypt genesis → SHA256d PoS | era-based tx `nTime`, SegWit |
| Lynx | LYNX | scrypt genesis → SHA256d PoS | era-based tx `nTime`, SegWit |
| Lynx family¹ | ALIOTH … INDUS | scrypt genesis → SHA256d PoS | era-based tx `nTime`, SegWit |

¹ The **Lynx multi-chain family**: Alioth, Borrelly, Cassiopeia, Delphinus, Enceladus, Fenrir, Galatea,
Halley, Indus — the same Lynx codebase compiled per `export $NAME=<chain>`. Each reports subversion
`/<Chain>:` and differs only in identity and base58 address bytes; all share Lynx's format. Defined via
a shared `makeLynxFamily()` factory in `src/CoinConfig.cpp`.

² **Digitalcoin**'s daemon is now built from the Lynx multi-chain codebase and switched to Lynx-style PoS
at height 8725000, but it kept its whole original chain history. Its format is a plain Bitcoin-style
chain: SHA256d block-ids for every block (genesis included), **no** per-tx `nTime` in any era, SegWit
(active from height 8725643).
Detected via subversion `/Digitalcoin:` (e.g. `/Digitalcoin:28.4.1/`); the legacy `/Digitalcoin Core:` daemon is no longer supported. **Existing DGC databases synced from
the pre-rebase daemon must be deleted and re-synced.**

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

## Release history

### v1.1.1
- **Fixed DGC sync.** v1.1.0 gave Digitalcoin the IL8P-style per-transaction timestamp rule. DGC's
  transactions never carried an `nTime`, in either the old PoW era or the Lynx PoS era (checked
  against raw blocks 8451, 8725000, 8725001 and 8725977), so v1.1.0 stopped at block 8451, the first
  block with more than one transaction (`ReadCompactSize(): size too large`). DGC is now configured as
  SHA256d block-ids everywhere, no tx `nTime`, SegWit (active from 8725643), smart fees.
  **DGC servers that ran v1.1.0 must delete their database and re-sync**, because blocks 0–8450 were
  indexed with the wrong parsing. No other coin is affected.

### v1.1.0
- **Digitalcoin (DGC) re-targeted to its new Lynx-based chain.** DGC moved off its own multi-algo
  chain and onto the Lynx multi-chain codebase (daemon subversion `/Digitalcoin:28.4.1/`). DGC now
  uses the IL8P / Lynx-family format: PoS blocks with SHA256d block-ids, era-based per-tx `nTime`
  (blocks with header nVersion <= 7), SegWit, `estimatesmartfee`. Its genesis keeps the original 2013
  block-id `5e039e1c…f9c8`, which is **SHA256d** (scrypt is only that block's PoW), so unlike IL8P
  there is no scrypt-genesis override. Address bytes unchanged (P2PKH 30 / P2SH 5); docker default
  RPC port is now 7998. The legacy `/Digitalcoin Core:` daemon is no longer supported. **Existing DGC
  databases must be deleted and re-synced** against the new daemon.
- **Added the Lynx multi-chain family:** Alioth, Borrelly, Cassiopeia, Delphinus, Enceladus, Fenrir,
  Galatea, Halley, Indus (auto-detected by subversion `/<Chain>:`). LYNX now also matches `/Lynx:`.
- Build: `contrib/build/build.sh` reuses an existing toolchain docker image instead of always
  rebuilding it (set `FORCE_IMAGE_REBUILD=1` to force).

### v1.0.2
- **Portable Linux binary — fixes a `SIGILL` (illegal instruction) crash on Intel and other CPUs
  without AMD's SSE4a.** The prebuilt static RocksDB in the build SDK had been compiled with
  `PORTABLE=0` (i.e. `-march=native`) on an AMD build host, which baked an **SSE4a `insertq`**
  instruction into the release binary. SSE4a is AMD-only, so on any Intel host Fulcrum-MCM died the
  moment RocksDB opened the database (`Loading database ...` → `code=killed, status=4/ILL`,
  restart loop). RocksDB is now built with **`PORTABLE=1`** (generic x86-64 baseline; CRC32C stays
  hardware-accelerated via RocksDB's runtime CPUID dispatch), and the binary is verified free of
  SSE4a/XOP/FMA4/TBM opcodes. Runs on any x86-64 CPU, Intel or AMD. **No source change** — this is a
  build-portability fix; the on-disk DB format is unchanged, so existing installs just swap the
  binary and resume indexing. *Note for source builders: build your static RocksDB with
  `-DPORTABLE=1` (or `PORTABLE=<baseline-arch>`), never `-march=native`, if the binary may run on a
  different CPU than the one it was built on.*

### v1.0.1
- Fixed a PROXY-protocol buffered-data bug: a plaintext **tcp** client behind a proxy that sends the
  PROXY header and the first request in the same TCP segment could get no reply (the request was
  stranded in the read buffer after the header was stripped). Added a deferred read-kick in
  `AbstractConnection::on_connected()`, plus a diagnostic warning when an unconsumed PROXY header is
  detected (misconfigured `proxy_protocol` / `proxy_protocol_from`).

### v1.0.0
- First Fulcrum-MCM release. Everything under **"What's new vs. upstream"** above, forked from
  upstream Fulcrum `v2.1.1`.
