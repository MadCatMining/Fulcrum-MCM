# Fulcrum Docker image

Multi-coin Electrum / Fulcrum SPV server in a Debian 13-slim container. Built
from `contrib/docker/Dockerfile`. The entrypoint runs a one-time setup wizard
on first start, generates a `fulcrum.conf` into the data volume, provisions a
TLS cert from one of four modes, and self-manages renewals from then on.

---

## Quick start

### 1. Build the image (or pull a prebuilt one)

```sh
# Multi-arch build (amd64 + arm64), pushes to your registry:
contrib/docker/build.sh yourrepo/fulcrum:latest

# Local single-arch build:
docker build -t fulcrum:latest -f contrib/docker/Dockerfile .
```

### 2. Start the container

The first `docker run` is interactive (TTY required) and walks you through:
coin, daemon endpoint + RPC credentials, listener ports, cert mode, peering.
Subsequent starts re-use the generated `/data/fulcrum.conf`.

Run **non-interactively** by passing every choice as an environment variable —
see the **Environment variables** table below.

#### Linux host

```sh
docker run -it --rm \
    --name fulcrum \
    --add-host=host.docker.internal:host-gateway \
    -v /home/$USER/fulcrum-data:/data \
    -p 50001:50001 -p 50002:50002 \
    fulcrum:latest
```

`--add-host=host.docker.internal:host-gateway` lets the container reach the
daemon running on the host's `127.0.0.1`. Alternative: `--network=host`
(Linux only, drops port isolation; then set `DAEMON_HOST=127.0.0.1`).

#### macOS host (Docker Desktop)

```sh
docker run -it --rm \
    --name fulcrum \
    -v /Users/$USER/fulcrum-data:/data \
    -p 50001:50001 -p 50002:50002 \
    fulcrum:latest
```

`host.docker.internal` is built in — no `--add-host` needed.

#### Windows host (Docker Desktop, PowerShell)

```powershell
docker run -it --rm `
    --name fulcrum `
    -v "${env:USERPROFILE}\fulcrum-data:/data" `
    -p 50001:50001 -p 50002:50002 `
    fulcrum:latest
```

In `cmd.exe`, use `%USERPROFILE%\fulcrum-data:/data`. From inside WSL2 you can
also use `/mnt/c/Users/<user>/fulcrum-data:/data`.

---

## Environment variables

Any variable set in the environment at first run wins over its prompt. Once
`/data/fulcrum.conf` exists the wizard is skipped entirely — re-run it with
`-e FORCE_SETUP=1`.

| Var | Required | Default | Meaning |
|---|---|---|---|
| `COIN` | yes (on first run) | `BCH` | Used only for defaulting `DAEMON_RPC_PORT` and `PEERING`. Fulcrum's actual coin detection is driven by the daemon's `getnetworkinfo.subversion` — see `src/CoinConfig.cpp` for the supported list. |
| `DAEMON_HOST` | yes | `host.docker.internal` | Where the coin daemon is reachable from inside the container. |
| `DAEMON_RPC_PORT` | yes | per-coin (e.g. 8332 for BCH/BTC, 9332 for LTC, 21102 for DIMI) | Matches the daemon's `rpcport=`. |
| `DAEMON_RPC_USER` | yes | — | Matches the daemon's `rpcuser=`. |
| `DAEMON_RPC_PASSWORD` | yes | — | Matches the daemon's `rpcpassword=`. |
| `ELECTRUM_TCP_PORT` | no | `50001` | Container-side TCP listener. |
| `ELECTRUM_SSL_PORT` | no | `50002` | Container-side SSL listener; `0` disables. |
| `SSL_MODE` | no | `self-signed` | One of `self-signed`, `letsencrypt`, `user-supplied`, `none`. |
| `LE_DOMAIN` | LE only | — | Fully qualified domain name for the Let's Encrypt cert. |
| `PEERING` | no | per-coin (`true` for BCH/BTC/LTC, `false` for the rest) | Enables Fulcrum's peer discovery. Must be `false` for coins with no `peerResourcePath` in `CoinConfig.cpp`. |
| `FORCE_SETUP` | no | — | Set to `1` to overwrite `/data/fulcrum.conf` from the current env / prompts. |

---

## SSL cert modes

### `self-signed` (default)

A 1-year, RSA-2048 self-signed cert is generated on first start. The renewal
loop regenerates it whenever it's within 30 days of expiry. Good for private
deployments and lab use; Electrum clients will show a self-signed warning.

Nothing extra to mount.

### `letsencrypt`

Bind-mount a writable `/etc/letsencrypt` directory into the container so cert
state persists across `docker run`s. Bootstrap the cert **once** (either on the
host with your usual certbot install, or interactively inside the running
container — see below). From then on, the container's renewal loop runs
`certbot renew --quiet --keep-until-expiring` on startup and every ~30 days
(with ±30 min jitter). Fulcrum's `SSLCertMonitor` re-reads the new cert
automatically — no container restart required.

