# Installing Fulcrum-MCM on a Linux host

Fulcrum-MCM ships as a **fully static x86_64 Linux binary**, so there are no runtime dependencies to
install — you only need to place the binary, a config file, and a systemd unit, and create a
dedicated user + data directory.

There are two ways to do it: the **installer script** (recommended) or the **manual steps** below
(they do exactly the same thing).

> **Prerequisites (the installer can't do these for you):** the coin's daemon must have JSON-RPC
> enabled, **`txindex=1`**, be **fully synced**, and **not** be a pruning node. Fulcrum auto-detects
> the coin from the daemon's `getnetworkinfo.subversion` — there is no `coin=` setting; just point
> each instance at the matching daemon.
>
> **If you enable ssl/wss**, you must **already have a valid TLS cert + key** on the host (Let's
> Encrypt, another CA, or self-signed). The installer *copies* your existing cert/key into a
> fulcrum-owned dir — it does **not** obtain certificates.

---

## Option A — the installer script (recommended)

```bash
curl -fsSLO https://raw.githubusercontent.com/MadCatMining/Fulcrum-MCM/master/contrib/install/install-fulcrum-mcm.sh
sudo bash install-fulcrum-mcm.sh
```

It is **interactive** and **re-runnable — run it once per coin.** It will:

1. Check it's a root/systemd/x86_64 host.
2. Ask for: coin name + ticker; daemon RPC host/port/user/password (hidden); which of tcp/ssl/ws/wss
   to expose (blank = skip that one); TLS cert + key (only if ssl/wss chosen); public hostname
   (→ optional peering/announce); whether it's behind a reverse proxy (→ `proxy_protocol` +
   `proxy_protocol_from`, and optional `ws_x_forwarded_for`); optional local admin port.
3. Download the latest `Fulcrum-MCM-x86_64-linux`, **verify its SHA-256** against the release notes,
   install it to `/usr/local/bin/Fulcrum-MCM`, and confirm it runs.
4. Create the `fulcrum` system user, `/etc/fulcrum`, and `/fulcrum-db/<ticker>`.
5. **Copy** your cert/key into `/etc/fulcrum/<ticker>-ssl/` (owned by `fulcrum`) so perms stay stable
   across Let's Encrypt renewals, and (optionally) write a **renewal hook** (see below).
6. Write `/etc/fulcrum/<ticker>.conf` (`0640 root:fulcrum` — protects the RPC password).
7. Write and enable a hardened `fulcrum-<ticker>.service`.

Everything is **namespaced by ticker**, so DIMI, DGC, IL8P, LYNX … coexist on one host:

| | path |
|---|---|
| binary (shared) | `/usr/local/bin/Fulcrum-MCM` |
| config | `/etc/fulcrum/<ticker>.conf` |
| service | `/etc/systemd/system/fulcrum-<ticker>.service` |
| database | `/fulcrum-db/<ticker>` |
| TLS certs | `/etc/fulcrum/<ticker>-ssl/{fullchain,privkey}.pem` |

---

### Unattended / automation

For provisioning (Ansible, cloud-init, or a loop over many coins), run with `--unattended` — every
answer is read from `FULCRUM_*` env vars, no prompts, and a missing **required** var aborts non-zero:

| var | required | meaning |
|---|:---:|---|
| `FULCRUM_TICKER` | ✅ | ticker (names config/service/db) |
| `FULCRUM_RPC_PORT` `FULCRUM_RPC_USER` `FULCRUM_RPC_PASS` | ✅ | daemon RPC |
| `FULCRUM_COIN_NAME` | — | **display name** (e.g. `Diminutivecoin`) — not the ticker |
| `FULCRUM_RPC_HOST` | — | default `127.0.0.1` |
| `FULCRUM_TCP` `FULCRUM_SSL` `FULCRUM_WS` `FULCRUM_WSS` | — | port per protocol; unset = not exposed |
| `FULCRUM_CERT` `FULCRUM_KEY` | if ssl/wss | existing cert + key paths |
| `FULCRUM_LE_NAME` | — | LE `live/<name>` — **auto-derived** from `FULCRUM_CERT`; set only to override |
| `FULCRUM_LE_HOOK` | — | default `true`; set `false` to skip the renewal hook (other CA / self-signed) |
| `FULCRUM_HOSTNAME` `FULCRUM_PEERING` `FULCRUM_ANNOUNCE` | — | public identity / peering |
| `FULCRUM_PROXY_FROM` `FULCRUM_WS_XFF` | — | reverse-proxy (`proxy_protocol*`, `ws_x_forwarded_for`) |
| `FULCRUM_ADMIN_PORT` | — | local admin port |
| `FULCRUM_START` | — | `yes` = `systemctl start` after enable |

```bash
sudo FULCRUM_COIN_NAME=Diminutivecoin FULCRUM_TICKER=DIMI \
     FULCRUM_RPC_HOST=127.0.0.1 FULCRUM_RPC_PORT=49588 \
     FULCRUM_RPC_USER=username FULCRUM_RPC_PASS='userpassword' \
     FULCRUM_TCP=50011 FULCRUM_SSL=50012 FULCRUM_WSS=50013 \
     FULCRUM_CERT=/etc/letsencrypt/live/electrumx1.diminutivecoin.com/fullchain.pem \
     FULCRUM_KEY=/etc/letsencrypt/live/electrumx1.diminutivecoin.com/privkey.pem \
     FULCRUM_HOSTNAME=electrumx2.diminutivecoin.com \
     FULCRUM_PEERING=true FULCRUM_ANNOUNCE=true FULCRUM_START=yes \
     bash install-fulcrum-mcm.sh --unattended
```
The LE hostname is auto-derived from `FULCRUM_CERT` above (no `FULCRUM_LE_NAME` needed). For a
non-Let's-Encrypt cert, no renewal hook is written; to skip it for an LE cert too, set
`FULCRUM_LE_HOOK=false`.

