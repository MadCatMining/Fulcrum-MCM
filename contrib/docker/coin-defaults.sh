#!/bin/sh
#
# Fulcrum - per-coin default RPC ports + peering hints for the Docker entrypoint.
#
# Sourced by docker-entrypoint.sh. Keep this in sync with src/CoinConfig.cpp:
#   - DEF_DAEMON_RPC_PORT is the daemon's well-known RPC port (the value the user
#     normally puts in `rpcport=` on the daemon side).
#   - DEF_PEERING mirrors CoinConfig.peerResourcePath: "true" if Fulcrum ships peer
#     seeds for this coin (BCH/BTC/LTC), "false" if not (DIMI and most altcoins).
#
# To add a coin: append one `case` clause whose label matches the `name` field of the
# corresponding CoinConfig entry. If you don't, the entrypoint will warn and prompt
# the user for DAEMON_RPC_PORT explicitly — which is the right fallback.

set_coin_defaults() {
    case "$(printf '%s' "$1" | tr '[:lower:]' '[:upper:]')" in
        BCH)      DEF_DAEMON_RPC_PORT=8332;  DEF_PEERING=true  ;;
        BTC)      DEF_DAEMON_RPC_PORT=8332;  DEF_PEERING=true  ;;
        LTC)      DEF_DAEMON_RPC_PORT=9332;  DEF_PEERING=true  ;;
        DIMI)     DEF_DAEMON_RPC_PORT=21102; DEF_PEERING=false ;;
        DGC)      DEF_DAEMON_RPC_PORT=11111; DEF_PEERING=false ;;  # Digitalcoin (SHA256d block-id)
        IL8P)     DEF_DAEMON_RPC_PORT=9459;  DEF_PEERING=false ;;  # Infiniloop (scrypt genesis, PoS)
        # NB: DGC/IL8P daemons let you pick any rpcport=; the values above are just this project's
        # conventional defaults. Override with DAEMON_RPC_PORT if your daemon uses a different one.
        # Future coins (placeholders — verify the actual RPC port against each project's docs
        # before relying on these):
        DASH)     DEF_DAEMON_RPC_PORT=9998;  DEF_PEERING=false ;;
        ARTBYTE|ABY) DEF_DAEMON_RPC_PORT=9777;  DEF_PEERING=false ;;
        PEERCOIN|PPC) DEF_DAEMON_RPC_PORT=9902;  DEF_PEERING=false ;;
        NOVACOIN|NVC) DEF_DAEMON_RPC_PORT=8344;  DEF_PEERING=false ;;
        PIVX)     DEF_DAEMON_RPC_PORT=51473; DEF_PEERING=false ;;
        EGULDEN|EFL) DEF_DAEMON_RPC_PORT=21015; DEF_PEERING=false ;;
        FLORIN|FLO) DEF_DAEMON_RPC_PORT=7313;  DEF_PEERING=false ;;
        POTCOIN|POT) DEF_DAEMON_RPC_PORT=4777;  DEF_PEERING=false ;;
        EMARK|DEM) DEF_DAEMON_RPC_PORT=6662;  DEF_PEERING=false ;;
        PRIMECOIN|XPM) DEF_DAEMON_RPC_PORT=9912;  DEF_PEERING=false ;;
        *)
            DEF_DAEMON_RPC_PORT=""
            DEF_PEERING=false
            return 1
            ;;
    esac
    return 0
}
