#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
ETHGIG_TAP="$REPO_ROOT/build/tribe_linux/net/ethgig_tap"
SOCKET=/tmp/tribe-ethgig.sock
TAP=tap-tribe
RUN_UID=${SUDO_UID:-$(id -u)}
OUT=/tmp/ethgig-tap.${RUN_UID}.out
TRACE=/tmp/ethgig-tap.${RUN_UID}.log

killall ethgig_tap 2>/dev/null || true
rm -f "$SOCKET"

if [ ! -x "$ETHGIG_TAP" ]; then
    echo "missing ethgig_tap binary: $ETHGIG_TAP" >&2
    echo "build it with: cmake --build $REPO_ROOT/build --target ethgig_tap" >&2
    exit 1
fi

if [ "$(id -u)" != 0 ]; then
    echo "run this helper with sudo because TAP setup needs /dev/net/tun access:" >&2
    echo "  sudo $0" >&2
    exit 1
fi

# Keep this helper process alive. ethgig_tap unlinks the socket on clean exit,
# and sudo/session cleanup can kill background children after the script exits.
ETHGIG_TAP_TRACE=1 ETHGIG_TAP_TRACE_FILE="$TRACE" \
    stdbuf -oL -eL "$ETHGIG_TAP" --tap "$TAP" --socket "$SOCKET" \
    </dev/null >"$OUT" 2>&1 &
TAP_PID=$!
trap 'kill "$TAP_PID" 2>/dev/null || true; wait "$TAP_PID" 2>/dev/null || true' INT TERM EXIT

for _ in $(seq 1 50); do
    if [ -S "$SOCKET" ]; then
        break
    fi
    if ! kill -0 "$TAP_PID" 2>/dev/null; then
        echo "ethgig_tap exited before creating $SOCKET" >&2
        cat "$OUT" >&2 || true
        exit 1
    fi
    sleep 0.1
done

if [ ! -S "$SOCKET" ]; then
    echo "timeout waiting for $SOCKET" >&2
    cat "$OUT" >&2 || true
    exit 1
fi

if ! kill -0 "$TAP_PID" 2>/dev/null; then
    echo "ethgig_tap exited after creating $SOCKET" >&2
    cat "$OUT" >&2 || true
    exit 1
fi

ip addr add 192.168.76.1/24 dev "$TAP" 2>/dev/null || true
ip link set "$TAP" up
chmod a+rw "$SOCKET"
echo "ethgig_tap is running; socket: $SOCKET"
echo "leave this terminal open; press Ctrl+C here to stop the TAP bridge"

wait "$TAP_PID"
trap - INT TERM EXIT

#in cpu:
#  ip link set eth0 up
#  ip addr add 192.168.76.2/24 dev eth0
#  ping 192.168.76.1
