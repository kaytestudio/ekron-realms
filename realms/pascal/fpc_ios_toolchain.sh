#!/usr/bin/env bash
# Build the FPC RTL for aarch64-ios on an Apple Silicon Mac. The host fpc
# already targets iOS; only the RTL is missing, and it must come from sources
# of the same FPC version:
#
#   ver=$(fpc -iV)
#   curl -L https://codeload.github.com/fpc/FPCSource/tar.gz/refs/tags/release_${ver//./_} | tar xz
#   realms/pascal/fpc_ios_toolchain.sh FPCSource-release_${ver//./_} "$HOME/fpc-ios"
#
# Output: <prefix>/lib/fpc/<ver>/units/aarch64-ios/rtl.
# Environment: FPC (default fpc), IOS_DEPLOYMENT_TARGET (default 16.0).
set -euo pipefail

SRC="${1:?usage: $0 <fpc-source-dir> <install-prefix>}"
PREFIX="${2:?usage: $0 <fpc-source-dir> <install-prefix>}"
FPC="${FPC:-fpc}"
MIN="${IOS_DEPLOYMENT_TARGET:-16.0}"

[ "$(uname -s)" = Darwin ] || { echo "error: the iOS RTL is built on macOS (needs Xcode)" >&2; exit 1; }
[ -f "$SRC/rtl/Makefile" ] || { echo "error: $SRC is not an FPC source tree" >&2; exit 1; }
command -v "$FPC" >/dev/null 2>&1 || { echo "error: host fpc not found (brew install fpc)" >&2; exit 1; }
if ! "$FPC" -Paarch64 -iTP >/dev/null 2>&1 || [ "$("$FPC" -Paarch64 -iTP)" != aarch64 ]; then
    echo "error: $FPC has no aarch64 compiler (use an Apple Silicon Mac / macos-14+ runner)" >&2
    exit 1
fi

SRC="$(cd "$SRC" && pwd)"
mkdir -p "$PREFIX"
PREFIX="$(cd "$PREFIX" && pwd)"
SDK="$(xcrun --sdk iphoneos --show-sdk-path)"
VER="$("$FPC" -iV)"
SRC_VER="$(sed -n 's/^version=//p' "$SRC/rtl/Makefile.fpc" | head -1)"
if [ -n "$SRC_VER" ] && [ "$SRC_VER" != "$VER" ]; then
    echo "error: FPC sources are $SRC_VER but the host compiler is $VER" >&2
    exit 1
fi

# -n: ignore the host fpc.cfg (its macOS SDK/unit paths must not leak in).
OPTS="-n -XR$SDK -WP$MIN -O2"
MAKE_ARGS=(OS_TARGET=ios CPU_TARGET=aarch64 "FPC=$(command -v "$FPC")" "OPT=$OPTS" CROSSINSTALL=1)

echo "== FPC $VER RTL for aarch64-ios (iOS $MIN, SDK $SDK)"
make -C "$SRC/rtl" clean "${MAKE_ARGS[@]}" >/dev/null
make -C "$SRC/rtl" all "${MAKE_ARGS[@]}"
make -C "$SRC/rtl" install "${MAKE_ARGS[@]}" "INSTALL_PREFIX=$PREFIX"

UNITS="$PREFIX/lib/fpc/$VER/units/aarch64-ios/rtl"
for u in system objpas sysutils ctypes cthreads sysinit; do
    [ -f "$UNITS/$u.ppu" ] || { echo "error: $u.ppu missing from $UNITS" >&2; exit 1; }
done
echo "installed $UNITS"
