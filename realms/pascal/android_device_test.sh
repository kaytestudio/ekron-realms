#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# Realms Engine - run the bridge conformance suite on an Android device or
# emulator, against both the C++ reference core and the Free Pascal core.
#
#   realms/pascal/android_device_test.sh <realms_bridge_test> <librealms_core.so> \
#       [<realms_quake2_module_test> <librealms_game.so>]
#
# With the last two it also runs the Quake 2 module test on both cores,
# REALMS_MODULE_RUNS times each (default 1).
#
# Build the test with the NDK for the device ABI, e.g. (x86_64 emulator):
#   $NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++ \
#     --target=x86_64-linux-android30 -std=c++17 -O2 -static-libstdc++ \
#     -Irealms/include tests/realms/realms_bridge_test.cpp realms/src/*.cpp \
#     -o realms_bridge_test
# -----------------------------------------------------------------------------
set -euo pipefail

TEST_BIN="${1:?usage: $0 <realms_bridge_test> <librealms_core.so>}"
CORE_LIB="${2:?usage: $0 <realms_bridge_test> <librealms_core.so>}"
ADB="${ADB:-adb}"
DIR=/data/local/tmp/realms

"$ADB" wait-for-device
# root is needed to read tombstones; only works on userdebug images
"$ADB" root >/dev/null 2>&1 && "$ADB" wait-for-device || true
echo "device: $("$ADB" shell getprop ro.product.cpu.abi | tr -d '\r') API $("$ADB" shell getprop ro.build.version.sdk | tr -d '\r')"
"$ADB" shell "rm -rf $DIR && mkdir -p $DIR/tmp"
"$ADB" push "$TEST_BIN" "$DIR/realms_bridge_test" >/dev/null
"$ADB" push "$CORE_LIB" "$DIR/librealms_core.so" >/dev/null
"$ADB" shell "chmod 755 $DIR/realms_bridge_test"
MODULE_TEST="${3:-}"
GAME_LIB="${4:-}"
if [ -n "$MODULE_TEST" ]; then
    "$ADB" push "$MODULE_TEST" "$DIR/realms_quake2_module_test" >/dev/null
    "$ADB" push "$GAME_LIB" "$DIR/librealms_game.so" >/dev/null
    "$ADB" shell "chmod 755 $DIR/realms_quake2_module_test"
fi

