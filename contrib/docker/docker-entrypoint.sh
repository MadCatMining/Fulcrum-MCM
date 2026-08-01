#!/bin/sh
#
# Fulcrum Docker entrypoint.
#
# Responsibilities:
#   1. First-run setup wizard (interactive if a TTY is attached, otherwise env-driven).
#      Writes /data/fulcrum.conf and persists across container restarts.
#   2. SSL cert provisioning (self-signed | letsencrypt | user-supplied | none).
#   3. Daemon-reachability probe before launching Fulcrum (fail fast with an actionable error).
#   4. Self-managed renewal loop: opportunistic refresh on startup, then every ~30 days while
#      the container runs. Fulcrum's QFileSystemWatcher picks up new cert files automatically;
#      no restart needed.
#   5. SIGTERM forwarding so `docker stop` returns promptly even mid-sleep.
#
# All steps are POSIX sh (no bashisms). Keep this script idempotent.

set -eu

: "${DATA_DIR:=/data}"
: "${SSL_CERTFILE:=${DATA_DIR}/fulcrum.crt}"
: "${SSL_KEYFILE:=${DATA_DIR}/fulcrum.key}"

CONFIG_FILE="${DATA_DIR}/fulcrum.conf"
LE_LIVE_DIR="/etc/letsencrypt/live"
USER_CERTS_DIR="/certs"

# ----------------------------------------------------------------------------
# logging
# ----------------------------------------------------------------------------
log()   { printf '[entrypoint] %s\n' "$*" >&2; }
warn()  { printf '[entrypoint] WARNING: %s\n' "$*" >&2; }
fatal() { printf '[entrypoint] FATAL: %s\n' "$*" >&2; exit 1; }

# ----------------------------------------------------------------------------
# small helpers
# ----------------------------------------------------------------------------

# True if stdin is a TTY (so prompting makes sense).
is_tty() { [ -t 0 ]; }

# Prompt the user for a value with a default. Echoes the chosen value. Non-TTY -> echo default.
# Usage: ask VAR_NAME "Prompt text" "default"
ask() {
    _varname=$1; _prompt=$2; _default=${3:-}
    eval "_current=\${$_varname:-}"
    if [ -n "$_current" ]; then
        # explicit env var wins; never prompt.
        return 0
    fi
    if ! is_tty; then
        eval "$_varname=\"\$_default\""
        return 0
    fi
    if [ -n "$_default" ]; then
        printf '%s [%s]: ' "$_prompt" "$_default" >&2
    else
        printf '%s: ' "$_prompt" >&2
    fi
    IFS= read -r _ans || _ans=""
    [ -z "$_ans" ] && _ans=$_default
    eval "$_varname=\"\$_ans\""
}

# Like ask, but echoes nothing while reading (for passwords).
ask_secret() {
    _varname=$1; _prompt=$2
    eval "_current=\${$_varname:-}"
    if [ -n "$_current" ]; then return 0; fi
    if ! is_tty; then
        eval "$_varname=\"\""
        return 0
    fi
    printf '%s: ' "$_prompt" >&2
    stty -echo 2>/dev/null || true
    IFS= read -r _ans || _ans=""
    stty echo 2>/dev/null || true
    printf '\n' >&2
    eval "$_varname=\"\$_ans\""
}

