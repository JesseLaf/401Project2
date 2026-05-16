#!/bin/bash
# Concurrency tests for Project 3
# Usage: ./test_concurrent.sh

PORT=8004
BINARY="./wserver"
CLIENT="./wclient"
PASS=0
FAIL=0

GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

pass() { echo -e "${GREEN}PASS${NC} $1"; PASS=$((PASS+1)); }
fail() { echo -e "${RED}FAIL${NC} $1 -- $2"; FAIL=$((FAIL+1)); }

start_server() {
    $BINARY -d . -p $PORT -t "$1" -b "$2" -s "$3" &
    SERVER_PID=$!
    sleep 1
}

stop_server() {
    kill $SERVER_PID 2>/dev/null
    wait $SERVER_PID 2>/dev/null
}

# Wait only for specific client PIDs, not all background jobs
wait_for_clients() {
    for pid in "$@"; do
        wait "$pid"
    done
}

query() {
    $CLIENT localhost $PORT "$1" 2>/dev/null | grep -v "^Header:"
}

echo "=== Concurrency Tests ==="

# ── Test 1: server starts and responds with new flags ────────────────
start_server 2 4 FIFO
R=$(query "/spin.cgi?1")
echo "$R" | grep -q "spun" && pass "Server starts with -t -b -s flags" \
                             || fail "Server starts with -t -b -s flags" "$R"
stop_server

# ── Test 2: default args still work ──────────────────────────────────
start_server 1 1 FIFO
R=$(query "/spin.cgi?1")
echo "$R" | grep -q "spun" && pass "Default args work" \
                             || fail "Default args work" "$R"
stop_server

# ── Test 3: FIFO — 4 concurrent requests complete correctly ──────────
start_server 4 8 FIFO
TMPDIR_TEST=$(mktemp -d)
START=$(date +%s)

PIDS=()
for i in 1 2 3 4; do
    $CLIENT localhost $PORT /spin.cgi?2 > "$TMPDIR_TEST/out$i.txt" 2>/dev/null &
    PIDS+=($!)
done
wait_for_clients "${PIDS[@]}"

END=$(date +%s)
ELAPSED=$((END - START))

ALL_OK=1
for i in 1 2 3 4; do
    grep -q "spun" "$TMPDIR_TEST/out$i.txt" || ALL_OK=0
done
[ "$ALL_OK" -eq 1 ] && pass "FIFO: all 4 concurrent requests completed" \
                      || fail "FIFO: all 4 concurrent requests completed" "one or more missing"

[ "$ELAPSED" -le 5 ] && pass "FIFO: 4 requests ran in parallel (${ELAPSED}s)" \
                       || fail "FIFO: 4 requests ran in parallel (${ELAPSED}s, expected <=5)" ""
stop_server
rm -rf "$TMPDIR_TEST"

# ── Test 4: SFF — requests complete correctly ─────────────────────────
start_server 4 8 SFF
TMPDIR_TEST=$(mktemp -d)
START=$(date +%s)

PIDS=()
for i in 1 2 3 4; do
    $CLIENT localhost $PORT /spin.cgi?2 > "$TMPDIR_TEST/out$i.txt" 2>/dev/null &
    PIDS+=($!)
done
wait_for_clients "${PIDS[@]}"

END=$(date +%s)
ELAPSED=$((END - START))

ALL_OK=1
for i in 1 2 3 4; do
    grep -q "spun" "$TMPDIR_TEST/out$i.txt" || ALL_OK=0
done
[ "$ALL_OK" -eq 1 ] && pass "SFF: all 4 concurrent requests completed" \
                      || fail "SFF: all 4 concurrent requests completed" "one or more missing"

[ "$ELAPSED" -le 5 ] && pass "SFF: 4 requests ran in parallel (${ELAPSED}s)" \
                       || fail "SFF: 4 requests ran in parallel (${ELAPSED}s, expected <=5)" ""
stop_server
rm -rf "$TMPDIR_TEST"

# ── Test 5: SFF ordering — small file served before large file ────────
echo "x" > small.html
python3 -c "print('x' * 100000)" > large.html

start_server 1 4 SFF
TMPDIR_TEST=$(mktemp -d)

PIDS=()
$CLIENT localhost $PORT /large.html > "$TMPDIR_TEST/large.txt" 2>/dev/null &
PIDS+=($!)
sleep 0.1
$CLIENT localhost $PORT /small.html > "$TMPDIR_TEST/small.txt" 2>/dev/null &
PIDS+=($!)
wait_for_clients "${PIDS[@]}"

grep -q "200 OK" "$TMPDIR_TEST/small.txt" && pass "SFF: small file request was served" \
                                            || fail "SFF: small file request was served" ""
grep -q "200 OK" "$TMPDIR_TEST/large.txt" && pass "SFF: large file request was served" \
                                            || fail "SFF: large file request was served" ""
stop_server
rm -f small.html large.html
rm -rf "$TMPDIR_TEST"

# ── Test 6: buffer fills and drains correctly ─────────────────────────
start_server 2 4 FIFO
TMPDIR_TEST=$(mktemp -d)

PIDS=()
for i in 1 2 3 4 5 6; do
    $CLIENT localhost $PORT /spin.cgi?1 > "$TMPDIR_TEST/out$i.txt" 2>/dev/null &
    PIDS+=($!)
done
wait_for_clients "${PIDS[@]}"

ALL_OK=1
for i in 1 2 3 4 5 6; do
    grep -q "spun" "$TMPDIR_TEST/out$i.txt" || ALL_OK=0
done
[ "$ALL_OK" -eq 1 ] && pass "Buffer fills and drains: all 6 requests completed" \
                      || fail "Buffer fills and drains: all 6 requests completed" "one or more missing"
stop_server
rm -rf "$TMPDIR_TEST"

# ── Test 7: single thread handles sequential requests ─────────────────
start_server 1 2 FIFO
R1=$(query "/spin.cgi?1")
R2=$(query "/spin.cgi?1")
echo "$R1" | grep -q "spun" && echo "$R2" | grep -q "spun" \
    && pass "Single thread handles sequential requests" \
    || fail "Single thread handles sequential requests" ""
stop_server

# ── Summary ───────────────────────────────────────────────────────────
echo "========================="
echo "PASSED: $PASS  FAILED: $FAIL"