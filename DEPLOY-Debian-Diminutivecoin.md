# Deploying Fulcrum (with Diminutivecoin support) on Debian

> **What's new in this fork**
> Per-coin behaviour has been refactored into a single declarative table
> (`src/CoinConfig.cpp`) plus a PoW-hash registry (`src/PoWHash.cpp`). The DIMI
> integration is now one struct literal in that table — and adding any further
> coin (Peercoin, Dash, PivX, Novacoin, etc.) means appending another struct
> literal and recompiling. See **§ 9. Adding a new coin** at the bottom of this
> document.
>
> Coins currently in the registry: **BCH, BTC, LTC, DIMI** (Diminutivecoin, Tribus),
> **DGC** (Digitalcoin), **IL8P** (Infiniloop, scrypt). Vendored block-ID hashes:
> **SHA256d, Tribus, Scrypt** (≈35 more names are reserved in the enum and throw until
> their sources are vendored). Three mechanisms added for the trickier PoS/altcoin
> formats, all driven from `CoinConfig` (details in § 9):
> - **`genesisBlockIdAlgo`** — hybrid coins that mine *only* the genesis block with an
>   exotic algo but use a different block-ID hash for every later (PoS) block. IL8P:
>   genesis = scrypt, all later blocks = SHA256d.
> - **`txTimestampMaxBlockVersion`** — Blackcoin "protocol-v2" coins that carried a
>   per-tx `nTime` until a switch that bumped the block header version. IL8P: blocks
>   with header `nVersion ≤ 7` carry tx `nTime`; later (BIP9) blocks do not.
> - **block-ID hash abstraction** (`PoWHashAlgo` + `HashHeaderForAlgo`) — coins whose
>   block *identifier* is a non-SHA256d hash (DIMI = Tribus, IL8P genesis = scrypt).

> **Important — binary portability**
> The binary compiled on this Nobara/Fedora machine was built with **gcc 15.2.1**
> and **Qt 6.10.3**. Debian's libraries are older (bookworm: Qt 6.4 / GCC 12,
> trixie: Qt 6.8 / GCC 14). Copying the Fedora binary to Debian will fail at
> startup with errors like `version 'GLIBCXX_3.4.3x' not found` or undefined Qt
> symbols. **Build Fulcrum on the Debian host** (or in a Debian container) so it
> links against Debian's own Qt6 / libstdc++ / glibc. That is what this guide does.

---

## 1. Get the source (with the DIMI changes) onto the Debian host

The DIMI changes live in this working tree. Copy the whole source tree to the
Debian host, e.g.:

```bash
# from this Fedora machine:
rsync -a --exclude build --exclude build-test /home/rob/Public/Fulcrum/ \
      user@debian-host:~/Fulcrum/
```

(Or commit the changes to a branch and `git clone` it on the Debian host.)

## 2. Install build dependencies on Debian

```bash
sudo apt update
sudo apt install -y build-essential qt6-base-dev qt6-base-dev-tools \
                    zlib1g-dev libbz2-dev libzstd-dev pkg-config git
# Optional features (auto-detected by qmake if present):
#   libzmq3-dev        -> ZMQ block/tx notifications
#   libminiupnpc-dev   -> UPnP port mapping
```

- `qt6-base-dev` + `qt6-base-dev-tools` provide `qmake6`, QtCore and QtNetwork
  (the only Qt modules Fulcrum uses).
- `zlib1g-dev` + `libbz2-dev` (and `libzstd-dev`) are needed to link the bundled
  static RocksDB. RocksDB itself ships prebuilt in `staticlibs/rocksdb/`, so no
  `librocksdb-dev` is required. (If you'd rather use Debian's system RocksDB,
  `sudo apt install librocksdb-dev` and qmake will auto-detect and use it.)

## 3. Build

```bash
cd ~/Fulcrum
mkdir -p build && cd build
qmake6 ../Fulcrum.pro
make -j"$(nproc)"
```

The binary is `~/Fulcrum/build/Fulcrum`. Verify:

```bash
./Fulcrum --version
```