# ----------------------------------------------------------------------------
# setup wizard — only runs on first boot (or when FORCE_SETUP=1)
# ----------------------------------------------------------------------------
run_setup() {
    log "Running first-time setup. Generated config will be persisted to $CONFIG_FILE."

    # --- coin selection (used only for sensible defaults below; Fulcrum auto-detects the actual
    #     coin from the daemon's getnetworkinfo subversion at runtime) ---
    ask COIN "Coin (BCH, BTC, LTC, DIMI, DASH, ARTBYTE, PEERCOIN, NOVACOIN, PIVX, EGULDEN, FLORIN, POTCOIN, EMARK, PRIMECOIN, ...)" "BCH"
    # shellcheck disable=SC1091
    . /coin-defaults.sh
    if ! set_coin_defaults "$COIN"; then
        warn "No built-in defaults for coin '$COIN'. You'll need to supply DAEMON_RPC_PORT explicitly."
    fi

    # --- daemon connection ---
    ask DAEMON_HOST       "Host/IP where the coin daemon is running" "host.docker.internal"
    ask DAEMON_RPC_PORT   "Daemon RPC port"                          "${DEF_DAEMON_RPC_PORT:-}"
    [ -n "${DAEMON_RPC_PORT:-}" ] || fatal "DAEMON_RPC_PORT is required."
    ask        DAEMON_RPC_USER     "Daemon RPC user"   ""
    ask_secret DAEMON_RPC_PASSWORD "Daemon RPC password"
    [ -n "${DAEMON_RPC_USER:-}" ]     || fatal "DAEMON_RPC_USER is required."
    [ -n "${DAEMON_RPC_PASSWORD:-}" ] || fatal "DAEMON_RPC_PASSWORD is required."

    # --- Electrum-protocol listeners ---
    ask ELECTRUM_TCP_PORT "TCP listen port (Electrum protocol)" "50001"
    ask ELECTRUM_SSL_PORT "SSL listen port (Electrum protocol; 0 to disable)" "50002"
    ask ELECTRUM_WS_PORT  "WebSocket (ws) listen port (0 to disable)" "50003"
    ask ELECTRUM_WSS_PORT "WebSocket Secure (wss) listen port; needs a cert (0 to disable)" "50004"

    # --- reverse proxy / PROXY protocol ---
    # If Fulcrum runs behind an L4 reverse proxy (nginx / Nginx Proxy Manager, HAProxy) that prepends a PROXY
    # protocol header, enable this so the real client IP is recovered (instead of every client appearing as the proxy).
    ask ENABLE_PROXY_PROTOCOL "Accept PROXY protocol headers from a trusted reverse proxy on tcp/ssl/ws/wss (true/false)" "false"
    # For an L7 (HTTP) proxy that terminates TLS and forwards ws:// (e.g. NPM as an HTTP Proxy Host with Websockets
    # Support), the real client IP arrives in the X-Forwarded-For header instead. Enable this for that setup.
    ask ENABLE_WS_XFF "Trust X-Forwarded-For on ws/wss from a trusted reverse proxy (true/false)" "false"
    if [ "$ENABLE_PROXY_PROTOCOL" = "true" ] || [ "$ENABLE_WS_XFF" = "true" ]; then
        # Comma-separated trusted proxy subnets. Leave blank to trust loopback + private ranges (RFC1918/ULA/link-local).
        ask PROXY_PROTOCOL_FROM "Trusted proxy subnet(s), comma-separated (blank = loopback + private ranges)" ""
    fi

    # --- SSL cert mode ---
    ask SSL_MODE "SSL cert mode: self-signed | letsencrypt | user-supplied | none" "self-signed"
    case "$SSL_MODE" in
        self-signed|letsencrypt|user-supplied|none) ;;
        *) fatal "Invalid SSL_MODE '$SSL_MODE'." ;;
    esac
    if [ "$SSL_MODE" = "letsencrypt" ]; then
        ask LE_DOMAIN "Let's Encrypt domain (must already be bootstrapped in /etc/letsencrypt)" ""
        [ -n "${LE_DOMAIN:-}" ] || fatal "LE_DOMAIN is required when SSL_MODE=letsencrypt."
    fi
    if [ "$SSL_MODE" = "none" ]; then
        ELECTRUM_SSL_PORT=0
    fi

    # --- peering ---
    ask PEERING "Enable peer discovery (true/false)" "${DEF_PEERING:-false}"

    # --- write fulcrum.conf ---
    mkdir -p "$DATA_DIR"
    {
        printf '# Generated by docker-entrypoint.sh on %s.\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
        printf '# Delete this file (or set FORCE_SETUP=1) to re-run the setup wizard.\n\n'
        printf 'datadir       = %s\n' "$DATA_DIR"
        printf 'bitcoind      = %s:%s\n' "$DAEMON_HOST" "$DAEMON_RPC_PORT"
        printf 'rpcuser       = %s\n' "$DAEMON_RPC_USER"
        printf 'rpcpassword   = %s\n' "$DAEMON_RPC_PASSWORD"
        printf 'tcp           = 0.0.0.0:%s\n' "$ELECTRUM_TCP_PORT"
        if [ "${ELECTRUM_WS_PORT:-0}" != "0" ]; then
            printf 'ws            = 0.0.0.0:%s\n' "$ELECTRUM_WS_PORT"
        fi
        if [ "$ELECTRUM_SSL_PORT" != "0" ] && [ "$SSL_MODE" != "none" ]; then
            printf 'ssl           = 0.0.0.0:%s\n' "$ELECTRUM_SSL_PORT"
            printf 'cert          = %s\n' "$SSL_CERTFILE"
            printf 'key           = %s\n' "$SSL_KEYFILE"
        fi
        # wss (Fulcrum-terminated TLS WebSocket) reuses the same cert/key as ssl; only emit it if we have a cert.
        if [ "${ELECTRUM_WSS_PORT:-0}" != "0" ] && [ "$SSL_MODE" != "none" ]; then
            printf 'wss           = 0.0.0.0:%s\n' "$ELECTRUM_WSS_PORT"
        fi
        if [ "${ENABLE_PROXY_PROTOCOL:-false}" = "true" ]; then
            printf 'proxy_protocol = true\n'
        fi
        if [ "${ENABLE_WS_XFF:-false}" = "true" ]; then
            printf 'ws_x_forwarded_for = true\n'
        fi
        if { [ "${ENABLE_PROXY_PROTOCOL:-false}" = "true" ] || [ "${ENABLE_WS_XFF:-false}" = "true" ]; } \
                && [ -n "${PROXY_PROTOCOL_FROM:-}" ]; then
            printf 'proxy_protocol_from = %s\n' "$PROXY_PROTOCOL_FROM"
        fi
        printf 'peering       = %s\n' "$PEERING"
    } > "$CONFIG_FILE"
    chmod 600 "$CONFIG_FILE"

    # Persist the chosen SSL_MODE and LE_DOMAIN so subsequent starts know what to renew.
    {
        printf 'SSL_MODE=%s\n' "$SSL_MODE"
        [ -n "${LE_DOMAIN:-}" ] && printf 'LE_DOMAIN=%s\n' "$LE_DOMAIN"
    } > "${DATA_DIR}/.entrypoint-state"
    chmod 600 "${DATA_DIR}/.entrypoint-state"

    log "Wrote $CONFIG_FILE."
}

