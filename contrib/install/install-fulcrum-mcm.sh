#!/usr/bin/env bash
#
# install-fulcrum-mcm.sh — interactive installer for a single Fulcrum-MCM instance on a Linux host.
#
# Fulcrum-MCM ships as a fully static x86_64 Linux binary, so there are no runtime dependencies to
# install — this script only downloads the binary, generates a config + hardened systemd unit, and
# creates a dedicated system user and data directory.
#
# It is per-coin and re-runnable: run it once per coin you want to serve. Files are namespaced by the
# coin ticker so multiple instances coexist on one host:
#     binary   : /usr/local/bin/Fulcrum-MCM              (shared by all instances)
#     config   : /etc/fulcrum/<ticker>.conf
#     service  : /etc/systemd/system/fulcrum-<ticker>.service
#     database : /fulcrum-db/<ticker>
#     runs as  : system user 'fulcrum'
#
# NOTE: the coin is auto-detected by Fulcrum from the daemon's getnetworkinfo.subversion — the ticker
# you give here is only used for naming. Point this instance at the matching coin's daemon.
#
set -euo pipefail

# ---- constants (override via env if you like) --------------------------------------------------
REPO="${REPO:-MadCatMining/Fulcrum-MCM}"
ASSET="${ASSET:-Fulcrum-MCM-x86_64-linux}"
BIN_PATH="${BIN_PATH:-/usr/local/bin/Fulcrum-MCM}"
CONF_DIR="${CONF_DIR:-/etc/fulcrum}"
DB_ROOT="${DB_ROOT:-/fulcrum-db}"
SVC_DIR="${SVC_DIR:-/etc/systemd/system}"
RUN_USER="${RUN_USER:-fulcrum}"

# ---- pretty output ------------------------------------------------------------------------------
c_g(){ printf '\033[1;32m%s\033[0m\n' "$*"; }
c_y(){ printf '\033[1;33m%s\033[0m\n' "$*"; }
c_r(){ printf '\033[1;31m%s\033[0m\n' "$*"; }
info(){ printf '  %s\n' "$*"; }
die(){ c_r "ERROR: $*"; exit 1; }

ask(){ # ask VAR "Prompt" ["default"]
    local __v=$1 __p=$2 __d=${3:-} __in
    if [ -n "$__d" ]; then read -r -p "$__p [$__d]: " __in || true; __in=${__in:-$__d}
    else read -r -p "$__p: " __in || true; fi
    printf -v "$__v" '%s' "$__in"
}
ask_secret(){ local __v=$1 __p=$2 __in; read -r -s -p "$__p: " __in || true; echo; printf -v "$__v" '%s' "$__in"; }
yesno(){ local __p=$1 __d=${2:-N} __in; read -r -p "$__p [$( [ "$__d" = Y ] && echo 'Y/n' || echo 'y/N')]: " __in || true; __in=${__in:-$__d}; [[ $__in =~ ^[Yy]$ ]]; }
norm_bool(){ case "${1:-}" in 1|[Tt]rue|TRUE|[Yy]es|YES|[Yy]) echo true;; *) echo false;; esac; }

# ---- args ---------------------------------------------------------------------------------------
UNATTENDED=0
usage(){ cat <<'USAGE'
Usage: install-fulcrum-mcm.sh [--unattended]

Interactive by default. With --unattended, every answer is read from FULCRUM_* env vars
(no prompts; a missing required var aborts):

  required : FULCRUM_TICKER FULCRUM_RPC_PORT FULCRUM_RPC_USER FULCRUM_RPC_PASS
  optional : FULCRUM_COIN_NAME (display name, NOT the ticker) FULCRUM_RPC_HOST(=127.0.0.1)
             FULCRUM_TCP FULCRUM_SSL FULCRUM_WS FULCRUM_WSS   (port per protocol; unset = not exposed)
             FULCRUM_CERT FULCRUM_KEY                          (required if ssl/wss set)
             FULCRUM_LE_NAME  (LE live/<name>; auto-derived from FULCRUM_CERT — override only)
             FULCRUM_LE_HOOK  (default true; set false to skip the LE renewal hook: other CA / self-signed)
             FULCRUM_HOSTNAME FULCRUM_PEERING FULCRUM_ANNOUNCE
             FULCRUM_PROXY_FROM FULCRUM_WS_XFF FULCRUM_ADMIN_PORT
             FULCRUM_START   (yes = systemctl start after enable)
USAGE
}
for a in "$@"; do case "$a" in -u|--unattended) UNATTENDED=1 ;; -h|--help) usage; exit 0 ;; *) die "unknown argument: $a (see --help)";; esac; done

