#!/usr/bin/env python3
"""Builds the sample Quake 2 glTF map shipped with the apps; Python standard
library only, deterministic.

Writes, under sys/android/app/src/main/assets (the assets every platform
ships):
  maps/courtyard.glb          walled courtyard: bunker with a sliding door,
                              a lift up to a platform whose button opens the
                              vault, a pool, crates, barrels and items
  env/courtyard_<face>.tga    its skybox (RLE TGA, 512 x 512 per face)

The map uses only what glTF maps support (realms/include/realms/MeshMap.h):
nodes named after Quake 2 entities, brush entities from node names, keys
from extras, the sky from the scene's extras. The base colour textures are
the sandbox's own (textures/*.png, tools/generate_android_textures.py),
embedded so the file opens in any glTF viewer. Run from anywhere:

    python3 tools/build_sample_map.py
"""
import json
import math
import random
import struct
from pathlib import Path

ASSETS = Path(__file__).resolve().parents[1] / 'sys/android/app/src/main/assets'
MAP_NAME = 'courtyard'
SKY_NAME = 'courtyard_'
SKY_SIZE = 512


# --------------------------------------------------------------------------
# Geometry (glTF space: metres, Y up; Quake is (x, -z, y) * 32)

class Primitive:
    def __init__(self):
        self.positions, self.normals, self.uvs, self.indices = [], [], [], []

    def quad(self, corners, normal, scale):
        """Four corners of a planar quad; wound to face `normal`."""
        a, b, c = corners[0], corners[1], corners[2]
        e1 = [b[i] - a[i] for i in range(3)]
        e2 = [c[i] - a[i] for i in range(3)]
        cross = [e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]]
        if sum(cross[i] * normal[i] for i in range(3)) < 0:
            corners = corners[::-1]
        axis = max(range(3), key=lambda i: abs(normal[i]))
        u_axis, v_axis = {0: (2, 1), 1: (0, 2), 2: (0, 1)}[axis]
        base = len(self.positions)
        for p in corners:
            self.positions.append(tuple(p))
            self.normals.append(tuple(normal))
            self.uvs.append((p[u_axis] / scale, -p[v_axis] / scale))
        self.indices += [base, base + 1, base + 2, base, base + 2, base + 3]

    def box(self, x0, x1, y0, y1, z0, z1, scale=2.0, bottom=False):
        """An axis-aligned box facing outwards; no bottom face by default
        (it would sit on the ground)."""
        self.quad([(x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (x1, y0, z1)], (1, 0, 0), scale)
        self.quad([(x0, y0, z0), (x0, y1, z0), (x0, y1, z1), (x0, y0, z1)], (-1, 0, 0), scale)
        self.quad([(x0, y1, z0), (x1, y1, z0), (x1, y1, z1), (x0, y1, z1)], (0, 1, 0), scale)
        if bottom:
            self.quad([(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)], (0, -1, 0), scale)
        self.quad([(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)], (0, 0, 1), scale)
        self.quad([(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0)], (0, 0, -1), scale)


class Mesh:
    def __init__(self):
        self.prims = {}  # material name -> Primitive

    def __getitem__(self, material):
        return self.prims.setdefault(material, Primitive())


def courtyard():
    """Returns (meshes, nodes, scene extras); nodes refer to meshes by name."""
    world = Mesh()
    meshes = {'World': world}
    nodes = [{'name': 'World', 'mesh': 'World'}]

    # 40 x 40 m ground inside 4 m brick walls
    world['asphalt'].quad([(-20, 0, -20), (20, 0, -20), (20, 0, 20), (-20, 0, 20)], (0, 1, 0), 3.0)
    for x0, x1, z0, z1 in ((-20, 20, -20, -19.5), (-20, 20, 19.5, 20), (-20, -19.5, -19.5, 19.5), (19.5, 20, -19.5, 19.5)):
        world['brick'].box(x0, x1, 0, 4, z0, z1)

    # Bunker (north): 10 x 8 m, its doorway faces the spawn
    c = world['concrete']
    c.box(-5, -1, 0, 3.5, -8.5, -8)       # front wall, left of the doorway
    c.box(1, 5, 0, 3.5, -8.5, -8)         # front wall, right
    c.box(-1, 1, 3, 3.5, -8.5, -8)        # lintel
    c.box(-5, 5, 0, 3.5, -16, -15.5)      # back wall
    c.box(-5, -4.5, 0, 3.5, -15.5, -8.5)  # side walls
    c.box(4.5, 5, 0, 3.5, -15.5, -8.5)
    c.box(-5, 5, 3.5, 3.8, -16, -8, bottom=True)  # roof
    # The door slides +X (angle 0) into the wall on its right.
    door = Mesh()
    door['steel door'].box(-1, 1, 0, 3, -8.375, -8.125)
    meshes['Bunker door'] = door
    nodes.append({'name': 'func_door', 'mesh': 'Bunker door',
                  'extras': {'angle': 0, 'speed': 100, 'wait': 4}})

    # Platform (east) 3 m up, a lift on its west side, a button on top
    c.box(10, 16, 0, 3, -4, 2)
    lift = Mesh()
    lift['lift'].box(8, 10, 0, 3, -2, 0)  # top flush with the platform when raised
    meshes['Lift'] = lift
    nodes.append({'name': 'func_plat', 'mesh': 'Lift', 'extras': {'speed': 150}})
    button = Mesh()
    button['button'].box(14.5, 15.5, 3, 3.25, -3.5, -2.5, scale=1.0)
    meshes['Button'] = button
    nodes.append({'name': 'func_button', 'mesh': 'Button',
                  'extras': {'angle': -2, 'target': 'vault', 'wait': 1, 'message': 'The vault is open.'}})

    # Vault (west): opened only by the button
    c.box(-14.5, -14, 0, 3, -7, -5)       # front wall, both sides of the doorway
    c.box(-14.5, -14, 0, 3, -3, -1)
    c.box(-14.5, -14, 2.5, 3, -5, -3)     # lintel
    c.box(-18, -17.5, 0, 3, -7, -1)       # back wall
    c.box(-17.5, -14.5, 0, 3, -7.5, -7)   # side walls
    c.box(-17.5, -14.5, 0, 3, -1, -0.5)
    c.box(-18, -14, 3, 3.3, -7.5, -0.5, bottom=True)  # roof
    vault = Mesh()
    vault['steel door'].box(-14.375, -14.125, 0, 2.5, -5, -3)
    meshes['Vault door'] = vault
    # angle 90 is Quake +Y, glTF -Z: into the wall towards the back
    nodes.append({'name': 'func_door', 'mesh': 'Vault door',
                  'extras': {'targetname': 'vault', 'angle': 90, 'speed': 60, 'wait': -1}})

    # Pool (south-west): a 0.6 m rim, water 0.45 m up
    r = world['concrete']
    r.box(-14, -8, 0, 0.6, 6, 6.25)
    r.box(-14, -8, 0, 0.6, 9.75, 10)
    r.box(-14, -13.75, 0, 0.6, 6.25, 9.75)
    r.box(-8.25, -8, 0, 0.6, 6.25, 9.75)
    world['water'].quad([(-13.75, 0.45, 6.25), (-8.25, 0.45, 6.25), (-8.25, 0.45, 9.75), (-13.75, 0.45, 9.75)],
                        (0, 1, 0), 2.0)

    # Crates by the vault, one stacked
    w = world['wood crate']
    w.box(-11, -10, 0, 1, -9, -8, scale=1.0)
    w.box(-12.2, -11.2, 0, 1, -9.2, -8.2, scale=1.0)
    w.box(-11.7, -10.7, 1, 2, -9.1, -8.1, scale=1.0, bottom=True)

    # Point entities: 0.75 m (24 units) above their floor
    def ent(name, x, y, z, **extras):
        node = {'name': name, 'translation': [x, y, z]}
        if extras:
            node['extras'] = extras
        nodes.append(node)

    ent('info_player_start', 0, 0.75, 14, angle=90)  # faces the bunker door
    ent('info_player_deathmatch', -15, 0.75, 15, angle=45)
    ent('info_player_deathmatch', 15, 0.75, 15, angle=135)
    ent('info_player_deathmatch', 0, 0.75, -12, angle=270)
    ent('item_health', -3, 0.75, 4)
    ent('item_health', 6, 0.75, 8)
    for i in range(4):
        ent('item_armor_shard', 3 + i, 0.75, 1 - i)
    ent('item_armor_jacket', 0, 0.75, -13)  # inside the bunker, ahead of the spawn
    ent('item_health_large', -3, 0.75, -14)
    ent('weapon_shotgun', 12.5, 3.75, -1)
    ent('ammo_shells', 13.5, 3.75, 0.5)
    ent('item_quad', -16, 0.75, -4)          # in the vault
    ent('misc_explobox', 7, 0.75, -9)
    ent('misc_explobox', 8.3, 0.75, -9.6)
    return meshes, nodes, {'sky': SKY_NAME}


MATERIALS = {
    # name: (texture, colour factor, extra glTF keys); "water" in the name
    # makes the triangles liquid (MeshMap.h)
    'asphalt': ('asphalt', [1, 1, 1, 1], {}),
    'brick': ('brick', [1, 1, 1, 1], {}),
    'concrete': ('concrete', [1, 1, 1, 1], {}),
    'steel door': ('rust', [0.8, 0.85, 0.9, 1], {}),
    'lift': ('rust', [1, 0.9, 0.6, 1], {}),
    'button': ('rust', [1, 0.3, 0.25, 1], {}),
    'wood crate': ('wood', [1, 1, 1, 1], {}),
    'water': (None, [0.18, 0.42, 0.62, 0.6], {'alphaMode': 'BLEND', 'doubleSided': True}),
}


def build_glb():
    meshes, nodes, scene_extras = courtyard()
    textures = sorted({t for t, _, _ in MATERIALS.values() if t})
    bin_data = bytearray()
    views, accessors = [], []

    def add_view(data, target=None):
        while len(bin_data) % 4:
            bin_data.append(0)
        view = {'buffer': 0, 'byteOffset': len(bin_data), 'byteLength': len(data)}
        if target:
            view['target'] = target
        bin_data.extend(data)
        views.append(view)
        return len(views) - 1

    def add_floats(rows, kind, bounds=False):
        data = b''.join(struct.pack('<%df' % len(r), *r) for r in rows)
        acc = {'bufferView': add_view(data, 34962), 'componentType': 5126, 'count': len(rows), 'type': kind}
        if bounds:
            acc['min'] = [min(r[i] for r in rows) for i in range(len(rows[0]))]
            acc['max'] = [max(r[i] for r in rows) for i in range(len(rows[0]))]
        accessors.append(acc)
        return len(accessors) - 1

    images = []
    for name in textures:
        png = (ASSETS / 'textures' / (name + '.png')).read_bytes()
        images.append({'name': name, 'bufferView': add_view(png), 'mimeType': 'image/png'})

    material_names = list(MATERIALS)
    materials = []
    for name in material_names:
        texture, colour, extra = MATERIALS[name]
        pbr = {'baseColorFactor': colour, 'metallicFactor': 0.0, 'roughnessFactor': 0.9}
        if texture:
            pbr['baseColorTexture'] = {'index': textures.index(texture)}
        materials.append(dict({'name': name, 'pbrMetallicRoughness': pbr}, **extra))

    gltf_meshes, mesh_index = [], {}
    for mesh_name, mesh in meshes.items():
        prims = []
        for material, p in sorted(mesh.prims.items(), key=lambda kv: material_names.index(kv[0])):
            assert len(p.positions) < 65536
            idx = struct.pack('<%dH' % len(p.indices), *p.indices)
            accessors.append({'bufferView': add_view(idx, 34963), 'componentType': 5123,
                              'count': len(p.indices), 'type': 'SCALAR'})
            index_acc = len(accessors) - 1
            prims.append({'attributes': {'POSITION': add_floats(p.positions, 'VEC3', True),
                                         'NORMAL': add_floats(p.normals, 'VEC3'),
                                         'TEXCOORD_0': add_floats(p.uvs, 'VEC2')},
                          'indices': index_acc, 'material': material_names.index(material)})
        mesh_index[mesh_name] = len(gltf_meshes)
        gltf_meshes.append({'name': mesh_name, 'primitives': prims})

    gltf_nodes = []
    for n in nodes:
        node = dict(n)
        if 'mesh' in node:
            node['mesh'] = mesh_index[node['mesh']]
        gltf_nodes.append(node)

    doc = {
        'asset': {'version': '2.0', 'generator': 'ekron-realms tools/build_sample_map.py'},
        'scene': 0,
        'scenes': [{'name': MAP_NAME, 'nodes': list(range(len(gltf_nodes))), 'extras': scene_extras}],
        'nodes': gltf_nodes,
        'meshes': gltf_meshes,
        'materials': materials,
        'textures': [{'source': i, 'sampler': 0} for i in range(len(images))],
        'images': images,
        'samplers': [{'magFilter': 9729, 'minFilter': 9987, 'wrapS': 10497, 'wrapT': 10497}],
        'accessors': accessors,
        'bufferViews': views,
        'buffers': [{'byteLength': len(bin_data)}],
    }
    while len(bin_data) % 4:
        bin_data.append(0)
    doc['buffers'][0]['byteLength'] = len(bin_data)
    js = json.dumps(doc, separators=(',', ':')).encode()
    js += b' ' * ((4 - len(js) % 4) % 4)
    total = 12 + 8 + len(js) + 8 + len(bin_data)
    return (struct.pack('<III', 0x46546C67, 2, total) + struct.pack('<II', len(js), 0x4E4F534A) + js +
            struct.pack('<II', len(bin_data), 0x004E4942) + bytes(bin_data))


# --------------------------------------------------------------------------
# Skybox. Each pixel is shaded from its direction, so the faces meet without
# seams; flat posterised colours keep the RLE files small.

def face_direction(layer, u, v):
    """realms::MapSky::faceCoords inverted: layer (+X, -X, +Y, -Y, +Z, -Z in
    Quake axes, showing rt, lf, bk, ft, up, dn) and u, v (0..1, v down) to
    the direction seen there."""
    s, t = 2 * u - 1, 1 - 2 * v
    return [(1, -s, t), (-1, s, t), (s, 1, t), (-s, -1, t), (-t, -s, 1), (t, -s, -1)][layer]


SUFFIXES = ['rt', 'lf', 'bk', 'ft', 'up', 'dn']  # MapSky::kSuffixes, layer order


class Noise:
    def __init__(self, seed):
        rng = random.Random(seed)
        self.perm = list(range(256))
        rng.shuffle(self.perm)
        self.perm += self.perm
        self.val = [rng.random() for _ in range(256)]

    def lattice(self, x, y, z):
        p = self.perm
        return self.val[p[p[p[x & 255] + (y & 255)] + (z & 255)]]

    def value(self, x, y, z):
        ix, iy, iz = math.floor(x), math.floor(y), math.floor(z)
        fx, fy, fz = x - ix, y - iy, z - iz
        fx, fy, fz = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy), fz * fz * (3 - 2 * fz)
        def lerp(a, b, t):
            return a + (b - a) * t
        l = self.lattice
        x0 = lerp(lerp(l(ix, iy, iz), l(ix + 1, iy, iz), fx), lerp(l(ix, iy + 1, iz), l(ix + 1, iy + 1, iz), fx), fy)
        x1 = lerp(lerp(l(ix, iy, iz + 1), l(ix + 1, iy, iz + 1), fx),
                  lerp(l(ix, iy + 1, iz + 1), l(ix + 1, iy + 1, iz + 1), fx), fy)
        return lerp(x0, x1, fz)

    def fbm(self, x, y, z, octaves):
        total, amp, norm = 0.0, 1.0, 0.0
        for _ in range(octaves):
            total += self.value(x, y, z) * amp
            norm += amp
            x, y, z, amp = x * 2.03, y * 2.03, z * 2.03, amp * 0.5
        return total / norm


