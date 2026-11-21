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
#   ios-arm64         -> build/realms-ios/librealms_core.a  (static, device)
#   macos-arm64-static  -> build/realms-macos-static/librealms_core.a; the same
#                     static pipeline as iOS for a CPU/OS that can run tests
#
# Android: needs a Free Pascal cross compiler + Android RTL for the CPU
# (build one with realms/pascal/fpc_android_toolchain.sh and point
# FPC_ANDROID_PREFIX at its install prefix), GNU binutils for the CPU
# (aarch64-linux-gnu-* / x86_64-linux-gnu-*) and the NDK (ANDROID_NDK_HOME)
# for bionic: the library links only against the NDK sysroot of
# ANDROID_API (default 30 = the app's minSdk). Once the .so is in jniLibs,
# realms::Engine loads it automatically (REALMS_CORE=auto) - nothing else
# changes in the APK build.
#
# iOS: apps cannot load their own dynamic libraries, so the core is linked
# statically. FPC compiles it with -Cn (no link step) and records every object
# the library needs, RTL included, in linkfiles*.res; those are prelinked with
# `ld -r` into ONE object and archived. That matters: on Darwin FPC starts the
# RTL from a __mod_init_func entry (FPC_LIBMAIN) in the library's main object,
# which also defines realms_pascal_get_api - referencing the entry point pulls
# the whole core, init entry included, into the app. (A plain archive of the
# unit objects would let the linker drop the init and crash on first use.)
# Needs the aarch64-ios RTL from realms/pascal/fpc_ios_toolchain.sh
# (FPC_IOS_PREFIX=<prefix>, or FPC_IOS_UNITS=<units dir>). The script also
# writes RealmsPascal.generated.xcconfig next to the archive; the Xcode
# project includes it, so device builds then link the Pascal core and define
# REALMS_PASCAL_STATIC=1 automatically. Without it iOS uses the C++ core.
# -----------------------------------------------------------------------------
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
TARGET="${1:-host}"
FPC="${FPC:-fpc}"
COMMON=(-Mobjfpc -Sh -O2 -Xs "-Fu$HERE" "-Fi$HERE")

command -v "$FPC" >/dev/null 2>&1 || { echo "error: fpc not found (set FPC=/path/to/fpc)" >&2; exit 1; }

ndk_sysroot_lib() {
    local triple="$1"
    local ndk="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}"
    [ -n "$ndk" ] || { echo "error: set ANDROID_NDK_HOME" >&2; exit 1; }
    local host_tag
    case "$(uname -s)" in
        Linux) host_tag=linux-x86_64 ;;
        Darwin) host_tag=darwin-x86_64 ;;
        *) echo "error: unsupported build host" >&2; exit 1 ;;
    esac
    local dir="$ndk/toolchains/llvm/prebuilt/$host_tag/sysroot/usr/lib/$triple/${ANDROID_API:-30}"
    [ -f "$dir/crtbegin_so.o" ] || { echo "error: NDK sysroot not found at $dir" >&2; exit 1; }
    echo "$dir"
}

build_android() {
    local cpu="$1" abi="$2" ndk_triple="$3" binutils="$4" ppc="$5"; shift 5
    local out="${OUT_DIR:-$ROOT/sys/android/app/src/main/jniLibs/$abi}"
    local units; units="$(mktemp -d)"
    local sysroot_lib; sysroot_lib="$(ndk_sysroot_lib "$ndk_triple")"
    local compiler="$FPC" rtl=()
    if [ -n "${FPC_ANDROID_PREFIX:-}" ]; then
        compiler="$(find "$FPC_ANDROID_PREFIX" -name "$ppc" -type f | head -n 1)"
        [ -x "$compiler" ] || { echo "error: $ppc not found under $FPC_ANDROID_PREFIX" >&2; exit 1; }
        local rtl_dir; rtl_dir="$(dirname "$compiler")/units/$cpu-android/rtl"
        [ -d "$rtl_dir" ] || { echo "error: $cpu-android RTL not found at $rtl_dir" >&2; exit 1; }
        # -n: ignore the host fpc.cfg; everything the core needs is explicit.
        rtl=(-n "-Fu$rtl_dir")
    fi
    mkdir -p "$out"
    "$compiler" "${rtl[@]}" -Tandroid "-P$cpu" "$@" -Cg "-XP$binutils" "-Fl$sysroot_lib" \
        -k-z -kmax-page-size=16384 ${FPC_ANDROID_FLAGS:-} \
        "${COMMON[@]}" "-FU$units" "-FE$out" "$HERE/realms_core.lpr"
    rm -rf "$units"
    echo "built $out/librealms_core.so"
}

