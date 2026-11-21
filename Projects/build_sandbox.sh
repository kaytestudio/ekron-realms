#!/bin/sh
# -----------------------------------------------------------------------------
# Ekron Realms - build libekron_sandbox, the C++ SDL2 sandbox that the Free
# Pascal client (Projects/realms.lpr -> realms-pascal) links and runs.
#
#   Projects/build_sandbox.sh            # configures <repo>/build if needed
#   EKRON_BUILD_DIR=/path Projects/build_sandbox.sh
#
# Projects/realms.lpi and the root realms.lpi run this before compiling, so
# Lazarus / lazbuild builds work from a fresh checkout. Needs cmake, a C++17
# compiler, SDL2 development files and fpc (see README.md).
# -----------------------------------------------------------------------------
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(dirname "$here")
build=${EKRON_BUILD_DIR:-$root/build}

if [ ! -f "$build/CMakeCache.txt" ]; then
    cmake -S "$root" -B "$build" -DCMAKE_BUILD_TYPE=Release -DENABLE_FPC_CLIENT=ON
elif ! grep -q '^ENABLE_FPC_CLIENT:BOOL=ON' "$build/CMakeCache.txt"; then
    cmake -S "$root" -B "$build" -DENABLE_FPC_CLIENT=ON
fi
cmake --build "$build" --target ekron_sandbox
# Unit output directory used by `fpc @realms-fpc.cfg Projects/realms.lpr`.
mkdir -p "$build/fpc-units/realms-pascal-cfg"