# ----------------------------------------------------------------------------
# cert provisioning + renewal
# ----------------------------------------------------------------------------

cert_setup_self_signed() {
    # Generate if missing. The renewal loop will rotate when within 30d of expiry.
    if [ ! -e "$SSL_CERTFILE" ] || [ ! -e "$SSL_KEYFILE" ]; then
        log "Generating self-signed cert at $SSL_CERTFILE."
        openssl req -newkey rsa:2048 -sha256 -nodes -x509 -days 365 \
                    -subj "/O=Fulcrum" \
                    -keyout "$SSL_KEYFILE" -out "$SSL_CERTFILE"
    fi
}

cert_setup_letsencrypt() {
    [ -n "${LE_DOMAIN:-}" ] || fatal "LE_DOMAIN is required for SSL_MODE=letsencrypt."
    if [ ! -d "${LE_LIVE_DIR}/${LE_DOMAIN}" ]; then
        fatal "Let's Encrypt live directory ${LE_LIVE_DIR}/${LE_DOMAIN} not found. Bootstrap the cert first
       (see contrib/docker/README.md → 'Let's Encrypt bootstrap'). Also confirm you bind-mounted
       /etc/letsencrypt into the container."
    fi
    # Symlink the LE files to the canonical paths Fulcrum reads. Symlink targets are stable across
    # certbot renewals, so QFileSystemWatcher sees the inode/content change and triggers a reload.
    ln -sf "${LE_LIVE_DIR}/${LE_DOMAIN}/fullchain.pem" "$SSL_CERTFILE"
    ln -sf "${LE_LIVE_DIR}/${LE_DOMAIN}/privkey.pem"   "$SSL_KEYFILE"
    log "Let's Encrypt cert for $LE_DOMAIN linked to $SSL_CERTFILE."
}

cert_setup_user_supplied() {
    if [ ! -e "${USER_CERTS_DIR}/fullchain.pem" ] || [ ! -e "${USER_CERTS_DIR}/privkey.pem" ]; then
        fatal "SSL_MODE=user-supplied requires ${USER_CERTS_DIR}/fullchain.pem and
       ${USER_CERTS_DIR}/privkey.pem to be bind-mounted into the container."
    fi
    ln -sf "${USER_CERTS_DIR}/fullchain.pem" "$SSL_CERTFILE"
    ln -sf "${USER_CERTS_DIR}/privkey.pem"   "$SSL_KEYFILE"
    log "User-supplied cert linked to $SSL_CERTFILE."
}

cert_dispatch() {
    case "$SSL_MODE" in
        self-signed)   cert_setup_self_signed ;;
        letsencrypt)   cert_setup_letsencrypt ;;
        user-supplied) cert_setup_user_supplied ;;
        none)          log "SSL_MODE=none; only the TCP listener will be started." ;;
        *)             fatal "Unknown SSL_MODE '$SSL_MODE'." ;;
    esac
}

