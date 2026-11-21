#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# Realms Engine - run the bridge conformance suite on an Android device or
# emulator, against both the C++ reference core and the Free Pascal core.
#
#   realms/pascal/android_device_test.sh <realms_bridge_test> <librealms_core.so>
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
echo "device: $("$ADB" shell getprop ro.product.cpu.abi | tr -d '\r') API $("$ADB" shell getprop ro.build.version.sdk | tr -d '\r')"
"$ADB" shell "rm -rf $DIR && mkdir -p $DIR/tmp"
"$ADB" push "$TEST_BIN" "$DIR/realms_bridge_test" >/dev/null
"$ADB" push "$CORE_LIB" "$DIR/librealms_core.so" >/dev/null
"$ADB" shell "chmod 755 $DIR/realms_bridge_test"

status=0
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
        echo "::error title=Realms bridge on Android ($label)::$(grep -E 'FAIL|exit=' "$log" | head -n 6 | tr '\n' ' ' | cut -c1-900)"
        status=1
    fi
    rm -f "$log"
}

run_suite "C++ reference core" native-cpp
run_suite "Free Pascal core" pascal-quake2 --pascal "$DIR/librealms_core.so"

"$ADB" shell "rm -rf $DIR" || true
exit "$status"
