#!/usr/bin/env python3
"""Rebuilds sys/android/app/src/main/assets/sounds/ from CC0 sources on GitHub.

Every file is CC0 1.0 (public domain dedication). The sources are pinned to
exact commits so the result can be checked again later:

  * Kenney packs (Impact Sounds, Interface Sounds, RPG Audio, Sci-Fi Sounds),
    mirrored unchanged in kapishdima/soundcn. These .ogg files are copied
    byte for byte; only the names change.
  * "racing car engine sound loops" by domasx2 (OpenGameArt, CC0), mirrored
    in yashimosh/border-run. Re-encoded to .ogg.
  * "Leidseplein" street recording by erilee (Freesound 108285, CC0) and
    "Infinite Peace" by Kevin MacLeod (FreePD.com, CC0), both in the
    SoundSafari/CC0-1.0-Music corpus. Trimmed, levelled, made to loop
    (ambience) and re-encoded to .ogg.

Needs: python3 -m pip install soundfile numpy
Usage: python3 tools/build_sounds.py [--cache DIR]
Set GITHUB_TOKEN to avoid the anonymous GitHub API rate limit.
"""

from __future__ import annotations

import argparse
import io
import json
import os
import sys
import urllib.parse
import urllib.request
import zipfile
from pathlib import Path

import numpy as np
import soundfile as sf

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "sys/android/app/src/main/assets/sounds"

SOUNDCN = ("kapishdima/soundcn", "7cbfbb3f56e8548b81fb26410bdcea657447d087")
SAFARI = ("SoundSafari/CC0-1.0-Music", "655388f987eacf6d08d73080fcc3e727f267bfde")
BORDER_RUN = ("yashimosh/border-run", "38d6afa442a06bbd414f21ac05c0f1b4d08bd705")

KENNEY_PACKS = {
    "impact": ("kenney_impact-sounds", "Impact Sounds"),
    "interface": ("kenney_interface-sounds", "Interface Sounds"),
    "rpg": ("kenney_rpg-audio", "RPG Audio"),
    "scifi": ("kenney_sci-fi-sounds", "Sci-Fi Sounds"),
}

# output name -> (pack, original file). Copied unchanged.
KENNEY = {}
for surface in ("concrete", "wood", "grass", "carpet", "snow"):
    for i in range(5):
        KENNEY[f"step_{surface}_{i + 1}.ogg"] = ("impact", f"footstep_{surface}_00{i}.ogg")
KENNEY.update({
    "jump.ogg": ("rpg", "cloth1.ogg"),
    "land_1.ogg": ("impact", "impactSoft_heavy_000.ogg"),
    "land_2.ogg": ("impact", "impactSoft_heavy_001.ogg"),
    "grab.ogg": ("scifi", "forceField_000.ogg"),
    "throw.ogg": ("scifi", "laserLarge_000.ogg"),
    "freeze.ogg": ("scifi", "forceField_003.ogg"),
    "spawn.ogg": ("scifi", "laserSmall_000.ogg"),
    "delete.ogg": ("scifi", "explosionCrunch_000.ogg"),
    "tool_switch.ogg": ("interface", "switch_002.ogg"),
    "door_open.ogg": ("rpg", "doorOpen_1.ogg"),
    "door_close.ogg": ("rpg", "doorClose_1.ogg"),
    "car_door_open.ogg": ("rpg", "metalLatch.ogg"),
    "car_door_close.ogg": ("impact", "impactMetal_heavy_001.ogg"),
    "ui_click.ogg": ("interface", "click_002.ogg"),
    "ui_open.ogg": ("interface", "maximize_001.ogg"),
    "ui_close.ogg": ("interface", "minimize_001.ogg"),
    "ui_confirm.ogg": ("interface", "confirmation_001.ogg"),
    "ui_error.ogg": ("interface", "error_004.ogg"),
})
for i in range(3):
    KENNEY[f"impact_wood_{i + 1}.ogg"] = ("impact", f"impactWood_heavy_00{i}.ogg")
    KENNEY[f"impact_metal_{i + 1}.ogg"] = ("impact", f"impactMetal_medium_00{i}.ogg")
    KENNEY[f"impact_generic_{i + 1}.ogg"] = ("impact", f"impactGeneric_light_00{i}.ogg")


def fetch(url: str, accept: str | None = None) -> bytes:
    req = urllib.request.Request(url, headers={"User-Agent": "ekron-realms-build-sounds"})
    if accept:
        req.add_header("Accept", accept)
    token = os.environ.get("GITHUB_TOKEN")
    if token and "api.github.com" in url:
        req.add_header("Authorization", f"Bearer {token}")
    with urllib.request.urlopen(req, timeout=300) as r:
        return r.read()


def github_file(repo: tuple[str, str], path: str, cache: Path) -> Path:
    name, sha = repo
    target = cache / name.replace("/", "_") / sha / path
    if not target.exists():
        target.parent.mkdir(parents=True, exist_ok=True)
        url = f"https://api.github.com/repos/{name}/contents/{urllib.parse.quote(path)}?ref={sha}"
        target.write_bytes(fetch(url, "application/vnd.github.raw"))
    return target


