#!/usr/bin/env python3
"""Validate the actual embedded GLES 3 shader pairs; needs glslangValidator."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
cpp = root / 'sys/android/app/src/main/cpp'
validator = os.environ.get('GLSLANG_VALIDATOR', 'glslangValidator')
# (source file, [(vertex, fragment), ...]): the sandbox program and sky in
# Renderer.cpp, the Quake 2 entity / effect programs in Quake2Gles.cpp.
programs = [
    ('Renderer.cpp', [('vertex', 'fragment'), ('skyVertex', 'skyFragment')]),
    ('Quake2Gles.cpp', [('entityVertex', 'entityFragment'), ('effectVertex', 'effectFragment')]),
]
with tempfile.TemporaryDirectory(prefix='ekron-shaders-') as tmp:
    for file_name, pairs in programs:
        source = (cpp / file_name).read_text()
        for vs, fs in pairs:
            paths = []
            for name, suffix in [(vs, '.vert'), (fs, '.frag')]:
                match = re.search(r'const char \*' + name + r'=R"\((.*?)\)";', source, re.S)
                if match is None:
                    raise RuntimeError(f'Shader source not found: {file_name}: {name}')
                path = Path(tmp) / (name + suffix)
                path.write_text(match.group(1))
                paths.append(str(path))
            result = subprocess.run([validator, '-l', *paths], text=True, capture_output=True)
            print(result.stdout, end='')
            if result.returncode:
                message = (result.stdout + result.stderr).replace('%', '%25').replace('\r', '%0D').replace('\n', '%0A')
                print('::error::' + message)
                raise SystemExit(result.returncode)
            print(f'{file_name}: {vs} + {fs}: GLSL ES compilation/link validation OK')
