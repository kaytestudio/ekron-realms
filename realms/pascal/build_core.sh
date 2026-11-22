#!/usr/bin/env bash
# Build librealms_core (and librealms_game) for a platform.
#
#   realms/pascal/build_core.sh <target> [output-dir]
#
#   host                     librealms_core for this machine
#   android-{aarch64,armv7,x86_64}       core into sys/android/.../jniLibs/<abi>/
#   game-android-{aarch64,armv7,x86_64}  librealms_game.so into the same folder
#   ios-arm64                build/realms-ios/librealms_{core,game}.a
#   macos-arm64-static       the iOS static pipeline on a Mac, for tests
#
# Android: FPC_ANDROID_PREFIX (from fpc_android_toolchain.sh), GNU binutils
# for the CPU, ANDROID_NDK_HOME. Links against bionic at ANDROID_API (30).
#
# iOS: FPC_IOS_PREFIX or FPC_IOS_UNITS (from fpc_ios_toolchain.sh). The core
# is compiled with -Cn and prelinked with `ld -r` into one object, so the
# FPC_LIBMAIN __mod_init_func entry can't be dropped by the app linker. Also
# writes RealmsPascal.generated.xcconfig for the Xcode project. Apps linking
# the archive need -Wl,-no_fixup_chains: FPC 3.2 tables hold pointers that
# are only 4-byte aligned.
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
    if [ "${BUILD_GAME:-0}" = 1 ]; then
        # REALMS_GAME_STRIP=0 keeps symbols for crash backtraces
        local strip=(-Xs)
        [ "${REALMS_GAME_STRIP:-1}" = 0 ] && strip=(-gl)
        "$compiler" "${rtl[@]}" -Tandroid "-P$cpu" "$@" -Cg "-XP$binutils" "-Fl$sysroot_lib" \
            -k-z -kmax-page-size=16384 ${FPC_ANDROID_FLAGS:-} \
            -Mdelphi -dGAMEDLL -O2 "${strip[@]}" "-FU$units" "-FE$out" "$ROOT/game/realms_game.lpr"
        rm -rf "$units"
        echo "built $out/librealms_game.so"
        return
    fi
    "$compiler" "${rtl[@]}" -Tandroid "-P$cpu" "$@" -Cg "-XP$binutils" "-Fl$sysroot_lib" \
        -k-z -kmax-page-size=16384 ${FPC_ANDROID_FLAGS:-} \
        "${COMMON[@]}" "-FU$units" "-FE$out" "$HERE/realms_core.lpr"
    rm -rf "$units"
    echo "built $out/librealms_core.so"
}

# build_darwin_static <out> <xcrun-sdk> <fpc target flags...>
# BUILD_GAME=1 packages the Quake 2 game module (game/realms_game.lpr) as
# librealms_game.a instead. The app then links two Free Pascal programs, each
# with its own RTL, so the game's prelinked object keeps only _GetGameAPI
# global: ld -r -exported_symbol turns every other symbol (RTL included)
# local, like a dylib's private symbols, and the two RTLs cannot clash. Each
# still starts from its own __mod_init_func entry.
build_darwin_static() {
    local out="$1" sdk="$2"; shift 2
    local objs; objs="$(mktemp -d)"
    local name=realms_core entry=_realms_pascal_get_api src="$HERE/realms_core.lpr" flags=("${COMMON[@]}") hide=()
    if [ "${BUILD_GAME:-0}" = 1 ]; then
        name=realms_game entry=_GetGameAPI src="$ROOT/game/realms_game.lpr"
        flags=(-Mdelphi -dGAMEDLL -O2)
        hide=(-exported_symbol "$entry")
    fi
    mkdir -p "$out"
    # -Cn: compile and assemble only; FPC still writes the link inputs.
    "$FPC" "$@" -Cn "${flags[@]}" "-FU$objs" "-FE$objs" "$src"
    local filelist; filelist="$(ls "$objs"/linkfiles*.res 2>/dev/null | head -n 1)"
    [ -f "$filelist" ] || { echo "error: FPC wrote no linkfiles*.res in $objs" >&2; exit 1; }
    echo "prelinking $(wc -l < "$filelist" | tr -d ' ') objects:"
    sed 's/^/  /' "$filelist"
    if ! (cd "$objs" && xcrun --sdk "$sdk" ld -r -arch arm64 ${hide[@]+"${hide[@]}"} -o "${name}_prelinked.o" -filelist "$filelist"); then
        [ "${#hide[@]}" -gt 0 ] || exit 1
        # an ld without -exported_symbol for -r: prelink, then keep only the
        # entry point global with nmedit
        echo "ld -r -exported_symbol failed; hiding the symbols with nmedit"
        (cd "$objs" && xcrun --sdk "$sdk" ld -r -arch arm64 -o "${name}_prelinked.o" -filelist "$filelist")
        echo "$entry" > "$objs/exports.txt"
        xcrun --sdk "$sdk" nmedit -s "$objs/exports.txt" "$objs/${name}_prelinked.o"
    fi
    rm -f "$out/lib$name.a"
    xcrun --sdk "$sdk" libtool -static -o "$out/lib$name.a" "$objs/${name}_prelinked.o"
    # The archive must export the entry point and carry the RTL init entry.
    # (grep without -q: an early exit would SIGPIPE the producer under pipefail)
    nm -gU "$out/lib$name.a" | grep " $entry\$" >/dev/null || {
        echo "error: $entry is not exported by $out/lib$name.a" >&2; exit 1; }
    otool -l "$objs/${name}_prelinked.o" | grep __mod_init_func >/dev/null || {
        echo "error: no __mod_init_func (FPC_LIBMAIN) in the prelinked $name" >&2; exit 1; }
    if [ "${BUILD_GAME:-0}" = 1 ]; then
        local extra; extra="$(nm -gU "$out/lib$name.a" | grep -E ' [A-Z] ' | grep -v " $entry\$" | head -n 5 || true)"
        [ -z "$extra" ] || { echo "error: $out/lib$name.a exports more than $entry:" >&2; echo "$extra" >&2; exit 1; }
    fi
    rm -rf "$objs"
    ls -l "$out/lib$name.a"
}

