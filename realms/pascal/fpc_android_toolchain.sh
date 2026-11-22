#!/usr/bin/env bash
# Build FPC cross compilers and RTL for Android.
#
#   realms/pascal/fpc_android_toolchain.sh <fpc-source-dir> <install-prefix> [cpu...]
#
# cpu: aarch64 (default) and/or x86_64. Needs a native fpc of the same
# release as the sources, and GNU binutils for the target CPU
# (binutils-aarch64-linux-gnu for aarch64); FPC 3.2 can't use the NDK's
# LLVM tools.
#
# Output: <prefix>/lib/fpc/<version>/ppcross{a64,x64} and units/<cpu>-android/rtl
set -euo pipefail

SRC="$(cd "${1:?usage: $0 <fpc-source-dir> <install-prefix> [cpu...]}" && pwd)"
PREFIX="${2:?usage: $0 <fpc-source-dir> <install-prefix> [cpu...]}"
shift 2
CPUS=("$@")
[ "${#CPUS[@]}" -gt 0 ] || CPUS=(aarch64)
mkdir -p "$PREFIX"
PREFIX="$(cd "$PREFIX" && pwd)"
FPC_BOOT="${FPC:-fpc}"

command -v "$FPC_BOOT" >/dev/null || { echo "error: native fpc not found" >&2; exit 1; }
echo "bootstrap compiler: $("$FPC_BOOT" -iV) ($("$FPC_BOOT" -iSO)-$("$FPC_BOOT" -iSP))"

for cpu in "${CPUS[@]}"; do
    case "$cpu" in
        aarch64) binutils=aarch64-linux-gnu- ;;
        x86_64)  binutils=x86_64-linux-gnu- ;;
        *) echo "error: unsupported Android CPU '$cpu'" >&2; exit 2 ;;
    esac
    command -v "${binutils}as" >/dev/null && command -v "${binutils}ld" >/dev/null || {
        echo "error: ${binutils}as / ${binutils}ld not found (install GNU binutils for $cpu)" >&2; exit 1; }

    echo "=== FPC cross compiler for $cpu-android ==="
    common=(OS_TARGET=android CPU_TARGET="$cpu" CROSSINSTALL=1
            BINUTILSPREFIX="$binutils" FPC="$FPC_BOOT" CROSSOPT="-Cg -O2"
            INSTALL_PREFIX="$PREFIX")
    make -C "$SRC" clean "${common[@]}" >/dev/null 2>&1 || true
    make -C "$SRC" buildbase "${common[@]}"
    make -C "$SRC" installbase "${common[@]}"
done

echo "=== installed ==="
find "$PREFIX" -maxdepth 4 -name 'ppcross*' -print
find "$PREFIX" -type d -path '*-android/rtl' -print