# ---- pre-flight ---------------------------------------------------------------------------------
[ "$(id -u)" -eq 0 ] || die "please run as root (needed to write /usr/local/bin, /etc, and create a user)."
[ "$(uname -s)" = Linux ] || die "this installer is for Linux hosts only."
case "$(uname -m)" in x86_64|amd64) ;; *) die "the published binary is x86_64 only; this host is $(uname -m). Build from source for this arch." ;; esac
command -v systemctl >/dev/null || die "systemd (systemctl) not found — this installer targets systemd hosts."
DL=""; command -v curl >/dev/null && DL=curl; [ -z "$DL" ] && command -v wget >/dev/null && DL=wget
[ -n "$DL" ] || die "need curl or wget to download the release."

c_g "== Fulcrum-MCM installer =="
echo

# ---- gather inputs ------------------------------------------------------------------------------
# NOTE: enabling ssl/wss assumes you ALREADY have a valid certificate + key on this host (from
# Let's Encrypt, another CA, or self-signed) — this installer does NOT obtain certificates, it only
# copies your existing cert/key into a fulcrum-owned dir and points the config at them.
if [ "$UNATTENDED" = 1 ]; then
    # Non-interactive: every answer comes from FULCRUM_* env vars. Required ones must be set.
    COIN_NAME="${FULCRUM_COIN_NAME:-}"          # display name only (e.g. Diminutivecoin); NOT the ticker
    TICKER_RAW="${FULCRUM_TICKER:-}"
    RPC_HOST="${FULCRUM_RPC_HOST:-127.0.0.1}"
    RPC_PORT="${FULCRUM_RPC_PORT:-}"
    RPC_USER="${FULCRUM_RPC_USER:-}"
    RPC_PASS="${FULCRUM_RPC_PASS:-}"
    PORT_TCP="${FULCRUM_TCP:-}"; PORT_SSL="${FULCRUM_SSL:-}"; PORT_WS="${FULCRUM_WS:-}"; PORT_WSS="${FULCRUM_WSS:-}"
    CERT_SRC="${FULCRUM_CERT:-}"; KEY_SRC="${FULCRUM_KEY:-}"
    HOSTNAME_PUB="${FULCRUM_HOSTNAME:-}"
    PEERING="$(norm_bool "${FULCRUM_PEERING:-}")"; ANNOUNCE="$(norm_bool "${FULCRUM_ANNOUNCE:-}")"
    PROXY_FROM="${FULCRUM_PROXY_FROM:-}"; WS_XFF="$(norm_bool "${FULCRUM_WS_XFF:-}")"
    ADMIN_PORT="${FULCRUM_ADMIN_PORT:-}"
    LE_NAME="${FULCRUM_LE_NAME:-}"; GEN_HOOK="false"
    [ -n "$TICKER_RAW" ] || die "unattended: FULCRUM_TICKER is required"
    [ -n "$RPC_PORT" ]   || die "unattended: FULCRUM_RPC_PORT is required"
    [ -n "$RPC_USER" ]   || die "unattended: FULCRUM_RPC_USER is required"
    [ -n "$RPC_PASS" ]   || die "unattended: FULCRUM_RPC_PASS is required"
    if [ -n "$PORT_SSL$PORT_WSS" ]; then
        { [ -n "$CERT_SRC" ] && [ -n "$KEY_SRC" ]; } || die "unattended: ssl/wss requested but FULCRUM_CERT and FULCRUM_KEY are not both set"
        [ -f "$CERT_SRC" ] || die "unattended: FULCRUM_CERT is not a readable file: $CERT_SRC (point at fullchain.pem, NOT the directory)"
        [ -f "$KEY_SRC" ]  || die "unattended: FULCRUM_KEY is not a readable file: $KEY_SRC (point at privkey.pem)"
    fi
    # LE renewal hook: only for Let's Encrypt certs. Auto-derive live/<name> from the cert path unless
    # FULCRUM_LE_NAME overrides it; set FULCRUM_LE_HOOK=false to skip (e.g. other CA / self-signed).
    if [ -z "$LE_NAME" ] && [[ "$CERT_SRC" == */letsencrypt/live/*/* ]]; then
        LE_NAME="$(printf '%s' "$CERT_SRC" | sed -E 's#.*/letsencrypt/live/([^/]+)/.*#\1#')"
    fi
    if [ -n "$LE_NAME" ] && [ -n "$CERT_SRC" ] && [ "$(norm_bool "${FULCRUM_LE_HOOK:-true}")" = true ]; then GEN_HOOK="true"; fi
else
    ask COIN_NAME  "Coin display name (e.g. Diminutivecoin)"
    ask TICKER_RAW "Coin ticker (e.g. DIMI) — used for file/service names"

    echo; c_y "Daemon (coind) JSON-RPC connection:"
    ask RPC_HOST "  RPC host" "127.0.0.1"
    ask RPC_PORT "  RPC port"
    [ -n "$RPC_PORT" ] || die "RPC port is required."
    ask RPC_USER "  RPC username"
    ask_secret RPC_PASS "  RPC password"

    echo; c_y "Listener ports (leave blank to NOT expose that protocol):"
    ask PORT_TCP "  tcp  (plaintext Electrum)" "50001"
    ask PORT_SSL "  ssl  (TLS Electrum)"        ""
    ask PORT_WS  "  ws   (plaintext WebSocket)" ""
    ask PORT_WSS "  wss  (TLS WebSocket)"       ""

    CERT_SRC=""; KEY_SRC=""; LE_NAME=""; GEN_HOOK="false"
    if [ -n "$PORT_SSL" ] || [ -n "$PORT_WSS" ]; then
        echo; c_y "ssl/wss need a TLS certificate + key (PEM). Leave blank to disable those listeners."
        c_y "NOTE: this does NOT obtain a certificate — you must ALREADY have a valid cert+key on this"
        c_y "      host (Let's Encrypt, another CA, or self-signed). It is copied into a fulcrum-owned dir."
        ask CERT_SRC "  Path to fullchain/cert .pem"
        ask KEY_SRC  "  Path to private key .pem"
        if [ -z "$CERT_SRC" ] || [ -z "$KEY_SRC" ]; then
            c_y "  No cert/key given → disabling ssl and wss."; PORT_SSL=""; PORT_WSS=""
        else
            [ -f "$CERT_SRC" ] || c_y "  WARNING: $CERT_SRC is not a readable FILE — give the fullchain.pem file, NOT the directory."
            [ -f "$KEY_SRC" ]  || c_y "  WARNING: $KEY_SRC is not a readable FILE — give the privkey.pem file."
            # A renewal hook only makes sense for Let's Encrypt; detect an LE source to pre-answer.
            if [[ "$CERT_SRC" == */letsencrypt/live/*/* ]]; then
                LE_NAME="$(printf '%s' "$CERT_SRC" | sed -E 's#.*/letsencrypt/live/([^/]+)/.*#\1#')"
            fi
            c_y "  (Let's Encrypt only — decline for another CA or a self-signed cert and manage renewals yourself.)"
            if yesno "  Generate a Let's Encrypt renewal hook (re-copies fresh certs + restarts on renew)?" "$( [ -n "$LE_NAME" ] && echo Y || echo N )"; then
                GEN_HOOK="true"
                ask LE_NAME "    Let's Encrypt cert name (the /etc/letsencrypt/live/<NAME> dir)" "${LE_NAME:-}"
                [ -n "$LE_NAME" ] || { c_y "    no LE name given → skipping hook."; GEN_HOOK="false"; }
            fi
        fi
    fi

    echo
    ask HOSTNAME_PUB "Public hostname pointed at this server (blank to skip)" ""
    PEERING="false"; ANNOUNCE="false"
    if [ -n "$HOSTNAME_PUB" ] && yesno "Enable peering + announce for $HOSTNAME_PUB?" N; then PEERING="true"; ANNOUNCE="true"; fi

    echo
    PROXY_FROM=""; WS_XFF="false"
    if yesno "Is this server behind a reverse proxy (nginx/NPM/HAProxy)?" N; then
        ask PROXY_FROM "  Trusted proxy source address/subnet (proxy_protocol_from, e.g. 192.168.10.5 or 172.18.0.0/16)"
        if [ -n "$PORT_WS" ] || [ -n "$PORT_WSS" ]; then
            yesno "  Also trust X-Forwarded-For for ws/wss (L7/HTTP proxy that terminates TLS)?" N && WS_XFF="true"
        fi
    fi

    ADMIN_PORT=""
    yesno "Enable a local admin port for FulcrumAdmin tooling (127.0.0.1)?" N && ask ADMIN_PORT "  Admin port" "8000"
fi

# ---- validate + derive names/paths (both modes) -------------------------------------------------
[ -n "$TICKER_RAW" ] || die "a ticker is required (interactive prompt, or FULCRUM_TICKER)."
TICKER=$(printf '%s' "$TICKER_RAW" | tr '[:upper:]' '[:lower:]' | tr -cd 'a-z0-9-')
[ -n "$TICKER" ] || die "ticker must contain letters/numbers."

DB_DIR="$DB_ROOT/$TICKER"
CONF_FILE="$CONF_DIR/$TICKER.conf"
SVC_FILE="$SVC_DIR/fulcrum-$TICKER.service"
SSL_DIR="$CONF_DIR/$TICKER-ssl"

echo; c_g "Summary:"
info "coin/ticker : $COIN_NAME / $TICKER"
info "binary      : $BIN_PATH   (from latest $REPO release)"
info "config      : $CONF_FILE"
info "service     : fulcrum-$TICKER.service"
info "database    : $DB_DIR"
info "run as user : $RUN_USER"
info "daemon RPC  : $RPC_USER@$RPC_HOST:$RPC_PORT"
info "listeners   : tcp=${PORT_TCP:--} ssl=${PORT_SSL:--} ws=${PORT_WS:--} wss=${PORT_WSS:--}"
[ -n "$CERT_SRC" ] && info "tls cert    : $CERT_SRC -> $SSL_DIR/{fullchain,privkey}.pem"
[ "$GEN_HOOK" = true ] && info "renew hook  : $CONF_DIR/$TICKER-renew-certs.sh (LE: $LE_NAME)"
[ -n "$HOSTNAME_PUB" ] && info "hostname    : $HOSTNAME_PUB (peering=$PEERING announce=$ANNOUNCE)"
[ -n "$PROXY_FROM" ] && info "proxy_from  : $PROXY_FROM (ws_x_forwarded_for=$WS_XFF)"
echo
[ "$UNATTENDED" = 1 ] || yesno "Proceed with installation?" Y || { c_y "Aborted."; exit 0; }

# ---- download binary ----------------------------------------------------------------------------
echo; c_y "Downloading latest $ASSET ..."
URL="https://github.com/$REPO/releases/latest/download/$ASSET"
TMP="$(mktemp)"; trap 'rm -f "$TMP"' EXIT
if [ "$DL" = curl ]; then curl -fL --retry 3 -o "$TMP" "$URL"; else wget -O "$TMP" "$URL"; fi
[ -s "$TMP" ] || die "download failed / empty file."

# best-effort checksum verification against the release notes
WANT_SHA=""
if command -v sha256sum >/dev/null; then
    API="https://api.github.com/repos/$REPO/releases/latest"
    BODY="$( { [ "$DL" = curl ] && curl -fsSL "$API"; } 2>/dev/null || true)"
    WANT_SHA="$(printf '%s' "$BODY" | grep -oiE "[0-9a-f]{64}[^0-9a-f][^\"]*${ASSET}" | grep -oiE '^[0-9a-f]{64}' | head -1 || true)"
    if [ -n "$WANT_SHA" ]; then
        GOT_SHA="$(sha256sum "$TMP" | awk '{print $1}')"
        [ "$GOT_SHA" = "$WANT_SHA" ] && c_g "  checksum OK ($GOT_SHA)" || die "checksum MISMATCH! expected $WANT_SHA got $GOT_SHA"
    else
        c_y "  (could not read expected checksum from the release notes — skipping verification)"
    fi
fi
install -m 0755 "$TMP" "$BIN_PATH"
# sanity: must run and self-identify
"$BIN_PATH" --version 2>/dev/null | grep -q "Fulcrum-MCM" || die "downloaded binary did not run / identify as Fulcrum-MCM."
c_g "  installed $("$BIN_PATH" --version 2>/dev/null | head -1) -> $BIN_PATH"

# ---- user + directories -------------------------------------------------------------------------
if ! id -u "$RUN_USER" >/dev/null 2>&1; then
    useradd --system --no-create-home --shell /usr/sbin/nologin "$RUN_USER"
    c_g "  created system user '$RUN_USER'"
fi
install -d -m 0755 "$CONF_DIR"
install -d -o "$RUN_USER" -g "$RUN_USER" -m 0750 "$DB_DIR"
c_g "  data dir: $DB_DIR"

# ---- copy certs into a fulcrum-owned dir (stable perms; survives LE renewals) -------------------
if [ -n "$CERT_SRC" ]; then
    install -d -o "$RUN_USER" -g "$RUN_USER" -m 0750 "$SSL_DIR"
    if [ -f "$CERT_SRC" ] && [ -f "$KEY_SRC" ]; then
        if install -o "$RUN_USER" -g "$RUN_USER" -m 0644 "$(readlink -f "$CERT_SRC" 2>/dev/null || echo "$CERT_SRC")" "$SSL_DIR/fullchain.pem" \
           && install -o "$RUN_USER" -g "$RUN_USER" -m 0640 "$(readlink -f "$KEY_SRC" 2>/dev/null || echo "$KEY_SRC")" "$SSL_DIR/privkey.pem"; then
            c_g "  copied cert/key into $SSL_DIR (owned by $RUN_USER)"
        else
            c_y "  WARNING: could not copy cert/key into $SSL_DIR — place fullchain.pem + privkey.pem there (owned by $RUN_USER) before starting."
        fi
    else
        c_y "  cert/key are not readable files yet — created $SSL_DIR; place the fullchain.pem + privkey.pem"
        c_y "  FILES there (owned by $RUN_USER) before starting. (Give the .pem files, NOT a directory.)"
    fi
    if [ "$GEN_HOOK" = true ]; then
        HOOK="$CONF_DIR/$TICKER-renew-certs.sh"
        cat > "$HOOK" <<HOOKEOF
#!/usr/bin/env bash
# Renewal helper for fulcrum-$TICKER — copies fresh Let's Encrypt certs into the fulcrum-owned ssl
# dir and restarts the service. Wire it in so it runs after each renewal, e.g.:
#     certbot renew --deploy-hook $HOOK
# Fulcrum-MCM also hot-reloads its cert when the files change (SSLCertMonitor), so the restart below
# is belt-and-suspenders — replace it with ':' if you prefer zero client disruption.
set -euo pipefail
install -o $RUN_USER -g $RUN_USER -m 0644 "/etc/letsencrypt/live/$LE_NAME/fullchain.pem" "$SSL_DIR/fullchain.pem"
install -o $RUN_USER -g $RUN_USER -m 0640 "/etc/letsencrypt/live/$LE_NAME/privkey.pem"  "$SSL_DIR/privkey.pem"
systemctl restart fulcrum-$TICKER
HOOKEOF
        chmod 0755 "$HOOK"
        c_g "  wrote renewal hook: $HOOK"
    fi
fi

# ---- write config -------------------------------------------------------------------------------
c_y "Writing $CONF_FILE ..."
{
    echo "# Fulcrum-MCM config for $COIN_NAME ($TICKER). Generated by install-fulcrum-mcm.sh."
    echo "# Coin is auto-detected from the daemon subversion; point this at the $COIN_NAME daemon."
    echo
    echo "datadir = $DB_DIR"
    echo
    echo "# Daemon JSON-RPC (must match the coin daemon's .conf):"
    echo "rpcuser = $RPC_USER"
    echo "rpcpassword = $RPC_PASS"
    echo "bitcoind = $RPC_HOST:$RPC_PORT"
    echo
    echo "# Listener interfaces for Electrum clients:"
    [ -n "$PORT_TCP" ] && echo "tcp = 0.0.0.0:$PORT_TCP"
    [ -n "$PORT_SSL" ] && echo "ssl = 0.0.0.0:$PORT_SSL"
    [ -n "$PORT_WS"  ] && echo "ws = 0.0.0.0:$PORT_WS"
    [ -n "$PORT_WSS" ] && echo "wss = 0.0.0.0:$PORT_WSS"
    if [ -n "$CERT_SRC" ]; then
        echo
        [ -n "$PORT_SSL" ] && { echo "cert = $SSL_DIR/fullchain.pem"; echo "key  = $SSL_DIR/privkey.pem"; }
        [ -n "$PORT_WSS" ] && { echo "wss_cert = $SSL_DIR/fullchain.pem"; echo "wss_key  = $SSL_DIR/privkey.pem"; }
    fi
    if [ -n "$HOSTNAME_PUB" ]; then
        echo
        echo "hostname = $HOSTNAME_PUB"
    fi
    echo
    echo "peering = $PEERING"
    echo "announce = $ANNOUNCE"
    if [ -n "$PROXY_FROM" ]; then
        echo
        echo "# Behind a trusted reverse proxy (recovers real client IP from the PROXY-protocol header):"
        echo "proxy_protocol = true"
        echo "proxy_protocol_from = $PROXY_FROM"
        [ "$WS_XFF" = true ] && echo "ws_x_forwarded_for = true"
    fi
    if [ -n "$ADMIN_PORT" ]; then
        echo
        echo "admin = 127.0.0.1:$ADMIN_PORT"
    fi
} > "$CONF_FILE"
chown root:"$RUN_USER" "$CONF_FILE"
chmod 0640 "$CONF_FILE"   # contains the RPC password → not world-readable, readable by the service
c_g "  wrote $CONF_FILE (0640 root:$RUN_USER)"

# ---- write systemd unit -------------------------------------------------------------------------
c_y "Writing $SVC_FILE ..."
cat > "$SVC_FILE" <<UNIT
[Unit]
Description=Fulcrum-MCM Electrum server ($COIN_NAME / $TICKER)
Documentation=https://github.com/$REPO
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=$RUN_USER
Group=$RUN_USER
ExecStart=$BIN_PATH $CONF_FILE
Restart=on-failure
RestartSec=5
# --- hardening ---
NoNewPrivileges=true
ProtectSystem=strict
ProtectHome=true
PrivateTmp=true
PrivateDevices=true
ProtectControlGroups=true
ProtectKernelModules=true
ProtectKernelTunables=true
RestrictSUIDSGID=true
LockPersonality=true
ReadWritePaths=$DB_DIR

[Install]
WantedBy=multi-user.target
UNIT
c_g "  wrote $SVC_FILE"

systemctl daemon-reload
systemctl enable "fulcrum-$TICKER.service" >/dev/null 2>&1 || true
if [ "$(norm_bool "${FULCRUM_START:-}")" = true ]; then
    systemctl start "fulcrum-$TICKER.service" && c_g "  started fulcrum-$TICKER.service" \
        || c_y "  could not start fulcrum-$TICKER (check: journalctl -u fulcrum-$TICKER -e)"
fi

# ---- done ---------------------------------------------------------------------------------------
echo; c_g "== Installed. =="
cat <<DONE

Before starting, make sure the $COIN_NAME daemon:
  • has JSON-RPC enabled and matches the rpcuser/rpcpassword/port above,
  • has txindex=1 and is fully synced (Fulcrum needs to fetch arbitrary txs),
  • is not a pruning node.

Manage the service:
  systemctl start   fulcrum-$TICKER
  systemctl status  fulcrum-$TICKER
  journalctl -u fulcrum-$TICKER -f        # watch sync / logs

If exposing ports publicly, open them in your firewall, e.g.:
  ufw allow ${PORT_TCP:-<tcp>}/tcp    # (repeat for ssl/ws/wss ports you enabled)

Notes:
  • Config: $CONF_FILE   Service: $SVC_FILE   DB: $DB_DIR
  • Re-run this script for each additional coin (files are namespaced by ticker).
  • FulcrumAdmin (optional, needs python3) is the Python script 'FulcrumAdmin' in the source repo
    — not part of the release. Grab it from $REPO if you enabled the admin port.
DONE
if [ -n "$CERT_SRC" ]; then
    printf '  • TLS certs: %s (owned by %s; fulcrum reads its own copies, not /etc/letsencrypt).\n' "$SSL_DIR" "$RUN_USER"
    if [ "$GEN_HOOK" = true ]; then
        printf '  • Cert renewal: wire the hook so fresh certs are copied in + service refreshed after renew:\n'
        printf '        certbot renew --deploy-hook %s/%s-renew-certs.sh\n' "$CONF_DIR" "$TICKER"
    fi
fi
