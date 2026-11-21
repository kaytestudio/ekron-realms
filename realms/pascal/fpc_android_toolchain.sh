#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# Realms Engine - build Free Pascal cross compilers for Android.
#
#   realms/pascal/fpc_android_toolchain.sh <fpc-source-dir> <install-prefix> [cpu...]
#
# cpu: aarch64 (arm64-v8a, default) and/or x86_64 (emulators).
#
# Builds only what librealms_core needs: the cross compiler and its RTL
# ("buildbase", no packages). Requirements:
#   - a native FPC of the same release as the sources (e.g. apt's fpc 3.2.2
#     with https://github.com/fpc/FPCSource tag release_3_2_2)
#   - GNU binutils for the target CPU, used as FPC's external assembler and
#     linker (FPC 3.2 cannot drive the NDK's LLVM tools for Android):
#       aarch64: apt install binutils-aarch64-linux-gnu  (aarch64-linux-gnu-*)
#       x86_64:  the host binutils                        (x86_64-linux-gnu-*)
#     The output links only against the NDK's bionic libraries, which
#     build_core.sh passes in with -Fl.
#
# Result: <install-prefix>/lib/fpc/<version>/ppcross{a64,x64} and
#         <install-prefix>/lib/fpc/<version>/units/<cpu>-android/rtl
# -----------------------------------------------------------------------------
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