## 4. Runtime dependencies (if you build on one Debian box and run on another)

A minimal Debian run host of the **same release** needs only:

```bash
sudo apt install -y libqt6core6 libqt6network6 zlib1g libbz2-1.0 libzstd1
```

(`libqt6network6` transitively pulls `libssl3`, ICU, PCRE2, etc.) On the machine
where you built, these are already satisfied.

## 5. Point Fulcrum at diminutivecoind

`diminutivecoind` must have an RPC interface enabled and (for best performance)
ZMQ. In `~/.diminutivecoin/diminutivecoin.conf`:

```
server=1
rpcuser=YOUR_RPC_USER
rpcpassword=YOUR_STRONG_PASSWORD
rpcport=YOUR_RPC_PORT
# txindex is recommended so Fulcrum can fetch arbitrary transactions:
txindex=1
# Optional but recommended (faster mempool sync):
# zmqpubhashblock=tcp://127.0.0.1:8433
```

Restart `diminutivecoind` and let it finish reindexing if you just enabled
`txindex`.

## 6. Create a Fulcrum config

Save as `~/fulcrum.conf` (a full annotated template is at
`doc/fulcrum-example-config.conf`):

```
# Where Fulcrum stores its database (must exist / be writable):
datadir = /home/USER/fulcrum-db

# Diminutivecoin RPC connection (must match diminutivecoin.conf):
rpcuser = YOUR_RPC_USER
rpcpassword = YOUR_STRONG_PASSWORD
bitcoind = 127.0.0.1:YOUR_RPC_PORT

# Listen interfaces for Electrum clients:
tcp = 0.0.0.0:50001
# ssl = 0.0.0.0:50002          # requires 'cert' and 'key' below
# cert = /path/to/fullchain.pem
# key  = /path/to/privkey.pem

# Diminutivecoin has no Electrum peer network, so leave peering off:
peering = false

# Optional admin RPC (local only) for `FulcrumAdmin` tooling:
# admin = 127.0.0.1:8000
```

```bash
mkdir -p ~/fulcrum-db
```

## 7. Run

```bash
cd ~/Fulcrum/build
./Fulcrum ~/fulcrum.conf
```

On first start you should see it detect the coin, e.g. a log line confirming the
backend is Diminutivecoin (subversion `/Diminutivecoin:13.1.2/`), followed by
block synchronization. Initial sync downloads and indexes the whole chain.

## 8. systemd service

A ready-made, hardened unit ships at
`contrib/diminutivecoin/fulcrum-diminutivecoin.service`. Pick one of the two
setups below.

### Option A — Dedicated system user (recommended)

Works with the unit's security hardening (`ProtectHome=true`). Put the binary,
config and data in system paths:

```bash
sudo install -m755 ~/Fulcrum/build/Fulcrum /usr/local/bin/Fulcrum
sudo useradd --system --no-create-home --shell /usr/sbin/nologin fulcrum
sudo install -d -o fulcrum -g fulcrum /var/lib/fulcrum-diminutivecoin
sudo install -m640 -o fulcrum -g fulcrum ~/fulcrum.conf /etc/fulcrum-diminutivecoin.conf
# then edit /etc/fulcrum-diminutivecoin.conf:  datadir = /var/lib/fulcrum-diminutivecoin
```

Edit the unit's placeholders to:

```
User=fulcrum
Group=fulcrum
ExecStart=/usr/local/bin/Fulcrum /etc/fulcrum-diminutivecoin.conf
ReadWritePaths=/var/lib/fulcrum-diminutivecoin
```

### Option B — Run as your own user from your home dir

Quicker, but relax one hardening option (home is otherwise blocked):

```
User=YOUR_USER
Group=YOUR_USER
ExecStart=/home/YOUR_USER/Fulcrum/build/Fulcrum /home/YOUR_USER/fulcrum.conf
ProtectHome=false
ReadWritePaths=/home/YOUR_USER/fulcrum-db
```

### Install & manage (either option)