```sh
docker run -it --rm \
    --name fulcrum \
    --add-host=host.docker.internal:host-gateway \
    -v /home/$USER/fulcrum-data:/data \
    -v /etc/letsencrypt:/etc/letsencrypt \
    -p 50001:50001 -p 50002:50002 \
    -e SSL_MODE=letsencrypt \
    -e LE_DOMAIN=electrum.example.com \
    fulcrum:latest
```

#### Let's Encrypt bootstrap

The container ships with `certbot` plus the Cloudflare, Route 53 and RFC 2136
DNS-01 plugins — that covers most users. **HTTP-01 / standalone is not
supported** (would require holding port 80 inside the container, which would
defeat the indexer's role).

Pick one bootstrap path:

1. **DNS-01 from inside the container (recommended).** Drop your provider's
   credentials file under `/etc/letsencrypt/credentials.ini`, set its
   permissions to `600`, then:

   ```sh
   # Cloudflare example — token needs Zone:Read + DNS:Edit on the target zone.
   docker exec -it fulcrum certbot certonly \
       --dns-cloudflare \
       --dns-cloudflare-credentials /etc/letsencrypt/credentials.ini \
       -d electrum.example.com \
       -m you@example.com --agree-tos --non-interactive
   ```

   `certbot` saves the authenticator + credentials path under
   `/etc/letsencrypt/renewal/electrum.example.com.conf`, so future
   `certbot renew` runs need no additional flags.

2. **Pre-issued on the host.** Issue + renew the cert outside the container
   with your existing certbot setup; bind-mount the *read-only*
   `/etc/letsencrypt` into the container. The entrypoint detects the missing
   renewal state and **skips** spawning the in-container renewal loop, logging
   a one-line notice.

3. **Other DNS providers.** Either install the appropriate
   `python3-certbot-dns-*` package in a derived image, or use the RFC 2136
   plugin with a small `nsupdate`-capable DNS view.

### `user-supplied`

Bring your own cert + key. Mount them at `/certs/fullchain.pem` and
`/certs/privkey.pem`; the entrypoint symlinks both into Fulcrum's canonical
paths. **No renewal is performed** — you manage their lifecycle. Replacing the
files on disk triggers Fulcrum's hot-reload.

```sh
docker run -it --rm \
    --name fulcrum \
    --add-host=host.docker.internal:host-gateway \
    -v /home/$USER/fulcrum-data:/data \
    -v /etc/my-certs:/certs:ro \
    -p 50001:50001 -p 50002:50002 \
    -e SSL_MODE=user-supplied \
    fulcrum:latest
```

### `none`

No SSL listener. The container only exposes TCP. Useful behind a reverse
proxy that terminates TLS.

---

## Renewal in one paragraph

The entrypoint forks a background renewal loop after setup. On startup it runs
one opportunistic renewal; then it sleeps ~30 days (with jitter) and repeats
until the container stops. `self-signed` mode regenerates the cert whenever
it's within 30 days of expiry. `letsencrypt` mode runs
`certbot renew --keep-until-expiring`, which is a no-op until certbot itself
decides the cert is renewable (~30 days from expiry). Fulcrum watches its cert
files via `QFileSystemWatcher` and re-reads them on change — no SIGHUP, no
container restart. `docker stop` propagates `SIGTERM` to both Fulcrum and the
renewal loop and exits within a few seconds even mid-`sleep`.

---

## Compose

A worked example lives at `contrib/docker/docker-compose.example.yml`.

---

## Troubleshooting

**`Cannot resolve 'host.docker.internal' inside the container.`** — You're on
Linux without `--add-host=host.docker.internal:host-gateway`. Add the flag or
switch to `--network=host` and set `DAEMON_HOST=127.0.0.1`.

**`TCP connection to <host>:<port> failed.`** — The daemon is unreachable from
the container's network namespace. Common causes: daemon is bound to
`127.0.0.1` on a host where the container can't reach loopback; firewall;
wrong port. On Linux, ensure the daemon's `rpcbind=` and `rpcallowip=` permit
the Docker bridge subnet (often `172.17.0.0/16`).

**`Let's Encrypt live directory ... not found.`** — Either you haven't
bootstrapped the cert yet (see § Let's Encrypt bootstrap), or you forgot to
bind-mount `/etc/letsencrypt`.

**The cert isn't renewing.** — In `letsencrypt` mode, the renewal loop only
acts if certbot itself thinks the cert is due. Force a renewal with
`docker exec fulcrum certbot renew --force-renewal`. In `self-signed` mode,
the loop only rotates when the cert is within 30 days of expiry; for testing,
delete `/data/fulcrum.crt` and restart.

**I want to re-run the setup wizard.** —
`docker run -e FORCE_SETUP=1 -it ...` or `rm /data/fulcrum.conf` then restart.
