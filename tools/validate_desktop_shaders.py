#!/usr/bin/env python3
"""Validate the desktop renderer's shader programs as GLSL 330 core and as
the GLSL ES 3.00 the client gives OpenGL ES contexts (ekron::gl::shaderSource
in sys/desktop/GlLoader.h); needs glslangValidator."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
renderer = (root / 'sys/desktop/DesktopRenderer.cpp').read_text()
loader = (root / 'sys/desktop/GlLoader.h').read_text()
validator = os.environ.get('GLSLANG_VALIDATOR', 'glslangValidator')


def c_string(name):
    """The value of `constexpr const char *name = "..." "...";` in GlLoader.h."""
    match = re.search(r'constexpr const char \*' + name + r' = ((?:\s*"(?:[^"\\]|\\.)*")+);', loader)
    if match is None:
        raise RuntimeError(f'{name} not found in GlLoader.h')
    parts = re.findall(r'"((?:[^"\\]|\\.)*)"', match.group(1))
    return ''.join(parts).encode().decode('unicode_escape')


desktop_version = c_string('kDesktopShaderVersion')
gles_prelude = c_string('kGlesShaderPrelude')
shaders = dict(re.findall(r'const char \*(k\w+) = R"GLSL\((.*?)\)GLSL";', renderer, re.S))
programs = re.findall(r'compileProgram\(gl, (k\w+), (k\w+), errorOut\)', renderer)
if not programs:
    raise RuntimeError('No compileProgram(gl, kVert, kFrag, errorOut) calls found in DesktopRenderer.cpp')


def fail(message):
    print('::error::' + message.replace('%', '%25').replace('\r', '%0D').replace('\n', '%0A'))
    raise SystemExit(1)


with tempfile.TemporaryDirectory(prefix='ekron-desktop-shaders-') as tmp:
    for vs, fs in programs:
        for flavour in ('glsl330', 'gles300'):
            paths = []
            for name, suffix in ((vs, '.vert'), (fs, '.frag')):
                source = shaders.get(name)
                if source is None:
                    fail(f'Shader source not found: {name}')
                if not source.startswith(desktop_version):
                    fail(f'{name} must start with {desktop_version!r}')
                if flavour == 'gles300':
                    source = gles_prelude + source[len(desktop_version):]
                path = Path(tmp) / f'{name}.{flavour}{suffix}'
                path.write_text(source)
                paths.append(str(path))
            result = subprocess.run([validator, '-l', *paths], text=True, capture_output=True)
            if result.returncode:
                fail(f'{vs} + {fs} ({flavour}):\n' + result.stdout + result.stderr)
            print(f'{vs} + {fs}: {flavour} compilation/link validation OK')