def soundcn_zip(cache: Path) -> zipfile.ZipFile:
    name, sha = SOUNDCN
    target = cache / f"soundcn-{sha}.zip"
    if not target.exists():
        cache.mkdir(parents=True, exist_ok=True)
        target.write_bytes(fetch(f"https://codeload.github.com/{name}/zip/{sha}"))
    return zipfile.ZipFile(target)


def rms_db(x: np.ndarray) -> float:
    return float(20 * np.log10(np.sqrt(np.mean(np.square(x))) + 1e-12))


def level(x: np.ndarray, target_rms_db: float, peak: float = 0.89) -> np.ndarray:
    x = x * (10 ** ((target_rms_db - rms_db(x)) / 20))
    top = float(np.max(np.abs(x)))
    return x * (peak / top) if top > peak else x


def loop_crossfade(x: np.ndarray, fade: int) -> np.ndarray:
    """Folds the last `fade` frames over the first ones (equal power), so the
    result repeats without a seam."""
    body, tail = x[:-fade].copy(), x[-fade:]
    t = np.linspace(0.0, 1.0, fade, endpoint=False)
    if x.ndim == 2:
        t = t[:, None]
    body[:fade] = body[:fade] * np.sin(t * np.pi / 2) + tail * np.cos(t * np.pi / 2)
    return body


def write_ogg(path: Path, data: np.ndarray, rate: int, compression: float) -> None:
    # Written in blocks: one huge libsndfile Vorbis write can crash.
    data = np.ascontiguousarray(data, dtype=np.float32)
    with sf.SoundFile(str(path), "w", rate, 1 if data.ndim == 1 else data.shape[1],
                      format="OGG", subtype="VORBIS", compression_level=compression) as f:
        for start in range(0, len(data), 16384):
            f.write(data[start:start + 16384])


def steadiest_window(x: np.ndarray, rate: int, seconds: float) -> np.ndarray:
    """The window whose loudness changes least second to second (no horn or
    shout standing out of the loop)."""
    mono = x.mean(axis=1) if x.ndim == 2 else x
    per = np.array([rms_db(mono[i:i + rate]) for i in range(0, len(mono) - rate, rate)])
    span = int(seconds)
    best, best_score = 0, float("inf")
    for start in range(0, len(per) - span):
        win = per[start:start + span]
        score = float(np.std(win) + 0.25 * (np.max(win) - np.median(win)))
        if score < best_score:
            best, best_score = start, score
    return x[best * rate:(best + span) * rate]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--cache", default=str(Path.home() / ".cache/ekron-sounds"))
    args = ap.parse_args()
    cache = Path(args.cache)
    OUT.mkdir(parents=True, exist_ok=True)

    z = soundcn_zip(cache)
    prefix = f"soundcn-{SOUNDCN[1]}/assets/"
    for out_name, (pack, original) in sorted(KENNEY.items()):
        (OUT / out_name).write_bytes(z.read(prefix + f"{KENNEY_PACKS[pack][0]}/{original}"))

    # Car engine: one loop by domasx2; the game sets its pitch from the speed.
    data, rate = sf.read(github_file(BORDER_RUN, "public/sfx/engine/loop_0.wav", cache), always_2d=False)
    write_ogg(OUT / "engine.ogg", level(data, -14.0), rate, 0.35)

    # Drone propellers: Kenney Sci-Fi "engineCircular_002", cut to a seamless loop.
    with z.open(prefix + "kenney_sci-fi-sounds/engineCircular_002.ogg") as f:
        data, rate = sf.read(io.BytesIO(f.read()), always_2d=False)
    data = data.mean(axis=1) if data.ndim == 2 else data
    data = loop_crossfade(data[int(0.5 * rate):int(4.0 * rate)], int(0.5 * rate))
    write_ogg(OUT / "drone.ogg", level(data, -16.0), rate, 0.35)

    # City ambience: 40 steady seconds of a street recording, looped.
    data, rate = sf.read(github_file(SAFARI, "freesound.org/108285__erilee__erilee_extleidseplein.wav", cache),
                         always_2d=True)
    data = steadiest_window(data, rate, 43.0)
    data = loop_crossfade(data, 3 * rate)
    write_ogg(OUT / "ambience_city.ogg", level(data, -24.0), rate, 0.75)

    # Music: quiet and occasional, played whole.
    data, rate = sf.read(github_file(SAFARI, "freepd.com/Infinite Peace.mp3", cache), always_2d=True)
    fade = int(2.0 * rate)
    data[-fade:] *= np.linspace(1.0, 0.0, fade)[:, None]
    write_ogg(OUT / "music_infinite_peace.ogg", level(data, -20.0), rate, 0.6)

    total = 0
    report = []
    for p in sorted(OUT.glob("*.ogg")):
        info = sf.info(p)
        total += p.stat().st_size
        report.append({"file": p.name, "bytes": p.stat().st_size, "seconds": round(info.duration, 3),
                       "rate": info.samplerate, "channels": info.channels})
    print(json.dumps(report, indent=1))
    print(f"{len(report)} files, {total / 1024:.0f} KiB", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