# build_darwin_static <out> <xcrun-sdk> <fpc target flags...>
build_darwin_static() {
    local out="$1" sdk="$2"; shift 2
    local objs; objs="$(mktemp -d)"
    mkdir -p "$out"
    # -Cn: compile and assemble only; FPC still writes the link inputs.
    "$FPC" "$@" -Cn "${COMMON[@]}" "-FU$objs" "-FE$objs" "$HERE/realms_core.lpr"
    local filelist; filelist="$(ls "$objs"/linkfiles*.res 2>/dev/null | head -n 1)"
    [ -f "$filelist" ] || { echo "error: FPC wrote no linkfiles*.res in $objs" >&2; exit 1; }
    echo "prelinking $(wc -l < "$filelist" | tr -d ' ') objects:"
    sed 's/^/  /' "$filelist"
    (cd "$objs" && xcrun --sdk "$sdk" ld -r -arch arm64 -o realms_core_prelinked.o -filelist "$filelist")
    rm -f "$out/librealms_core.a"
    xcrun --sdk "$sdk" libtool -static -o "$out/librealms_core.a" "$objs/realms_core_prelinked.o"
    # The archive must export the entry point and carry the RTL init entry.
    nm -gU "$out/librealms_core.a" | grep -q ' _realms_pascal_get_api$' || {
        echo "error: _realms_pascal_get_api is not exported by $out/librealms_core.a" >&2; exit 1; }
    otool -l "$objs/realms_core_prelinked.o" | grep -q __mod_init_func || {
        echo "error: no __mod_init_func (FPC_LIBMAIN) in the prelinked core" >&2; exit 1; }
    rm -rf "$objs"
    ls -l "$out/librealms_core.a"
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
        OUT_DIR="${2:-}" build_android aarch64 arm64-v8a aarch64-linux-android aarch64-linux-gnu- ppcrossa64
        ;;
    android-armv7)
        OUT_DIR="${2:-}" build_android arm armeabi-v7a arm-linux-androideabi arm-linux-gnueabihf- ppcrossarm -CpARMV7A -CfVFPV3
        ;;
    android-x86_64)
        OUT_DIR="${2:-}" build_android x86_64 x86_64 x86_64-linux-android x86_64-linux-gnu- ppcrossx64
        ;;
    ios-arm64)
        SDK="$(xcrun --sdk iphoneos --show-sdk-path)"
        IOS_UNITS="${FPC_IOS_UNITS:-}"
        if [ -z "$IOS_UNITS" ] && [ -n "${FPC_IOS_PREFIX:-}" ]; then
            IOS_UNITS="$FPC_IOS_PREFIX/lib/fpc/$("$FPC" -iV)/units/aarch64-ios/rtl"
        fi
        [ -n "$IOS_UNITS" ] && [ -f "$IOS_UNITS/system.ppu" ] || {
            echo "error: aarch64-ios RTL not found (build it with fpc_ios_toolchain.sh, set FPC_IOS_PREFIX)" >&2; exit 1; }
        out="${2:-$ROOT/build/realms-ios}"
        build_darwin_static "$out" iphoneos -n "-Fu$IOS_UNITS" -Tios -Paarch64 "-XR$SDK" \
            "-WP${IOS_DEPLOYMENT_TARGET:-16.0}"
        cat > "$out/RealmsPascal.generated.xcconfig" <<XCCONFIG
// Generated by realms/pascal/build_core.sh ios-arm64 - do not edit.
// Device builds link the Free Pascal core; simulator builds keep the C++ core
// (FPC 3.2 has no arm64 simulator target).
REALMS_PASCAL_DEFINES[sdk=iphoneos*] = REALMS_PASCAL_STATIC=1
REALMS_PASCAL_LDFLAGS[sdk=iphoneos*] = "$out/librealms_core.a"
XCCONFIG
        echo "wrote $out/RealmsPascal.generated.xcconfig"
        ;;
    macos-arm64-static)
        out="${2:-$ROOT/build/realms-macos-static}"
        build_darwin_static "$out" macosx -Tdarwin -Paarch64
        ;;
    *)
        sed -n '3,/^# -----/p' "$0"
        exit 2
        ;;
esac