# The iOS device library search for the aarch64-ios RTL.
ios_units() {
    local units="${FPC_IOS_UNITS:-}"
    if [ -z "$units" ] && [ -n "${FPC_IOS_PREFIX:-}" ]; then
        units="$FPC_IOS_PREFIX/lib/fpc/$("$FPC" -iV)/units/aarch64-ios/rtl"
    fi
    [ -n "$units" ] && [ -f "$units/system.ppu" ] || {
        echo "error: aarch64-ios RTL not found (build it with fpc_ios_toolchain.sh, set FPC_IOS_PREFIX)" >&2; exit 1; }
    echo "$units"
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
    game-android-aarch64)
        BUILD_GAME=1 OUT_DIR="${2:-}" build_android aarch64 arm64-v8a aarch64-linux-android aarch64-linux-gnu- ppcrossa64
        ;;
    game-android-armv7)
        BUILD_GAME=1 OUT_DIR="${2:-}" build_android arm armeabi-v7a arm-linux-androideabi arm-linux-gnueabihf- ppcrossarm -CpARMV7A -CfVFPV3
        ;;
    game-android-x86_64)
        BUILD_GAME=1 OUT_DIR="${2:-}" build_android x86_64 x86_64 x86_64-linux-android x86_64-linux-gnu- ppcrossx64
        ;;
    ios-arm64)
        # The core and the Quake 2 game module, both static (see above).
        SDK="$(xcrun --sdk iphoneos --show-sdk-path)"
        IOS_UNITS="$(ios_units)"
        out="${2:-$ROOT/build/realms-ios}"
        build_darwin_static "$out" iphoneos -n "-Fu$IOS_UNITS" -Tios -Paarch64 "-XR$SDK" \
            "-WP${IOS_DEPLOYMENT_TARGET:-16.0}"
        BUILD_GAME=1 build_darwin_static "$out" iphoneos -n "-Fu$IOS_UNITS" -Tios -Paarch64 "-XR$SDK" \
            "-WP${IOS_DEPLOYMENT_TARGET:-16.0}"
        cat > "$out/RealmsPascal.generated.xcconfig" <<XCCONFIG
// Generated by realms/pascal/build_core.sh ios-arm64 - do not edit.
// Device builds link the Free Pascal core and the Quake 2 game module
// (GameServer::setLinkedGame); simulator builds keep the C++ core and walk
// maps without the game (FPC 3.2 has no arm64 simulator target).
REALMS_PASCAL_DEFINES[sdk=iphoneos*] = REALMS_PASCAL_STATIC=1 REALMS_GAME_STATIC=1
REALMS_PASCAL_LDFLAGS[sdk=iphoneos*] = "$out/librealms_core.a" "$out/librealms_game.a" -Wl,-no_fixup_chains
XCCONFIG
        echo "wrote $out/RealmsPascal.generated.xcconfig"
        ;;
    macos-arm64-static)
        out="${2:-$ROOT/build/realms-macos-static}"
        build_darwin_static "$out" macosx -Tdarwin -Paarch64
        BUILD_GAME=1 build_darwin_static "$out" macosx -Tdarwin -Paarch64
        ;;
    *)
        sed -n '3,/^# -----/p' "$0"
        exit 2
        ;;
esac