def mix(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


ZENITH, HORIZON = (48, 98, 178), (176, 204, 230)
HILLS, HILLS_FAR, GROUND = (78, 96, 102), (112, 132, 140), (64, 72, 70)
CLOUD, CLOUD_SHADE = (246, 248, 252), (214, 222, 234)


def sky_colour(d, clouds, hills):
    x, y, z = d
    n = math.sqrt(x * x + y * y + z * z)
    x, y, z = x / n, y / n, z / n
    e = z  # sine of the elevation (Quake Z up)
    az = math.atan2(y, x)
    ring = (math.cos(az) * 1.6, math.sin(az) * 1.6)
    far = 0.035 + 0.05 * hills.fbm(ring[0] + 7.3, ring[1], 0.5, 3)
    near = 0.015 + 0.04 * hills.fbm(ring[0] * 1.7, ring[1] * 1.7, 3.1, 4)
    if e < near:
        return GROUND if e < -0.06 else HILLS
    if e < far:
        return HILLS_FAR
    band = min(int(e ** 0.55 * 64), 63) / 63.0  # posterised gradient
    colour = mix(HORIZON, ZENITH, band)
    if e > 0.06:
        # flatten the cloud layer: further towards the horizon, denser
        k = 1.0 / (e + 0.25)
        density = clouds.fbm(x * k * 1.4 + 11.0, y * k * 1.4 - 5.0, 0.37, 5)
        threshold = 0.56 + 0.10 * e
        if density > threshold + 0.045:
            return CLOUD
        if density > threshold:
            return CLOUD_SHADE
    return colour


def rle_tga(width, height, rows):
    """Type 10 (RLE true-colour) TGA, bottom row first; packets do not cross
    rows. rows: top row first, lists of (r, g, b)."""
    out = bytearray(struct.pack('<BBBHHBHHHHBB', 0, 0, 10, 0, 0, 0, 0, 0, width, height, 24, 0))
    for row in reversed(rows):
        i = 0
        while i < width:
            run = 1
            while i + run < width and run < 128 and row[i + run] == row[i]:
                run += 1
            if run > 1:
                r, g, b = row[i]
                out += bytes((0x80 | (run - 1), b, g, r))
                i += run
                continue
            j = i + 1
            while j < width and j - i < 128 and (j + 1 >= width or row[j + 1] != row[j]):
                j += 1
            out.append(j - i - 1)
            for r, g, b in row[i:j]:
                out += bytes((b, g, r))
            i = j
    return bytes(out)


def build_sky():
    clouds, hills = Noise(2026), Noise(1121)
    faces = {}
    for layer, suffix in enumerate(SUFFIXES):
        rows = []
        for py in range(SKY_SIZE):
            v = (py + 0.5) / SKY_SIZE
            rows.append([sky_colour(face_direction(layer, (px + 0.5) / SKY_SIZE, v), clouds, hills)
                         for px in range(SKY_SIZE)])
        faces[suffix] = rle_tga(SKY_SIZE, SKY_SIZE, rows)
    return faces


def main():
    (ASSETS / 'maps').mkdir(parents=True, exist_ok=True)
    (ASSETS / 'env').mkdir(parents=True, exist_ok=True)
    glb = build_glb()
    (ASSETS / 'maps' / (MAP_NAME + '.glb')).write_bytes(glb)
    print('wrote maps/%s.glb (%d bytes)' % (MAP_NAME, len(glb)))
    for suffix, data in build_sky().items():
        (ASSETS / 'env' / (SKY_NAME + suffix + '.tga')).write_bytes(data)
        print('wrote env/%s%s.tga (%d bytes)' % (SKY_NAME, suffix, len(data)))


if __name__ == '__main__':
    main()
