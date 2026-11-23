#!/usr/bin/env python3
"""Reject reserved words used as names in the embedded GLSL shaders.

glslangValidator (tools/validate_android_shaders.py) accepts some reserved
words as identifiers, and so does NVIDIA's driver, but Mesa's compiler (Intel
and AMD on Linux, llvmpipe, softpipe) rejects the whole shader: a variable
called `packed` stopped the desktop renderer from starting there. This scans
every GLSL raw string (`R"GLSL(...)GLSL"` on desktop, `R"(#version ...)"` on
Android) for the words that GLSL 3.30 and GLSL ES 3.00 reserve, or that Mesa
treats as keywords in those versions. No dependencies; run with no arguments.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCES = [
    'sys/desktop/DesktopRenderer.cpp',
    'sys/android/app/src/main/cpp/Renderer.cpp',
    'sys/android/app/src/main/cpp/Quake2Gles.cpp',
]

# Reserved for future use in GLSL 3.30 / GLSL ES 3.00, plus later keywords
# that Mesa already reserves there (packed, shared, buffer, sample, patch...).
RESERVED = set('''
    common partition active asm class union enum typedef template this
    resource goto inline noinline public static extern external interface
    long short double half fixed unsigned superp input output hvec2 hvec3
    hvec4 dvec2 dvec3 dvec4 fvec2 fvec3 fvec4 sampler3DRect filter sizeof
    cast namespace using packed shared buffer sample patch subroutine
    coherent volatile restrict readonly writeonly atomic_uint precise
    image1D image2D image3D imageCube image1DArray image2DArray
'''.split())

RAW_STRING = re.compile(r'R"(GLSL|)\((.*?)\)\1"', re.S)
LAYOUT = re.compile(r'\blayout\s*\([^)]*\)')
COMMENT = re.compile(r'//[^\n]*|/\*.*?\*/', re.S)
IDENTIFIER = re.compile(r'\b[A-Za-z_][A-Za-z0-9_]*\b')


def check(path: Path, text: str) -> list:
    problems = []
    for match in RAW_STRING.finditer(text):
        shader = match.group(2)
        if '#version' not in shader:
            continue
        start_line = text.count('\n', 0, match.start(2)) + 1
        body = LAYOUT.sub(lambda m: ' ' * len(m.group(0)), COMMENT.sub(
            lambda m: re.sub(r'[^\n]', ' ', m.group(0)), shader))
        for ident in IDENTIFIER.finditer(body):
            if ident.group(0) in RESERVED:
                line = start_line + body.count('\n', 0, ident.start())
                problems.append(f'{path}:{line}: "{ident.group(0)}" is a reserved word in GLSL')
    return problems


def main(argv: list) -> int:
    files = [Path(a) for a in argv[1:]] or [ROOT / s for s in SOURCES]
    problems, shaders = [], 0
    for path in files:
        text = path.read_text(encoding='utf-8')
        shaders += sum('#version' in m.group(2) for m in RAW_STRING.finditer(text))
        problems += check(path.relative_to(ROOT) if path.is_relative_to(ROOT) else path, text)
    for p in problems:
        print(p)
    if problems:
        return 1
    print(f'GLSL reserved-word check OK ({shaders} shaders in {len(files)} files)')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