```bash
sudo cp contrib/diminutivecoin/fulcrum-diminutivecoin.service \
        /etc/systemd/system/fulcrum-diminutivecoin.service
# (edit the placeholders in that file per Option A or B first)
sudo systemctl daemon-reload
sudo systemctl enable --now fulcrum-diminutivecoin
journalctl -u fulcrum-diminutivecoin -f      # watch sync / logs
```

Do **not** set `syslog = true` in `fulcrum.conf` when running under systemd —
journald already captures Fulcrum's output.

---

### Quick verification once synced
- A client (e.g. an Electron-Cash-style DIMI wallet) connecting to
  `tcp://HOST:50001` should sync balances for `D…` addresses.
- `blockchain.transaction.get` for a known txid returns the raw tx + metadata
  (the `time` field passes through from the daemon).

---

## 9. Adding a new coin

All per-coin behaviour now lives in two files:

| File | Holds |
|---|---|
| `src/CoinConfig.cpp` | One `CoinConfig` struct literal per coin. Identity, base58 ver bytes, cashaddr prefixes, tx/block-format flags, PoS opt-ins, block-ID hash choice, RPC quirks, peer resource path, donation address, RPA + vuln-warning flags. |
| `src/PoWHash.cpp` | One switch case per supported block-ID hash algorithm. `SHA256d`, `Tribus` and `Scrypt` (Litecoin-style scrypt(1024,1,1)) are wired; ~35 further names are reserved in the enum (`X11`, `X16R`, `Lyra2REv2`, `Yescrypt`, `GhostRider`, etc.) but throw `InternalError` until the sources are vendored. |

### Step 1 — append the enumerator

In `src/BTC.h` (current: `Unknown, BCH, BTC, LTC, DIMI, DGC, IL8P`):

```cpp
enum class Coin { Unknown = 0, BCH, BTC, LTC, DIMI, DGC, IL8P, /* NEW: */ PEERCOIN };
```

### Step 2 — append a `CoinConfig` entry

In `src/CoinConfig.cpp`, copy one of the existing `makeXxx()` helpers as a
template, fill it in, and add it to the registry array. Example for a
PoS / hybrid coin with custom base58 bytes and an exotic block-ID hash:

```cpp
CoinConfig makeMyCoin() {
    CoinConfig c;
    c.coin                  = Coin::MYCOIN;
    c.name                  = QStringLiteral("MYCOIN");          // stored in Storage.meta.coin
    c.displayName           = QStringLiteral("My Coin");
    c.subversionPrefixes    = { QStringLiteral("/MyCoinCore:") };
    c.mainnetVer            = {0x37, 0x10};                      // P2PKH, P2SH (mainnet)
    c.testnetVer            = {111, 196};
    c.regtestVer            = {111, 196};

    // tx / block format
    c.allowSegWit           = false;    // true if segwit is active on-chain (see note below)
    c.allowMimbleWimble     = false;
    c.allowCashTokens       = false;
    c.hasTransactionTimestamp = true;   // Peercoin / Blackcoin-style per-tx nTime
    c.hasCoinStake          = true;     // (reserved; future-proofing)
    c.hasPoSBlockSig        = true;     // (reserved; throwIfJunkAtEnd is already off)

    // block-ID hash — pick from PoWHashAlgo (must be wired in PoWHash.cpp)
    c.blockIdAlgo           = PoWHashAlgo::SHA256d;

    // --- optional: hybrid coins that mine only the genesis with a different algo ---
    // If set (and it differs from blockIdAlgo), it is applied to the GENESIS block ALONE,
    // detected by content (hashPrevBlock == all-zeros). Leave unset for normal coins.
    //   c.genesisBlockIdAlgo   = PoWHashAlgo::Scrypt;   // e.g. IL8P: genesis scrypt, rest SHA256d

    // --- optional: Blackcoin "protocol v2" per-tx nTime that was dropped at a switch ---
    // Requires hasTransactionTimestamp = true. When set, txs in a block whose header nVersion
    // <= this value carry nTime (ANY tx version); later blocks carry none. Leave UNSET for the
    // DIMI/Blackcoin-more rule (nTime only on nVersion==1 txs).
    //   c.txTimestampMaxBlockVersion = 7;               // e.g. IL8P

    // RPC quirks
    c.getRawTxVerboseAsInt  = true;     // true for any pre-0.14 Bitcoin-Core-based daemon
    c.estimateFee           = EstimateFeeStyle::LegacyOneArg;

    // peer discovery & misc
    c.peerResourcePath      = QString();        // empty → peering disabled (no servers.json shipped)
    c.defaultDonationAddress = QString();
    c.isRPACapable          = false;
    return c;
}
```

