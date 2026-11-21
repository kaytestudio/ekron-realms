#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# Launch the real APK on a device/emulator and check which Realms core it runs.
#
#   realms/pascal/android_app_smoke.sh <app.apk> [expected-core]
#
# expected-core defaults to pascal-quake2 (the Free Pascal core shipped in
# jniLibs). The app logs "Realms engine ready: core=<name> origin=<path>"
# from Renderer's constructor, before any GL work, so this only needs the
# activity to start. Requires adb with exactly one device attached.
# -----------------------------------------------------------------------------
set -euo pipefail

APK="${1:?usage: $0 <app.apk> [expected-core]}"
EXPECT="${2:-pascal-quake2}"
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
    *"core=$EXPECT "*) echo "OK: $ready" ;;
    *) fail "expected core=$EXPECT, got: ${ready#*Realms engine ready: }" ;;
esac

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
fi