status=0
# escaping for ::error title=...::message
gh_prop() { local v="${1//%/%25}"; v="${v//,/%2C}"; v="${v//:/%3A}"; printf '%s' "$v"; }
gh_msg() { local v="${1//%/%25}"; v="${v//$'\n'/%0A}"; printf '%s' "$v"; }
# crash buffer, game frames run through addr2line, tombstone excerpt
crash_report() {
    local buf; buf="$("$ADB" logcat -d -b crash 2>/dev/null | tr -d '\r')"
    printf '%s\n' "$buf" | grep -E 'Fatal signal|Cause:|#[0-9]+ pc|Abort message' | head -n 16 || true
    if [ -n "$GAME_LIB" ] && command -v addr2line >/dev/null 2>&1; then
        local pc
        for pc in $(printf '%s\n' "$buf" | sed -n 's/.*#[0-9]* pc \([0-9a-f]*\) .*librealms_game\.so.*/\1/p' | sort -u | head -n 8); do
            echo "game pc 0x$pc: $(addr2line -f -i -e "$GAME_LIB" "0x$pc" 2>/dev/null | paste -sd ' ' -)"
            nm -n --defined-only "$GAME_LIB" 2>/dev/null | python3 -c '
import sys
pc, best = int(sys.argv[1], 16), None
for line in sys.stdin:
    f = line.split()
    if len(f) >= 3 and int(f[0], 16) <= pc: best = (f[2], pc - int(f[0], 16))
if best: print("  nearest symbol: %s+0x%x" % best)' "$pc" || true
        done
    fi
    local tomb; tomb="$("$ADB" shell 'ls -t /data/tombstones/ 2>/dev/null | grep -v pb | head -n 1' 2>/dev/null | tr -d '\r')"
    if [ -n "$tomb" ]; then
        "$ADB" shell "cat /data/tombstones/$tomb" 2>/dev/null | tr -d '\r' \
            | grep -E -A1 -B1 -- '--->' | head -n 8 || true
        "$ADB" shell "cat /data/tombstones/$tomb" 2>/dev/null | tr -d '\r' \
            | grep -E '^ +(rax|rdx|rsi|rbp|r12|r14|rip|rsp|cs) ' | head -n 8 || true
        "$ADB" shell "cat /data/tombstones/$tomb" 2>/dev/null | tr -d '\r' \
            | grep -E '^ +#[0-9]+ pc' | head -n 12 || true
    fi
}
run_suite() {
    local label="$1" expect="$2"; shift 2
    local log; log="$(mktemp)"
    # adb shell propagates the exit status; also check the summary line.
    if "$ADB" shell "cd $DIR && TMPDIR=$DIR/tmp REALMS_CORE= ./realms_bridge_test $*; echo exit=\$?" \
        | tr -d '\r' | tee "$log" >/dev/null; then :; fi
    local summary; summary="$(grep -E '^\[realms\] [0-9]+ checks' "$log" | tail -n 1 || true)"
    echo "[$label] ${summary:-no summary}"
    if ! grep -q '^exit=0$' "$log" || ! grep -qE "^\[realms\] [0-9]+ checks, 0 failures \($expect\)$" "$log"; then
        grep -E 'FAIL|error|Error' "$log" | head -n 20 || true
        echo "::error title=$(gh_prop "Realms bridge on Android ($label)")::$(gh_msg "$(grep -E 'FAIL|exit=' "$log" | head -n 6 | tr '\n' ' ' | cut -c1-900)")"
        status=1
    fi
    rm -f "$log"
}

run_suite "C++ reference core" native-cpp
run_suite "Free Pascal core" pascal-quake2 --pascal "$DIR/librealms_core.so"

RUNS="${REALMS_MODULE_RUNS:-1}"
run_module() {
    local label="$1" expect="$2"; shift 2
    local log; log="$(mktemp)"
    local passed=0 run
    for run in $(seq 1 "$RUNS"); do
        "$ADB" logcat -b crash -c >/dev/null 2>&1 || true
        if "$ADB" shell "cd $DIR && TMPDIR=$DIR/tmp REALMS_CORE= ./realms_quake2_module_test --game $DIR/librealms_game.so $*; echo exit=\$?" \
            | tr -d '\r' > "$log"; then :; fi
        if grep -q '^exit=0$' "$log" && grep -qE "^\[module\] [0-9]+ checks, 0 failures \($expect\)$" "$log"; then
            passed=$((passed + 1))
            continue
        fi
        local code; code="$(grep -E '^exit=' "$log" | tail -n 1)"
        local last; last="$(grep -E '^\[module\] step:' "$log" | tail -n 1)"
        echo "[$label] run $run/$RUNS failed (${code:-no exit code}; last ${last:-step unknown})"
        tail -n 30 "$log"
        local crash; crash="$(crash_report)"
        [ -n "$crash" ] && printf '%s\n' "$crash"
        echo "::error title=$(gh_prop "Realms Quake 2 module on Android ($label) run $run/$RUNS")::$(gh_msg "${code:-no exit code}; ${last:-no step marker}
$(grep -E 'FAIL|skipped' "$log" | head -n 6)
$crash")"
        status=1
        break
    done
    echo "[$label] $passed/$RUNS runs passed; $(grep -E '^\[module\] [0-9]+ checks' "$log" | tail -n 1 || echo 'no summary')"
    rm -f "$log"
}

if [ -n "$MODULE_TEST" ]; then
    run_module "Quake 2 module - C++ core" native-cpp
    run_module "Quake 2 module - Free Pascal core" pascal-quake2 --pascal "$DIR/librealms_core.so"
fi

"$ADB" shell "rm -rf $DIR" || true
exit "$status"
