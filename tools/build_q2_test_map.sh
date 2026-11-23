#!/bin/sh
# Rebuilds tests/realms/maps/q2room.bsp from q2room.map with id's qbsp3,
# qvis3 and qrad3 (yquake2/maptools, GPL-2.0). Needs curl, make and cc.
set -eu
repo=$(cd "$(dirname "$0")/.." && pwd)
rev=9b7e43334646f63f25a7aebfd62070b3aebc2554
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

curl -sSfL "https://codeload.github.com/yquake2/maptools/tar.gz/$rev" | tar xz -C "$work"
tools="$work/maptools-$rev"
# MakeTnodes aligns a pointer through (int), which crashes on 64-bit, and
# lights without "_color" test _color[1] of "", past the end of the string
sed -i.orig 's/(((int)tnodes + 31)/(((size_t)tnodes + 31)/' "$tools/src/qrad3/trace.c"
sed -i.orig 's/if (_color \&\& _color\[1\])/if (_color \&\& _color[0])/' "$tools/src/qrad3/lightmap.c"
grep -q '(size_t)tnodes' "$tools/src/qrad3/trace.c"
grep -q '_color\[0\]' "$tools/src/qrad3/lightmap.c"
make -C "$tools" -s qbsp3 qvis3 qrad3 >/dev/null 2>&1

# the tools want a quake2/baseq2 tree; qrad3 reads the palette from
# pics/colormap.pcx (a grey ramp will do: there are no .wal textures)
base="$work/quake2/baseq2"
mkdir -p "$base/maps" "$base/pics"
python3 - "$base/pics/colormap.pcx" <<'PY'
import struct, sys
hdr = struct.pack('<BBBBHHHHHH', 10, 5, 1, 8, 0, 0, 0, 0, 1, 1) + bytes(48)
hdr += struct.pack('<BBHH', 0, 1, 2, 1) + bytes(58)
pal = bytes([12]) + bytes(v for i in range(256) for v in (i, i, i))
open(sys.argv[1], 'wb').write(hdr + bytes([0xC1, 0]) + pal)
PY
cp "$repo/tests/realms/maps/q2room.map" "$base/maps/"
"$tools/release/qbsp3" "$base/maps/q2room.map"
"$tools/release/qvis3" "$base/maps/q2room"
"$tools/release/qrad3" "$base/maps/q2room"
cp "$base/maps/q2room.bsp" "$repo/tests/realms/maps/q2room.bsp"
echo "wrote tests/realms/maps/q2room.bsp"