Then add it to the registry:

```cpp
static const std::array<CoinConfig, 8> kRegistry = {{
    makeUnknown(), makeBCH(), makeBTC(), makeLTC(), makeDIMI(), makeDGC(), makeIL8P(),
    makeMyCoin(),                         // <-- new
}};
```

Bump the `std::array<…, N>` size to match (it is `7` today; adding one coin makes it `8`).

### Step 3 — wire a new PoW hash, *only if needed*

Skip this step if your coin uses `SHA256d` for its block ID (the case for the
vast majority of altcoins, even ones mined with an exotic algorithm — Fulcrum
never validates PoW, so the *mining* algorithm doesn't matter; only the
*identifier* hash does). Only coins where the block ID **is** the PoW hash
require a non-default algo. Already wired: **Tribus** (Diminutivecoin) and
**Scrypt** (Litecoin-style scrypt(1024,1,1) — used by IL8P's genesis). To add a
new one:

1. Drop the algorithm's sources under `src/bitcoin/crypto/<algo>/` (e.g.
   `tribus/`, `scrypt/`). Prefer self-contained code — the vendored `scrypt/`
   builds its PBKDF2 on the existing `CHMAC_SHA256`, no OpenSSL dependency.
2. Add the `.cpp`/`.h` files to **both** the `SOURCES` and `HEADERS` lists in
   `Fulcrum.pro`.
3. Add a `case PoWHashAlgo::Foo:` branch in `HashHeaderForAlgo()` **and** a name
   in `AlgoName()`, both inside `src/PoWHash.cpp`.

**Verify before a long build.** Compile a tiny standalone that links your new
algo + `hmac_sha256.cpp` + `sha256.cpp` with `-std=c++20 -Isrc` and the
`HAVE_DECL_*` defines from `Fulcrum.pro`, feed it the header hex from
`<coin>-cli getblockheader <hash> false`, and confirm it reproduces the daemon's
block hash (reversed) before rebuilding the whole server.

### Step 4 — (peering only) ship a `servers.json`

If the new coin has an Electrum-style peer network and you want Fulcrum to
participate:

1. Set `c.peerResourcePath = QStringLiteral(":resources/mycoin/");` in the
   config struct.
2. Add the JSON file(s) under `resources/mycoin/servers.json` (one per network).
3. Add them to `resources.qrc`.

Leaving `peerResourcePath` empty is the right choice for coins like DIMI that
have no public Electrum peer list.

### Step 5 — rebuild & run

```bash
cd ~/Fulcrum/build
qmake6 ../Fulcrum.pro       # only needed if you touched .pro
make -j"$(nproc)"
```

Point the rebuilt binary at the new coin's daemon. On first connection,
Fulcrum's auto-detection iterates the registry and matches your coin by
`subversionPrefixes` against the daemon's `getnetworkinfo.subversion`. The
detected coin is logged at startup and persisted into the database's `meta`
table (the `coin` field), so subsequent restarts pick up the same config
without re-detection.

### Things to watch for the first time

- **Address self-test.** Use `bitcoin-cli`-equivalent's
  `getnewaddress` / `validateaddress`, then call `blockchain.address.*` /
  `blockchain.scripthash.*` against Fulcrum to confirm both legacy base58
  encoding and decoding round-trip with the new ver-bytes.
