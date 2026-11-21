#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# Realms Engine - build the Free Pascal core (librealms_core) for a platform.
#
#   realms/pascal/build_core.sh <target> [output-dir]
#
# Targets:
#   host              librealms_core.so/.dylib for the machine running fpc
#                     (the desktop CMake build does this automatically when
#                     -DENABLE_PASCAL_CORE=ON and fpc is installed)
#   android-aarch64   -> sys/android/app/src/main/jniLibs/arm64-v8a/
#   android-armv7     -> sys/android/app/src/main/jniLibs/armeabi-v7a/
#   android-x86_64    -> sys/android/app/src/main/jniLibs/x86_64/
#   ios-arm64         -> build/realms-ios/librealms_core.a  (static)
#
# Android: needs an FPC cross compiler for the target (ppcrossa64/ppcrossarm/
# ppcx64 + android RTL) and the NDK. Set ANDROID_NDK_HOME; the script puts the
# NDK LLVM tools on PATH. Once the .so is in jniLibs, realms::Engine loads it
# automatically (REALMS_CORE=auto) - nothing else changes in the APK build.
#
# iOS: apps cannot ship dynamic Pascal libraries, so the core is compiled to
# objects and archived together with the FPC RTL units it uses. Link the
# archive into the app and add REALMS_PASCAL_STATIC=1 to the preprocessor
# definitions; realms::Engine then calls realms_pascal_get_api directly.
# FPC registers unit initialisation in __mod_init_func on Darwin, so the RTL
# starts up when the app image loads.
# -----------------------------------------------------------------------------
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
TARGET="${1:-host}"
FPC="${FPC:-fpc}"
COMMON=(-Mobjfpc -Sh -O2 -Xs "-Fu$HERE" "-Fi$HERE")

command -v "$FPC" >/dev/null 2>&1 || { echo "error: fpc not found (set FPC=/path/to/fpc)" >&2; exit 1; }

ndk_bin() {
    local ndk="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}"
    [ -n "$ndk" ] || { echo "error: set ANDROID_NDK_HOME" >&2; exit 1; }
    local host_tag
    case "$(uname -s)" in
        Linux) host_tag=linux-x86_64 ;;
        Darwin) host_tag=darwin-x86_64 ;;
        *) echo "error: unsupported build host" >&2; exit 1 ;;
    esac
    echo "$ndk/toolchains/llvm/prebuilt/$host_tag/bin"
}

build_android() {
    local cpu="$1" abi="$2" triple="$3"; shift 3
    local out="${OUT_DIR:-$ROOT/sys/android/app/src/main/jniLibs/$abi}"
    local units; units="$(mktemp -d)"
    local bin; bin="$(ndk_bin)"
    local prefix=()
    mkdir -p "$out"
    # Cross binutils: use <triple>-ld etc. when present (fpcupdeluxe installs
    # them); otherwise rely on the cross settings in fpc.cfg.
    if PATH="$bin:$PATH" command -v "$triple-ld" >/dev/null 2>&1; then
        prefix=("-XP$triple-")
    fi
    PATH="$bin:$PATH" "$FPC" -Tandroid "-P$cpu" "$@" -Cg "${prefix[@]}" ${FPC_ANDROID_FLAGS:-} \
        "${COMMON[@]}" "-FU$units" "-FE$out" "$HERE/realms_core.lpr"
    rm -rf "$units"
    echo "built $out/librealms_core.so"
}

case "$TARGET" in
    host)
        out="${2:-$ROOT/build}"
        mkdir -p "$out"
        units="$(mktemp -d)"
        extra=()
        [ "$(uname -s)" = Linux ] && extra+=(-Cg)
        "$FPC" "${COMMON[@]}" "${extra[@]}" "-FU$units" "-FE$out" "$HERE/realms_core.lpr"
        rm -rf "$units"
        ls -l "$out"/librealms_core.*
        ;;
    android-aarch64)
        OUT_DIR="${2:-}" build_android aarch64 arm64-v8a aarch64-linux-android
        ;;
    android-armv7)
        OUT_DIR="${2:-}" build_android arm armeabi-v7a arm-linux-androideabi -CpARMV7A -CfVFPV3
        ;;
    android-x86_64)
        OUT_DIR="${2:-}" build_android x86_64 x86_64 x86_64-linux-android
        ;;
    ios-arm64)
        out="${2:-$ROOT/build/realms-ios}"
        mkdir -p "$out"
        objs="$(mktemp -d)"
        SDK="$(xcrun --sdk iphoneos --show-sdk-path)"
        # -Cn: compile + assemble only; the app's Xcode link step does the rest.
        "$FPC" -Tios -Paarch64 -Cn -XX -CX "-XR$SDK" -WP16.0 \
            "${COMMON[@]}" "-FU$objs" "-FE$objs" "$HERE/realms_core.lpr"
        # Archive the core objects plus the RTL/FCL units they reference.
        # RTL units for aarch64-ios (override with FPC_IOS_UNITS).
        RTL_DIR="${FPC_IOS_UNITS:-$(dirname "$(command -v "$FPC")")/../lib/fpc/$("$FPC" -iV)/units/aarch64-ios}"
        [ -d "$RTL_DIR" ] || { echo "error: aarch64-ios RTL not found at $RTL_DIR (set FPC_IOS_UNITS)" >&2; exit 1; }
        rtl_objs=()
        for u in system objpas sysutils math ctypes unixtype baseunix unix sysconst \
                 errors cthreads initc dl unixutil syscall rtlconsts typinfo; do
            while IFS= read -r f; do rtl_objs+=("$f"); done < <(find "$RTL_DIR" -name "$u.o" 2>/dev/null)
        done
        xcrun libtool -static -o "$out/librealms_core.a" "$objs"/*.o "${rtl_objs[@]}"
        rm -rf "$objs"
        echo "built $out/librealms_core.a"
        echo "Xcode: link it and add REALMS_PASCAL_STATIC=1 to GCC_PREPROCESSOR_DEFINITIONS."
        ;;
    *)
        sed -n '2,30p' "$0"
        exit 2
        ;;
esac
