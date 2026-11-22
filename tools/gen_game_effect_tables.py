#!/usr/bin/env python3
"""Generates realms/src/GameEffectTables.inc from the Quake 2 sources in
this repository (GPL-2+, id Software / quake2fpc):

  * engine/client/anorms.inc   -> the 162 byte directions (MSG_WriteDir)
  * game/q_shared.pas          -> MZ2_* monster muzzle flash numbers
  * game/m_flash.pas           -> monster_flash_offset (where the flash is)
  * engine/client/cl_fx.pas    -> CL_ParseMuzzleFlash2: light colour, radius,
                                  smoke / puff, sound, attenuation per flash

Run from the repository root:  python3 tools/gen_game_effect_tables.py
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def read(path):
    with open(os.path.join(ROOT, path), encoding="latin-1") as f:
        return f.read().replace("\r", "")


def strip_comments(s):
    s = re.sub(r"\(\*.*?\*\)", "", s, flags=re.S)
    s = re.sub(r"\{[^$][^}]*\}", "", s)
    return re.sub(r"//[^\n]*", "", s)


def cf(x):
    """A C++ float literal."""
    t = "%.6g" % x
    if "." not in t and "e" not in t:
        t += ".0"
    return t + "f"


def anorms():
    s = strip_comments(read("engine/client/anorms.inc"))
    rows = re.findall(r"\(\s*(-?[\d.]+)\s*,\s*(-?[\d.]+)\s*,\s*(-?[\d.]+)\s*\)", s)
    if len(rows) != 162:
        sys.exit("anorms.inc: expected 162 normals, got %d" % len(rows))
    return [tuple(float(v) for v in r) for r in rows]


def mz2_numbers():
    s = read("game/q_shared.pas")
    out = {}
    for name, value in re.findall(r"\b(MZ2_\w+)\s*=\s*(\d+)\s*;", s):
        out[name] = int(value)
    return out


def flash_offsets():
    s = strip_comments(read("game/m_flash.pas"))
    i = s.index("monster_flash_offset")
    body = s[s.index("(", s.index("=", i)) + 1:]
    out = []
    for row in re.findall(r"\(([^()]*)\)", body)[:212]:
        parts = row.split(",")
        if len(parts) != 3:
            sys.exit("m_flash.pas: bad offset " + row)
        vals = []
        for p in parts:  # "10.6 * 1.2" (scaled soldier offsets) or "10.6"
            f = 1.0
            for factor in p.split("*"):
                if not re.match(r"^\s*-?[\d.]+\s*$", factor):
                    sys.exit("m_flash.pas: bad offset " + row)
                f *= float(factor)
            vals.append(round(f, 4))
        out.append(tuple(vals))
    return out


def flash_groups():
    s = read("engine/client/cl_fx.pas")
    i = s.index("procedure CL_ParseMuzzleFlash2;", s.index("implementation"))
    j = s.index("CL_AddDLights", i)
    body = s[i:j]
    body = body[body.index("case flash_number of") + len("case flash_number of"):]
    body = body[:body.rindex("end;\nend;")]  # the procedure's own end
    body = re.sub(r"\(\*.*?\*\)", "", body, flags=re.S)
    groups, labels, acts = [], [], []
    for line in body.split("\n"):
        line = re.sub(r"//.*", "", line)
        line = re.sub(r"\(\*.*?\*\)", "", line).strip()
        if not line or line in ("begin", "end;", "end"):
            continue
        labs = re.findall(r"\bMZ2_\w+", line)
        if labs and re.match(r"^(MZ2_\w+\s*[,:]\s*)+$", line):
            if acts:
                groups.append((labels, acts))
                labels, acts = [], []
            labels += labs
        else:
            acts.append(line)
    if labels:
        groups.append((labels, acts))
    return groups


def parse_actions(acts):
    info = {"color": [0.0, 0.0, 0.0], "radius": 0, "radiusRand": 31, "die": 0, "puff": False,
            "smoke": False, "sound": "", "soundVariants": 0, "attn": 1}
    for a in acts:
        m = re.match(r"dl\.color\[(\d)\]\s*:=\s*(-?[\d.]+)", a)
        if m:
            info["color"][int(m.group(1))] = float(m.group(2))
            continue
        m = re.match(r"dl\.radius\s*:=\s*(\d+)\s*\+\s*\(rand\(\)\s*and\s*(\d+)\)", a)
        if m:
            info["radius"], info["radiusRand"] = int(m.group(1)), int(m.group(2))
            continue
        m = re.match(r"dl\.die\s*:=\s*cl\.time\s*\+\s*(\d+)", a)
        if m:
            info["die"] = int(m.group(1))
            continue
        if a.startswith("CL_ParticleEffect(origin, vec3_origin, 0, 40)"):
            info["puff"] = True
            continue
        if a.startswith("CL_SmokeAndFlash(origin)"):
            info["smoke"] = True
            continue
        m = re.search(r"Com_sprintf\(soundname,.*'([^']+)%c\.wav'.*rand\(\) mod (\d+)", a)
        if m:
            info["sound"], info["soundVariants"] = m.group(1) + "%c.wav", int(m.group(2))
            continue
        m = re.search(r"S_StartSound\(.*S_RegisterSound\('([^']+)'\),\s*[\d.]+,\s*(ATTN_\w+)", a)
        if m:
            info["sound"] = m.group(1)
            info["attn"] = {"ATTN_NONE": 0, "ATTN_NORM": 1, "ATTN_IDLE": 2, "ATTN_STATIC": 3}[m.group(2)]
            continue
        m = re.search(r"S_StartSound\(.*S_RegisterSound\(soundname\),\s*[\d.]+,\s*(ATTN_\w+)", a)
        if m:
            info["attn"] = {"ATTN_NONE": 0, "ATTN_NORM": 1, "ATTN_IDLE": 2, "ATTN_STATIC": 3}[m.group(1)]
            continue
        sys.exit("cl_fx.pas: unknown muzzle flash action: " + a)
    return info


def main():
    norms = anorms()
    numbers = mz2_numbers()
    offsets = flash_offsets()
    groups = flash_groups()
    count = max(numbers.values()) + 1
    if len(offsets) < count:
        sys.exit("m_flash.pas: %d offsets for %d flashes" % (len(offsets), count))
    flashes = [None] * count
    for labels, acts in groups:
        info = parse_actions(acts)
        for name in labels:
            if name not in numbers:
                sys.exit("unknown flash " + name)
            flashes[numbers[name]] = info

    out = []
    out.append("// Generated by tools/gen_game_effect_tables.py from the Quake 2 sources in")
    out.append("// this repository (GPL-2+). Do not edit.")
    out.append("")
    out.append("// anorms.h: the 162 directions MSG_WriteDir / MSG_ReadDir index.")
    out.append("static const float kByteDirs[162][3] = {")
    for n in norms:
        out.append("    {%s, %s, %s}," % tuple(cf(v) for v in n))
    out.append("};")
    out.append("")
    out.append("// m_flash.c monster_flash_offset + CL_ParseMuzzleFlash2, by MZ2_* number:")
    out.append("// offset (forward, right, up), light colour, radius + (rand & radiusRand),")
    out.append("// extra light time (ms), bullet puff, smoke and flash, sound (%c: one of")
    out.append("// soundVariants letters from 'a'), attenuation.")
    out.append("struct MonsterFlash {")
    out.append("    float offset[3];")
    out.append("    float color[3];")
    out.append("    int radius, radiusRand, extraMs;")
    out.append("    bool puff, smoke;")
    out.append("    const char *sound;")
    out.append("    int soundVariants, attenuation;")
    out.append("};")
    out.append("static const int kMonsterFlashCount = %d;" % count)
    out.append("static const MonsterFlash kMonsterFlashes[%d] = {" % count)
    for i in range(count):
        o = offsets[i]
        f = flashes[i]
        if f is None:
            out.append("    {{%s, %s, %s}, {0, 0, 0}, 0, 0, 0, false, false, \"\", 0, 1}, // %d unused" % (
                cf(o[0]), cf(o[1]), cf(o[2]), i))
            continue
        radius = f["radius"] or 200
        out.append("    {{%s, %s, %s}, {%s, %s, %s}, %d, %d, %d, %s, %s, \"%s\", %d, %d}, // %d" % (
            cf(o[0]), cf(o[1]), cf(o[2]), cf(f["color"][0]), cf(f["color"][1]), cf(f["color"][2]), radius, f["radiusRand"], f["die"],
            "true" if f["puff"] else "false", "true" if f["smoke"] else "false", f["sound"], f["soundVariants"],
            f["attn"], i))
    out.append("};")
    path = os.path.join(ROOT, "realms", "src", "GameEffectTables.inc")
    with open(path, "w", newline="\n") as fh:
        fh.write("\n".join(out) + "\n")
    print("wrote %s: %d byte dirs, %d monster flashes" % (os.path.relpath(path, ROOT), len(norms), count))


if __name__ == "__main__":
    main()