- **Tx round-trip / `nTime`.** The txid must reserialise identically. For
  timestamped coins the txid is computed *including* the `nTime` field, so a
  wrong rule surfaces as a `ReadCompactSize: size too large` deserialization
  crash the moment sync hits a block whose txs disagree with the config. Two
  variants exist (see the mechanisms in the intro): DIMI reads `nTime` only on
  `nVersion==1` txs; IL8P reads it on *all* tx versions but only in blocks with
  header `nVersion ≤ txTimestampMaxBlockVersion`. **Confirm which** by pulling one
  raw block from each era (`<coin>-cli getblock <hash> false`) and checking that
  the single-tx merkleroot / the txids reproduce with the `nTime` interpretation
  you configured.
- **Block-ID match.** Compare the hash Fulcrum prints during sync at height 0, 1,
  100 with `<coin>-cli getblockhash <height>`. A mismatch at **height 0 only**
  means the coin uses a special genesis algo (`genesisBlockIdAlgo`); a mismatch
  from height 1 onward means the wrong `blockIdAlgo`.
- **Segwit.** Even PoS coins may have segwit active. Tell-tale: the coinbase
  carries a BIP141 witness commitment (an `OP_RETURN` output whose script starts
  `6a24aa21a9ed…`). If present, set `allowSegWit = true` — it is backward-compatible
  with legacy txs, and leaving it off breaks the first native-segwit tx.
- **PoS block trailer.** PoS / hybrid coins put a `vchBlockSig` after the tx
  list. Fulcrum's default `throwIfJunkAtEnd=false` for non-MWEB coins silently
  skips it; no extra plumbing is needed today. The `hasPoSBlockSig` flag is
  reserved for future, stricter validation.

### 9.6 Worked example — a hybrid scrypt-genesis / PoS coin (IL8P)

Infiniloop (IL8P) exercises every mechanism above and is a good template for the
"scrypt-mined genesis, then pure PoS" family. Its `CoinConfig` entry:

```cpp
CoinConfig makeIL8P() {
    CoinConfig c;
    c.coin                    = Coin::IL8P;
    c.name                    = QStringLiteral("IL8P");
    c.displayName             = QStringLiteral("Infiniloop");
    c.subversionPrefixes      = { QStringLiteral("/Infiniloop:") };
    c.mainnetVer              = {0x21, 0x55};
    c.hasTransactionTimestamp = true;                 // PoS lineage
    c.txTimestampMaxBlockVersion = 7;                 // blocks with hdr nVersion<=7 carry tx nTime
    c.allowSegWit             = true;                 // segwit active (BIP141 coinbase commitment)
    c.blockIdAlgo             = PoWHashAlgo::SHA256d; // every PoS block's id
    c.genesisBlockIdAlgo      = PoWHashAlgo::Scrypt;  // genesis (only PoW block) id
    c.estimateFee             = EstimateFeeStyle::SmartFee;
    c.peerResourcePath        = QString();            // peering disabled
    return c;
}
```

Why each non-obvious field is set the way it is — all **confirmed against the live
chain**, not assumed:

- **Genesis vs. rest block-ID.** `getblockhash 0` returned a hash with leading
  zeros that `SHA256d(header)` did *not* reproduce, but `scrypt(header)` did. Yet
  heights 1/10/50 matched `SHA256d`. So genesis (the only mined/PoW block) uses
  scrypt while every later PoS block uses SHA256d — hence `genesisBlockIdAlgo`.
  Detection is by content (`hashPrevBlock == 0`), so no height is threaded through.
- **Tx `nTime` is keyed on the *block* version, not the tx version.** Both eras use
  tx-version 2, but a v7-header block's txs carry `nTime` and a v0x20000002-header
  block's do not. The switch is a single clean height (block 2800000 = last v7 /
  `nTime`; 2800001 = first BIP9 / no `nTime`). `txTimestampMaxBlockVersion = 7`
  captures it (legacy versions ≤ 7, BIP9 versions are 0x20000000+, huge gap).
- **Segwit.** The coinbase's `OP_RETURN 6a24aa21a9ed…` commitment means segwit is
  active → `allowSegWit = true`.

If you hit a `ReadCompactSize: size too large` FATAL at some height, dump that raw
block and check the header version and whether its txs carry `nTime`; the fix is
almost always the `txTimestampMaxBlockVersion` boundary or a block-ID algo.