# Returns 0 if the cert at $1 expires within $2 seconds, non-zero otherwise.
cert_expiring_within() {
    openssl x509 -in "$1" -noout -checkend "$2" >/dev/null 2>&1 && return 1 || return 0
}

try_renew_once() {
    case "$SSL_MODE" in
        self-signed)
            if [ -e "$SSL_CERTFILE" ] && cert_expiring_within "$SSL_CERTFILE" $((30*24*3600)); then
                log "Self-signed cert expiring within 30 days; regenerating."
                openssl req -newkey rsa:2048 -sha256 -nodes -x509 -days 365 \
                            -subj "/O=Fulcrum" \
                            -keyout "$SSL_KEYFILE" -out "$SSL_CERTFILE"
            fi
            ;;
        letsencrypt)
            # Renewal config (incl. authenticator + DNS plugin) is stored under /etc/letsencrypt/renewal/
            # by certbot at first issuance. `certbot renew` re-uses that automatically.
            if [ ! -d /etc/letsencrypt/renewal ]; then
                # Read-only / pre-issued mount with no renewal state. Nothing for us to do.
                log "letsencrypt: no renewal/ directory present; assuming external renewal management."
                return 0
            fi
            log "letsencrypt: running 'certbot renew --quiet --keep-until-expiring'."
            certbot renew --quiet --keep-until-expiring \
                --deploy-hook 'printf "[entrypoint] deploy-hook: cert reloaded\n" >&2' \
                || warn "certbot renew exited non-zero (cert may still be valid; will retry next cycle)."
            ;;
        user-supplied|none)
            : # nothing to do
            ;;
    esac
}

