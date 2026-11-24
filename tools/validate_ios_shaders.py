#!/usr/bin/env python3
"""Validate iOS Metal shaders (using Xcode's Metal toolchain on macOS, plus structural checks)."""
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

REQUIRED_FUNCTIONS = [
    ("vertex", "sandboxVertex"),
    ("vertex", "modelVertex"),
    ("fragment", "sandboxFragment"),
    ("vertex", "skyVertex"),
    ("fragment", "skyFragment"),
    ("fragment", "polyBlendFragment"),
    ("vertex", "pipVertex"),
    ("fragment", "pipFragment"),
]


def fail(message: str, code: int = 1) -> None:
    escaped = message.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")
    print(f"::error::{escaped}")
    raise SystemExit(code)


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    shared_dir = root / "sys/ios/realms/realms Shared"
    shader_file = shared_dir / "Shaders.metal"
    header_file = shared_dir / "ShaderTypes.h"

    if not shader_file.is_file():
        fail(f"Missing Metal shader file: {shader_file}")
    if not header_file.is_file():
        fail(f"Missing shared shader header: {header_file}")

    source = shader_file.read_text(encoding="utf-8")
    for stage, fn_name in REQUIRED_FUNCTIONS:
        pattern = rf"\b{stage}\s+[A-Za-z0-9_]+\s+{fn_name}\s*\("
        if re.search(pattern, source) is None:
            fail(f"Missing required {stage} entry point '{fn_name}' in {shader_file}")

    if sys.platform == "darwin" and shutil.which("xcrun"):
        with tempfile.TemporaryDirectory(prefix="ekron-metal-shaders-") as tmp:
            air_path = Path(tmp) / "Shaders.air"
            lib_path = Path(tmp) / "default.metallib"

            compile_cmd = [
                "xcrun",
                "-sdk",
                "iphoneos",
                "metal",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(shared_dir),
                "-c",
                str(shader_file),
                "-o",
                str(air_path),
            ]
            res = subprocess.run(compile_cmd, text=True, capture_output=True)
            if res.returncode != 0:
                fail(f"Metal shader compile failed:\n{res.stdout}{res.stderr}", res.returncode)

            link_cmd = [
                "xcrun",
                "-sdk",
                "iphoneos",
                "metallib",
                str(air_path),
                "-o",
                str(lib_path),
            ]
            link_res = subprocess.run(link_cmd, text=True, capture_output=True)
            if link_res.returncode != 0:
                fail(f"Metal library link failed:\n{link_res.stdout}{link_res.stderr}", link_res.returncode)

            print(f"{shader_file.relative_to(root)}: Metal iphoneos compile/link validation OK")
    else:
        print(f"{shader_file.relative_to(root)}: Metal entry-point verification OK (xcrun unavailable on {sys.platform})")


if __name__ == "__main__":
    main()
