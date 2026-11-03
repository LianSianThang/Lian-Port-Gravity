"""Resolve native runtime dependencies and install hash-checked runtime bundles."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import tempfile


def sha256(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def pe_imports(path):
    data = Path(path).read_bytes()
    def get(fmt, offset):
        if offset < 0 or offset + struct.calcsize(fmt) > len(data):
            raise ValueError(f'Truncated PE file: {path}')
        return struct.unpack_from(fmt, data, offset)
    if data[:2] != b'MZ':
        raise ValueError(f'Not a PE executable: {path}')
    pe, = get('<I', 0x3c)
    if data[pe:pe + 4] != b'PE\0\0':
        raise ValueError(f'Invalid PE header: {path}')
    count, = get('<H', pe + 6)
    opt_size, = get('<H', pe + 20)
    opt = pe + 24
    magic, = get('<H', opt)
    if magic not in (0x10b, 0x20b):
        raise ValueError(f'Unsupported PE optional header: {path}')
    directories = opt + (112 if magic == 0x20b else 96)
    base, = get('<Q' if magic == 0x20b else '<I', opt + (24 if magic == 0x20b else 28))
    sections = []
    for index in range(count):
        virtual_size, rva, raw_size, raw = get('<IIII', opt + opt_size + index * 40 + 8)
        sections.append((rva, max(virtual_size, raw_size), raw, raw_size))
    def offset(rva):
        for start, size, raw, raw_size in sections:
            if start <= rva < start + size and rva - start < raw_size:
                return raw + rva - start
        raise ValueError(f'Unmapped PE import address: {path}')
    def name(rva):
        start = offset(rva)
        end = data.find(b'\0', start, start + 512)
        if end < 0:
            raise ValueError('Unterminated DLL name')
        value = data[start:end].decode('ascii')
        if not re.fullmatch(r'[A-Za-z0-9_.+-]+\.dll', value, re.IGNORECASE):
            raise ValueError(f'Invalid DLL name: {value!r}')
        return value
    result = set()
    for directory, stride in ((1, 20), (13, 32)):
        rva, size = get('<II', directories + directory * 8)
        if not rva or not size:
            continue
        start = offset(rva)
        for position in range(start, start + size, stride):
            values = get('<' + 'I' * (stride // 4), position)
            if not any(values):
                break
            if directory == 1:
                result.add(name(values[3]))
            else:
                result.add(name(values[1] if values[0] & 1 else values[1] - base))
    return sorted(result, key=str.lower)


def dependency_closure(executables, search_dirs):
    directories = [{p.name.lower(): p for p in Path(d).glob('*.dll')} for d in search_dirs]
    system = Path(os.environ.get('SystemRoot', r'C:\Windows')) / 'System32'
    pending = list(map(Path, executables))
    resolved = {}
    while pending:
        binary = pending.pop()
        for name in pe_imports(binary):
            key = name.lower()
            if key in resolved or key.startswith(('api-ms-', 'ext-ms-')):
                continue
            source = next((d[key] for d in directories if key in d), None)
            if source:
                resolved[key] = source
                pending.append(source)
            elif not (system / name).is_file():
                raise ValueError(f'Missing dependency {name} imported by {binary.name}')
    return sorted(resolved.values(), key=lambda p: p.name.lower())


def write_manifest(directory):
    directory = Path(directory)
    files = {p.name: {'sha256': sha256(p), 'bytes': p.stat().st_size}
             for p in sorted(directory.iterdir()) if p.is_file() and (p.suffix.lower() == '.dll' or
                 p.name in ('gravity-probe-local.exe', 'lianport-guest-start.exe'))}
    (directory / 'runtime_manifest.json').write_text(json.dumps({'format': 1, 'files': files}, indent=2) + '\n')


def install_bundle(source, destination):
    source, destination = Path(source).resolve(), Path(destination).resolve()
    manifest = json.loads((source / 'runtime_manifest.json').read_text())
    files = manifest.get('files', {})
    if manifest.get('format') != 1 or not {'gravity-probe-local.exe', 'lianport-guest-start.exe'} <= files.keys():
        raise ValueError('Runtime bundle does not contain the required executables.')
    for name, metadata in files.items():
        if '/' in name or '\\' in name or ':' in name or Path(name).suffix.lower() not in ('.dll', '.exe'):
            raise ValueError(f'Invalid runtime bundle filename: {name!r}')
        if Path(name).suffix.lower() == '.exe' and name not in ('gravity-probe-local.exe', 'lianport-guest-start.exe'):
            raise ValueError(f'Unexpected executable: {name}')
        path = source / name
        if not path.is_file() or path.stat().st_size != metadata['bytes'] or sha256(path) != metadata['sha256']:
            raise ValueError(f'Runtime bundle verification failed: {name}')
    destination.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='runtime-', dir=destination) as temp:
        for name in files:
            shutil.copy2(source / name, Path(temp) / name)
        for name in files:
            os.replace(Path(temp) / name, destination / name)
        shutil.copy2(source / 'runtime_manifest.json', destination / 'runtime_manifest.json')


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', type=Path, required=True)
    parser.add_argument('--destination', type=Path, required=True)
    args = parser.parse_args()
    install_bundle(args.install, args.destination)
