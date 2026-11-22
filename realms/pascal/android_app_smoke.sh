#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# Launch the real APK on a device/emulator and check which Realms core it runs.
#
#   realms/pascal/android_app_smoke.sh <app.apk> [expected-core] [game]
#
# expected-core defaults to pascal-quake2 (the Free Pascal core shipped in
# jniLibs). The app logs "Realms engine ready: core=<name> origin=<path>"
# from Renderer's constructor, before any GL work, so this only needs the
# activity to start. With "game" the Quake 2 game module (librealms_game.so
# in jniLibs) must also load: "Quake 2 game module: <path>" - dlopen runs
# its Free Pascal RTL start-up next to the core's. Requires adb with exactly
# one device attached.
# -----------------------------------------------------------------------------
set -euo pipefail

APK="${1:?usage: $0 <app.apk> [expected-core]}"
EXPECT="${2:-pascal-quake2}"
EXPECT_GAME="${3:-}"
PKG="${REALMS_APP_ID:-com.example.realms}"
ACTIVITY="${REALMS_APP_ACTIVITY:-.MainActivity}"
LOG=/tmp/realms-app-logcat.txt

fail() {
    echo "::error title=Realms app smoke::$1"
    echo "--- logcat (Realms, crashes) ---"
    adb logcat -d -s Realms:V AndroidRuntime:E DEBUG:V libc:F 2>/dev/null | tail -n 80 || true
    adb logcat -d -b crash 2>/dev/null | tail -n 40 || true
    exit 1
}

adb wait-for-device
echo "device ABI: $(adb shell getprop ro.product.cpu.abi | tr -d '\r'), API $(adb shell getprop ro.build.version.sdk | tr -d '\r')"
adb install -r -g "$APK" >/dev/null || fail "adb install failed"
adb logcat -c
adb shell am start -W -n "$PKG/$ACTIVITY" | tr -d '\r'

ready=""
for _ in $(seq 1 90); do
    adb logcat -d -s Realms:V > "$LOG" 2>/dev/null || true
    ready="$(grep -m1 'Realms engine ready' "$LOG" || true)"
    [ -n "$ready" ] && break
    if adb logcat -d -b crash 2>/dev/null | grep "$PKG" >/dev/null; then
        fail "the app crashed before the Realms engine started"
    fi
    sleep 1
done
cat "$LOG"
[ -n "$ready" ] || fail "no 'Realms engine ready' line within 90 s"

case "$ready" in
    *"core=$EXPECT "*) echo "OK: $ready"; echo "::notice title=Realms app smoke::${ready#*Realms: }" ;;
    *) fail "expected core=$EXPECT, got: ${ready#*Realms engine ready: }" ;;
esac

if [ "$EXPECT_GAME" = game ]; then
    game=""
    for _ in $(seq 1 15); do
        adb logcat -d -s Realms:V > "$LOG" 2>/dev/null || true
        game="$(grep -m1 'Quake 2 game module' "$LOG" || true)"
        [ -n "$game" ] && break
        sleep 1
    done
    case "$game" in
        *"Quake 2 game module: "*) echo "OK: $game"; echo "::notice title=Realms app smoke::${game#*Realms: }" ;;
        *) fail "the Quake 2 game module did not load: ${game:-no 'Quake 2 game module' line}" ;;
    esac
fi

# The Pascal RTL must also survive past start-up (frames run cvars/commands).
sleep 8
if adb logcat -d -b crash 2>/dev/null | grep "$PKG" >/dev/null; then
    fail "the app crashed after the Realms engine started"
fi
pid="$(adb shell pidof "$PKG" | tr -d '\r' || true)"
if [ -z "$pid" ]; then
    echo "::warning title=Realms app smoke::$PKG is no longer running (no crash recorded)"
else
    echo "OK: $PKG still running (pid $pid)"
    echo "::notice title=Realms app smoke::$PKG still running 8 s after the engine started"
fi
