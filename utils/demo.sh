#!/usr/bin/env bash
# utils/demo.sh — Live manual test: send a transaction and watch it get mined.
#
# Starts two background loops:
#   miner   — polls mempool every second; mines when transactions are pending
#   watcher — prints chain status whenever tip or mempool count changes
#
# Then prompts you to submit transactions:
#   ./build/zuno-debug send --from alice --to bob --amount 10
#
# Press Ctrl+C to stop and clean up.

set -uo pipefail

ZUNO="./build/zuno-debug"
KEY_GEN="./build/utils/key_gen"

# ── pre-flight ─────────────────────────────────────────────────────────────

if [[ ! -x "$ZUNO" ]]; then
    echo "error: $ZUNO not found — run 'make all' first" >&2
    exit 1
fi
if [[ ! -x "$KEY_GEN" ]]; then
    echo "error: $KEY_GEN not found — run 'make all' first" >&2
    exit 1
fi

# ── cleanup ────────────────────────────────────────────────────────────────

PIDS=()
_DEMO_CLEANED=0

cleanup() {
    [[ $_DEMO_CLEANED -eq 1 ]] && return
    _DEMO_CLEANED=1
    echo ""
    echo "[demo] stopping..."
    for pid in "${PIDS[@]:-}"; do
        kill "$pid" 2>/dev/null || true
    done
    wait 2>/dev/null || true
    rm -rf .chain
    echo "[demo] cleaned up."
}
trap cleanup INT TERM EXIT

# ── helpers ────────────────────────────────────────────────────────────────

ts() { date "+%H:%M:%S"; }

# ── setup ──────────────────────────────────────────────────────────────────

rm -rf .chain

echo "[$(ts)] init     initialising chain..."
$ZUNO init 2>/dev/null

echo "[$(ts)] init     generating keypair for 'alice'..."
$KEY_GEN alice 2>/dev/null

echo "[$(ts)] init     ready."
echo ""

# ── miner loop ─────────────────────────────────────────────────────────────
#
# Runs zuno mine every second. Prints only when a block is successfully mined
# or when all pending transactions fail verification.

miner_loop() {
    while true; do
        out=$($ZUNO mine 2>/dev/null) || true
        if [[ -n "$out" && "$out" != *"nothing to mine"* ]]; then
            echo "[$(ts)] miner    $out"
        fi
        sleep 1
    done
}

# ── watcher loop ───────────────────────────────────────────────────────────
#
# Polls chain status every 2 seconds. Prints only when something changes
# (new block mined or mempool count changes) so output isn't flooded.

watcher_loop() {
    local last_tip=""
    local last_pool=""
    while true; do
        status=$($ZUNO status 2>/dev/null) || true
        tip=$(echo "$status"  | grep "Chain tip"  | awk '{print $3, $4}')
        pool=$(echo "$status" | grep "Mempool"    | cut -d: -f2- | xargs)

        if [[ "$tip" != "$last_tip" || "$pool" != "$last_pool" ]]; then
            echo "[$(ts)] chain    tip=$tip  mempool=$pool"
            last_tip="$tip"
            last_pool="$pool"
        fi
        sleep 2
    done
}

# ── start background loops ─────────────────────────────────────────────────

miner_loop &
PIDS+=($!)

watcher_loop &
PIDS+=($!)

# ── instructions ───────────────────────────────────────────────────────────

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  Miner and watcher are running."
echo ""
echo "  In another terminal, send transactions:"
echo "    $ZUNO send --from alice --to bob   --amount 10"
echo "    $ZUNO send --from alice --to carol --amount 25"
echo "    $ZUNO send --from alice --to dave  --amount 5"
echo ""
echo "  Or verify a mined block:"
echo "    $ZUNO log"
echo "    $ZUNO verify <hash>"
echo ""
echo "  Press Ctrl+C to stop and clean up."
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

# ── wait ───────────────────────────────────────────────────────────────────

wait
