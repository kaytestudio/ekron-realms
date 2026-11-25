#!/usr/bin/env bash
# The PortMaster package for aarch64 handhelds (Anbernic RG40XXH, ...): the
# installed build (bin/, lib/ekron-realms/, share/ekron-realms/) in
# ekronrealms/, with the launch scripts and port.json from sys/portmaster.
#   tools/make_portmaster_zip.sh <cmake build dir> <output .zip>
set -euo pipefail

if [ "$#" -ne 2 ]; then
  echo "usage: $0 <cmake build dir> <output .zip>" >&2
  exit 2
fi
root="$(cd "$(dirname "$0")/.." && pwd)"
build="$(cd "$1" && pwd)"
out="$(realpath -m "$2")"
assets="$root/sys/android/app/src/main/assets"

stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
DESTDIR="$stage/install" cmake --install "$build" --prefix / > /dev/null

port="$stage/zip/ekronrealms"
mkdir -p "$port/bin" "$port/licenses" "$port/share"
cp -p "$stage/install/bin/ekron-realms" "$port/bin/"
# lib/ekron-realms: the Free Pascal core and the Quake 2 game module (when fpc built them)
[ -d "$stage/install/lib" ] && cp -a "$stage/install/lib" "$port/"
cp -a "$stage/install/share/ekron-realms" "$port/share/"
cp "$root/sys/portmaster/port.json" "$root/sys/portmaster/README.md" "$root/sys/portmaster/gameinfo.xml" "$port/"
cp "$root/RINGSCE_v2.png" "$port/cover.png"
cp "$root/LICENSE" "$port/licenses/LICENSE.ekron-realms.txt"
cp "$assets/sounds/LICENSE.md" "$port/licenses/LICENSE.sounds.md"
cp "$assets/models/README.md" "$port/licenses/LICENSE.models.md"
cp "$assets/textures/README.md" "$port/licenses/LICENSE.textures.md"
cp "$root/sys/portmaster/"*.sh "$stage/zip/"

rm -f "$out"
(cd "$stage/zip" && zip -q -r -9 "$out" -- *.sh ekronrealms)
echo "$out"