# ----------------------------------------------------------------------------
# daemon reachability probe
# ----------------------------------------------------------------------------
probe_daemon() {
    _host=$1; _port=$2
    if ! getent hosts "$_host" >/dev/null 2>&1; then
        warn "Cannot resolve '$_host' inside the container."
        case "$_host" in
            host.docker.internal)
                warn "On Linux hosts, add  --add-host=host.docker.internal:host-gateway  to your docker run,"
                warn "or use  --network=host  (Linux only) and set DAEMON_HOST=127.0.0.1."
                ;;
        esac
        fatal "Daemon host unreachable."
    fi
    if ! nc -z -w 3 "$_host" "$_port" 2>/dev/null; then
        warn "TCP connection to ${_host}:${_port} failed."
        warn "Check that the daemon is running and its rpcbind/rpcallowip permits this container."
        fatal "Daemon RPC port unreachable."
    fi
    log "Daemon reachable at ${_host}:${_port}."
}

# ----------------------------------------------------------------------------
# renewal loop (background)
# ----------------------------------------------------------------------------
renewal_loop() {
    # Initial pass on startup. Errors are logged, not fatal.
    try_renew_once || true
    while :; do
        # ~30 days with up to ±30 min jitter so a fleet of containers don't all wake together.
        # We use awk for randomness because POSIX sh has no $RANDOM and we don't want to depend on bash.
        _jitter=$(awk 'BEGIN { srand(); printf "%d", (rand() * 3600) - 1800 }')
        _sleep_secs=$((30*24*3600 + _jitter))
        sleep "$_sleep_secs" &
        wait $! 2>/dev/null || break   # break out cleanly when the parent gets SIGTERM
        try_renew_once || true
    done
}

# ----------------------------------------------------------------------------
# main
# ----------------------------------------------------------------------------
main() {
    mkdir -p "$DATA_DIR"

    # If invoked with anything other than the "Fulcrum" command, run it verbatim. This keeps
    # `docker run --rm -it fulcrum:tag /bin/sh` etc. working.
    if [ "${1:-}" != "Fulcrum" ]; then
        exec "$@"
    fi

    if [ ! -e "$CONFIG_FILE" ] || [ "${FORCE_SETUP:-0}" = "1" ]; then
        run_setup
    else
        log "Re-using existing $CONFIG_FILE (set FORCE_SETUP=1 to re-run the wizard)."
        # Recover persisted entrypoint state (SSL_MODE etc.) if present.
        if [ -e "${DATA_DIR}/.entrypoint-state" ]; then
            # shellcheck disable=SC1091
            . "${DATA_DIR}/.entrypoint-state"
        fi
        : "${SSL_MODE:=self-signed}"
    fi

    cert_dispatch

    # Parse the daemon endpoint back out of the generated config for the reachability probe.
    _bitcoind_line=$(awk -F= '/^[[:space:]]*bitcoind[[:space:]]*=/ { gsub(/[[:space:]]/, "", $2); print $2; exit }' "$CONFIG_FILE" || true)
    if [ -n "$_bitcoind_line" ]; then
        _host=${_bitcoind_line%:*}
        _port=${_bitcoind_line##*:}
        probe_daemon "$_host" "$_port"
    fi

    # Spawn the renewal loop in the background for modes that can renew.
    case "$SSL_MODE" in
        self-signed|letsencrypt)
            renewal_loop &
            RENEW_PID=$!
            ;;
        *)
            RENEW_PID=""
            ;;
    esac

    # Forward SIGTERM/SIGINT to Fulcrum and the renewal loop so `docker stop` exits promptly.
    trap 'log "received signal, stopping..."; [ -n "${FULCRUM_PID:-}" ] && kill -TERM "$FULCRUM_PID" 2>/dev/null; [ -n "${RENEW_PID:-}" ] && kill -TERM "$RENEW_PID" 2>/dev/null; exit 0' TERM INT

    log "Starting Fulcrum."
    Fulcrum "$CONFIG_FILE" &
    FULCRUM_PID=$!
    # Wait specifically on Fulcrum (not the renewal loop) so its exit code propagates to Docker.
    wait "$FULCRUM_PID"
    _rc=$?
    [ -n "$RENEW_PID" ] && kill -TERM "$RENEW_PID" 2>/dev/null || true
    exit "$_rc"
}

main "$@"