---

## Option B — manual steps

Replace `<ticker>` (e.g. `dimi`), ports, RPC creds, and cert paths as needed.

**1. Install the binary**
```bash
sudo curl -fL -o /usr/local/bin/Fulcrum-MCM \
  https://github.com/MadCatMining/Fulcrum-MCM/releases/latest/download/Fulcrum-MCM-x86_64-linux
sudo chmod 0755 /usr/local/bin/Fulcrum-MCM
/usr/local/bin/Fulcrum-MCM --version    # should print Fulcrum-MCM <ver> / Forked from Fulcrum v2.1.1
```

**2. User + directories**
```bash
sudo useradd --system --no-create-home --shell /usr/sbin/nologin fulcrum
sudo install -d -m 0755 /etc/fulcrum
sudo install -d -o fulcrum -g fulcrum -m 0750 /fulcrum-db/<ticker>
```

**3. TLS certs (only if using ssl/wss).** Copy into a fulcrum-owned dir so renewals don't break perms:
```bash
sudo install -d -o fulcrum -g fulcrum -m 0750 /etc/fulcrum/<ticker>-ssl
sudo install -o fulcrum -g fulcrum -m 0644 /etc/letsencrypt/live/<host>/fullchain.pem /etc/fulcrum/<ticker>-ssl/fullchain.pem
sudo install -o fulcrum -g fulcrum -m 0640 /etc/letsencrypt/live/<host>/privkey.pem  /etc/fulcrum/<ticker>-ssl/privkey.pem
```

**4. Config** — `sudo nano /etc/fulcrum/<ticker>.conf`:
```ini
datadir = /fulcrum-db/<ticker>

rpcuser = YOUR_RPC_USER
rpcpassword = YOUR_RPC_PASSWORD
bitcoind = 127.0.0.1:YOUR_RPC_PORT

tcp = 0.0.0.0:50001
ssl = 0.0.0.0:50002          # needs cert/key below
wss = 0.0.0.0:50004          # needs wss_cert/wss_key below

cert = /etc/fulcrum/<ticker>-ssl/fullchain.pem
key  = /etc/fulcrum/<ticker>-ssl/privkey.pem
wss_cert = /etc/fulcrum/<ticker>-ssl/fullchain.pem
wss_key  = /etc/fulcrum/<ticker>-ssl/privkey.pem

# hostname = electrumx.example.com
# peering = true
# announce = true

# Behind a trusted reverse proxy (recovers real client IP):
# proxy_protocol = true
# proxy_protocol_from = 192.168.10.5      # or the docker net, e.g. 172.18.0.0/16
# ws_x_forwarded_for = true               # for ws/wss behind an L7 proxy that terminates TLS

# admin = 127.0.0.1:8000                  # optional, for FulcrumAdmin
```
```bash
sudo chown root:fulcrum /etc/fulcrum/<ticker>.conf
sudo chmod 0640 /etc/fulcrum/<ticker>.conf
```

**5. systemd unit** — `sudo nano /etc/systemd/system/fulcrum-<ticker>.service`:
```ini
[Unit]
Description=Fulcrum-MCM Electrum server (<ticker>)
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=fulcrum
Group=fulcrum
ExecStart=/usr/local/bin/Fulcrum-MCM /etc/fulcrum/<ticker>.conf
Restart=on-failure
RestartSec=5
NoNewPrivileges=true
ProtectSystem=strict
ProtectHome=true
PrivateTmp=true
ReadWritePaths=/fulcrum-db/<ticker>

[Install]
WantedBy=multi-user.target
```

**6. Enable + start**
```bash
sudo systemctl daemon-reload
sudo systemctl enable --now fulcrum-<ticker>
journalctl -u fulcrum-<ticker> -f
```

---

## Let's Encrypt renewal

Because Fulcrum reads its **own copies** of the certs (in `/etc/fulcrum/<ticker>-ssl/`), you must
refresh them after each renewal. The installer can write `/etc/fulcrum/<ticker>-renew-certs.sh` that
does this; wire it into certbot:

```bash
sudo certbot renew --deploy-hook /etc/fulcrum/<ticker>-renew-certs.sh
```
The hook copies `fullchain.pem`/`privkey.pem` from `live/<host>` into `<ticker>-ssl` (owned by
`fulcrum`) and restarts the service. Fulcrum-MCM also **hot-reloads** its cert when the files change
(`SSLCertMonitor`), so if you want zero client disruption you can drop the restart from the hook.

## Firewall

Open whichever ports you exposed, e.g. with `ufw`:
```bash
sudo ufw allow 50001/tcp    # repeat for the ssl/ws/wss ports you enabled
```

## Managing the service

```bash
sudo systemctl {start,stop,restart,status} fulcrum-<ticker>
journalctl -u fulcrum-<ticker> -f
```

The full annotated config reference is at [`doc/fulcrum-example-config.conf`](../../doc/fulcrum-example-config.conf).
