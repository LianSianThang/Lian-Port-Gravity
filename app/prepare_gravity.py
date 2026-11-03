#!/usr/bin/env python3
"""Customized Preparation and Multi-Module Linker for Gravity Rush Remastered (CUSA01130).

Inspects decrypted eboot.bin, applies base relocations, links bundled libc.prx and libSceFios2.prx,
and emits a pre-linked PC runtime boot image without modifying game files.
"""

import argparse
import base64
import collections
import hashlib
import json
from pathlib import Path
import re
import struct
import sys


def span(data, offset, size):
    if offset < 0 or size < 0 or offset + size > len(data):
        raise ValueError(f"out of bounds: {offset:#x}+{size:#x} / {len(data):#x}")
    return data[offset:offset + size]


def unpack(fmt, data, offset):
    return struct.unpack(fmt, span(data, offset, struct.calcsize(fmt)))


def parse_self(data):
    if not data:
        raise ValueError('empty SELF (0 bytes)')
    magic = span(data, 0, 4)
    plain_elf = magic == b'\x7fELF'
    if plain_elf:
        count, base = 0, 0
    elif magic == b'O\x15=\x1d':
        count, = unpack('<H', data, 24)
        base = 32 + count * 32
    else:
        raise ValueError("expected PS4 SELF or ELF")
    header = unpack('<16sHHIQQQIHHHHHH', data, base)
    if header[0][:7] != b'\x7fELF\x02\x01\x01' or header[2] != 62:
        raise ValueError("expected little-endian x86-64 ELF")
    if header[9] != 56 or not 0 < header[10] < 256:
        raise ValueError("unsupported program headers")
    ph = [dict(zip(('type', 'flags', 'offset', 'vaddr', 'paddr', 'filesz', 'memsz', 'align'),
                   unpack('<IIQQQQQQ', data, base + header[5] + i * 56)))
          for i in range(header[10])]
    header_end = header[5] + header[10] * 56
    end = max(header_end, max(p['offset'] + p['filesz'] for p in ph))
    if end > 512 * 1024 * 1024:
        raise ValueError("probe image exceeds 512 MiB limit")
    if plain_elf:
        return bytearray(span(data, 0, end)), header, ph, [], []
    elf = bytearray(end)
    elf[:header_end] = span(data, base, header_end)
    covered = [(0, header_end)]
    segments = []
    for i in range(count):
        flags, off, size, mem = unpack('<QQQQ', data, 32 + i * 32)
        segments.append(dict(flags=hex(flags), offset=off, size=size))
        if not flags & 0x800:
            continue
        if flags & 10:
            raise ValueError("encrypted/compressed SELF segment is unsupported")
        index = (flags >> 20) & 4095
        if index >= len(ph):
            raise ValueError("invalid segment index")
        p = ph[index]
        if size != p['filesz'] or size != mem:
            raise ValueError("unsupported blocked segment layout")
        elf[p['offset']:p['offset'] + size] = span(data, off, size)
        covered.append((p['offset'], p['offset'] + size))
    missing = []
    for i, p in enumerate(ph):
        if p['filesz'] and not any(a <= p['offset'] and p['offset'] + p['filesz'] <= z
                                  for a, z in covered):
            missing.append(i)
            if p['type'] in (1, 2, 0x61000000, 0x61000010):
                raise ValueError(f"required segment {i} is unavailable")
    return elf, header, ph, segments, missing


def read_self(path):
    data = Path(path).read_bytes()
    if not data:
        raise ValueError(f'{Path(path).name} is empty: the game files are incomplete')
    return data


def sfo(data):
    magic, version, keys, values, count = unpack('<4sIIII', data, 0)
    if magic != b'\0PSF':
        raise ValueError('bad SFO signature')
    result = {}
    for i in range(count):
        key, fmt, size, capacity, off = unpack('<HHIII', data, 20 + i * 16)
        k = data[keys + key:data.index(0, keys + key)].decode()
        v = span(data, values + off, size)
        result[k] = struct.unpack('<I', v)[0] if fmt == 0x404 else v.rstrip(b'\0').decode('utf8', 'replace')
    return result


def nid(name):
    salt = bytes.fromhex('518d64a635ded8c1e6b039b1c3e55230')
    digest = hashlib.sha1(name.encode('ascii') + salt).digest()[:8][::-1]
    return base64.b64encode(digest).decode().rstrip('=').replace('/', '-')


def encode_id(raw):
    chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-"
    res = []
    while raw > 0:
        res.append(chars[raw % 64])
        raw //= 64
    return "".join(res) if res else "A"


def inspect_libc(path):
    source = read_self(path)
    elf, header, ph, _, missing = parse_self(source)
    dynamic = next(p for p in ph if p['type'] == 2)
    tags = dict(unpack('<QQ', elf, pos) for pos in
                range(dynamic['offset'], dynamic['offset'] + dynamic['filesz'], 16))
    lib = next(p for p in ph if p['type'] == 0x61000000)
    blob = span(elf, lib['offset'], lib['filesz'])
    strings = span(blob, tags[0x61000035], tags[0x61000037])
    syms = span(blob, tags[0x61000039], tags[0x6100003f])
    evidence = {'sha256': hashlib.sha256(source).hexdigest(), 'init_env_is_ret': False}
    for pos in range(0, len(syms), 24):
        name, info, other, section, value, size = unpack('<IBBHQQ', syms, pos)
        span(strings, name, 1)
        symbol = strings[name:strings.index(0, name)].decode('ascii')
        key = symbol.split('#')[0]
        if key not in (nid('_init_env'), nid('_ZNSt8ios_base4InitC1Ev')) or not section or info & 15 != 2:
            continue
        p = next(p for p in ph if p['type'] == 1 and p['flags'] & 1
                 and p['vaddr'] <= value < p['vaddr'] + p['filesz'])
        code = span(elf, p['offset'] + value - p['vaddr'], size)
        if key == nid('_init_env'):
            evidence.update(symbol=symbol, address=hex(value), size=size, bytes=code.hex(),
                            init_env_is_ret=code == b'\xc3')
        else:
            evidence['ios_base_init'] = dict(symbol=symbol, address=hex(value), size=size,
                                             code_sha256=hashlib.sha256(code).hexdigest(),
                                             name='_ZNSt8ios_base4InitC1Ev')
    return evidence


def parse_module_metadata(path):
    source = read_self(path)
    elf, header, ph, _, missing = parse_self(source)
    dp = next(p for p in ph if p['type'] == 2)
    dynamic = []
    for pos in range(dp['offset'], dp['offset'] + dp['filesz'], 16):
        tag, value = unpack('<QQ', elf, pos)
        if not tag:
            break
        dynamic.append((tag, value))
    tags = dict(dynamic)
    lp = next(p for p in ph if p['type'] == 0x61000000)
    blob = span(elf, lp['offset'], lp['filesz'])
    strings = span(blob, tags[0x61000035], tags[0x61000037])

    def string(offset):
        span(strings, offset, 1)
        return strings[offset:strings.index(0, offset)].decode('ascii')

    libraries, modules = {}, {}
    for tag, value in dynamic:
        if tag in (0x61000013, 0x61000015, 0x6100000d, 0x6100000f):
            table = libraries if tag in (0x61000013, 0x61000015) else modules
            key = encode_id(value >> 48)
            identity = (string(value & 0xffffffff), (value >> 32) & 65535)
            table[key] = identity

    def identity(name):
        parts = name.split('#')
        if len(parts) != 3:
            return None
        n, lib, mod = parts
        return (n, libraries.get(lib, (lib, 0)), modules.get(mod, (mod, 0)))

    symbols = []
    table = span(blob, tags[0x61000039], tags[0x6100003f])
    for pos in range(0, len(table), 24):
        n, info, other, section, value, size = unpack('<IBBHQQ', table, pos)
        name = string(n)
        symbols.append(dict(name=name, type=info & 15, binding=info >> 4, section=section,
                            value=value, size=size, identity=identity(name) if name else None))
    relocations = []
    for ot, st in ((0x61000029, 0x6100002d), (0x6100002f, 0x61000031)):
        table = span(blob, tags[ot], tags[st])
        for pos in range(0, len(table), 24):
            target, info, addend = unpack('<QQq', table, pos)
            relocations.append((target, info & 0xffffffff, info >> 32, addend))
    return dict(elf=elf, header=header, ph=ph, tags=tags, symbols=symbols, relocs=relocations,
                identity=identity, libraries=libraries, modules=modules,
                sha256=hashlib.sha256(source).hexdigest(), missing=missing)


FS_LOAD = bytes([0x64, 0x48, 0x8b, 0x04, 0x25, 0, 0, 0, 0])


def patch_fs_loads(image, ph, base=0):
    """Rewrite initial-exec `mov rax, fs:[0]` to GS: on Windows TEB holds TCB in a TLS slot."""
    patched = 0
    for p in ph:
        if p['type'] != 1 or not (p['flags'] & 1):
            continue
        start, end = base + p['vaddr'], base + p['vaddr'] + p['filesz']
        at = image.find(FS_LOAD, start, end)
        while at >= 0:
            image[at] = 0x65
            patched += 1
            at = image.find(FS_LOAD, at + len(FS_LOAD), end)
    return patched


def prepare_and_link(game_dir, out_dir):
    game = Path(game_dir)
    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)

    print("Step 1: Reading eboot.bin...")
    source = read_self(game / 'eboot.bin')
    elf, header, ph, segments, missing = parse_self(source)
    loads = [p for p in ph if p['type'] in (1, 0x61000010)]

    def mapped(addr, size=8):
        return any(p['vaddr'] <= addr and addr + size <= p['vaddr'] + p['memsz'] for p in loads)

    size = max(p['vaddr'] + p['memsz'] for p in loads)
    print(f"  Main executable memory image size: {size:,} bytes ({size / (1024*1024):.2f} MiB)")
    image = bytearray(size)
    for p in loads:
        if p['filesz'] > p['memsz']:
            raise ValueError('segment file size exceeds memory size')
        image[p['vaddr']:p['vaddr'] + p['filesz']] = span(elf, p['offset'], p['filesz'])
    patched_fs = patch_fs_loads(image, ph, 0)
    print(f"  Patched {patched_fs} FS_LOAD sites to GS in main executable")

    dp = next(p for p in ph if p['type'] == 2)
    dyn = []
    for pos in range(dp['offset'], dp['offset'] + dp['filesz'], 16):
        tag, value = unpack('<QQ', elf, pos)
        if tag == 0:
            break
        dyn.append((tag, value))
    tags = dict(dyn)
    lib = next(p for p in ph if p['type'] == 0x61000000)
    blob = span(elf, lib['offset'], lib['filesz'])
    strings = span(blob, tags[0x61000035], tags[0x61000037])

    def string(off):
        span(strings, off, 1)
        return strings[off:strings.index(0, off)].decode('ascii')

    syms = span(blob, tags[0x61000039], tags[0x6100003f])
    symbols = []
    for pos in range(0, len(syms), 24):
        name, info, other, shndx, value, sz = unpack('<IBBHQQ', syms, pos)
        symbols.append(dict(name=string(name), type=info & 15, section=shndx, value=value, size=sz))

    relocs, counts, imports = [], collections.Counter(), {}
    for offset_tag, size_tag in ((0x61000029, 0x6100002d), (0x6100002f, 0x61000031)):
        table = span(blob, tags[offset_tag], tags[size_tag])
        for pos in range(0, len(table), 24):
            target, info, addend = unpack('<QQq', table, pos)
            kind, sym = info & 0xffffffff, info >> 32
            counts[kind] += 1
            if not mapped(target):
                raise ValueError(f'relocation outside mapped segments: {target:#x}')
            if kind == 8:
                relocs.append((target, 0, addend, 0))  # base-relative
            elif kind in (1, 6, 7):
                symbol = symbols[sym]
                if symbol['section']:
                    if symbol['section'] == 0xfff1:
                        struct.pack_into('<Q', image, target, symbol['value'] + addend)
                    else:
                        relocs.append((target, 0, symbol['value'] + addend, 0))
                else:
                    if sym not in imports:
                        imports[sym] = len(imports)
                    rk = 2 if symbol['type'] == 1 else 1
                    if (rk == 1 and addend) or not 0 <= addend < 4096:
                        raise ValueError('unsupported import addend')
                    relocs.append((target, rk, imports[sym], addend))
            else:
                raise ValueError(f'unsupported relocation {kind}')

    names = [symbols[i]['name'] for i in imports]
    print(f"  Main executable: {len(names)} imports, {sum(counts.values()):,} relocations")

    # Step 2: Multi-module linkage with libc.prx and libSceFios2.prx
    print("\nStep 2: Linking bundled PRX modules (libc.prx, libSceFios2.prx)...")
    main_meta = parse_module_metadata(game / 'eboot.bin')
    identities = {main_meta['identity'](name): i for i, name in enumerate(names)}

    def canonical(ident, fallback=None):
        return f'{ident[0]}#{ident[1][0]}' if ident else fallback

    names = [canonical(main_meta['identity'](name), name) for name in names]

    def imported(sym):
        key = sym['identity']
        if key not in identities:
            identities[key] = len(names)
            names.append(canonical(key))
        return identities[key]

    exports = {}
    linked_segments = [(p['vaddr'], p['memsz'], p['flags']) for p in loads]
    base = (size + 65535) & ~65535
    modules_to_link = ['libc.prx', 'libSceFios2.prx']
    module_table = []
    tls_module = 2

    for modname in modules_to_link:
        modpath = game / 'sce_module' / modname
        if not modpath.exists():
            print(f"  Warning: {modname} not found, skipping linkage")
            continue
        print(f"  Linking {modname} at base offset 0x{base:X}...")
        m = parse_module_metadata(modpath)
        m_loads = [p for p in m['ph'] if p['type'] in (1, 0x61000010)]
        modsize = max(p['vaddr'] + p['memsz'] for p in m_loads)
        image.extend(bytes(base + modsize - len(image)))

        for p in m_loads:
            image[base + p['vaddr']:base + p['vaddr'] + p['filesz']] = span(m['elf'], p['offset'], p['filesz'])
            linked_segments.append((base + p['vaddr'], p['memsz'], p['flags']))
        m_patched = patch_fs_loads(image, m['ph'], base)
        print(f"  Patched {m_patched} FS_LOAD sites to GS in {modname}")

        tls = next((p for p in m['ph'] if p['type'] == 7), None)
        module_id = 0
        if tls:
            module_id = tls_module
            tls_module += 1

        for target, kind, symid, addend in m['relocs']:
            target += base
            if kind == 8:
                relocs.append((target, 0, base + addend, 0))
            elif kind == 16:
                struct.pack_into('<Q', image, target, module_id)
            elif kind in (1, 6, 7):
                sym = m['symbols'][symid]
                if sym['section']:
                    if sym['section'] == 0xfff1:
                        struct.pack_into('<Q', image, target, sym['value'] + addend)
                    else:
                        relocs.append((target, 0, base + sym['value'] + addend, 0))
                elif not sym['identity'] and sym['binding'] == 2:
                    struct.pack_into('<Q', image, target, 0)
                else:
                    rk = 2 if sym['type'] == 1 else 1
                    relocs.append((target, rk, imported(sym), addend))

        mod_export_count = 0
        for s in m['symbols']:
            if s['section'] and s['binding'] in (1, 2) and s['identity'] and s['type'] in (1, 2):
                entry_val = (base + s['value'], 2 if s['type'] == 1 else 1, modname)
                exports[s['identity']] = entry_val
                mod_export_count += 1

        module_table.append({
            'file': modname,
            'base': hex(base),
            'size': modsize,
            'init': base + m['tags'].get(12, 0),
            'tls_address': base + tls['vaddr'] if tls else 0,
            'tls_memsz': tls['memsz'] if tls else 0,
            'tls_filesz': tls['filesz'] if tls else 0,
            'exports': mod_export_count,
            'tls_module': module_id
        })

        base = (base + modsize + 65535) & ~65535

    print(f"  Linked {len(module_table)} modules. Total exports mapped: {len(exports)}")

    # Step 3: Resolve bindings between main executable and linked modules
    by_nid = {}
    for (nid, lib, mod), entry in exports.items():
        by_nid.setdefault((nid, lib[0]), []).append(entry)

    bound_count = 0
    bindings = []
    for ident, idx in identities.items():
        found = exports.get(ident)
        if not found and ident and ident[1][0] == 'libSceLibcInternal':
            candidates = by_nid.get((ident[0], 'libc'), [])
            found = candidates[0] if len(candidates) == 1 else None
        if found:
            addr, kind, prov = found
            bindings.append((idx, addr, kind, prov))
            bound_count += 1
    print(f"  Intra-module resolution: {bound_count} of {len(identities)} imports satisfied by linked PRXs!")

    # Step 3.5: Construct synthetic HLE stub module (gravity_stubs.prx) for remaining missing APIs
    print("\nStep 3.5: Constructing synthetic HLE stub module (gravity_stubs.prx)...")
    aerolib_path = Path(__file__).resolve().parent / 'data' / 'aerolib.inl'
    nid_to_name = {}
    if aerolib_path.exists():
        with open(aerolib_path, 'r', encoding='utf-8', errors='ignore') as f:
            for m in re.finditer(r'STUB\s*\(\s*"([^"]+)"\s*,\s*([A-Za-z0-9_]+)', f.read()):
                nid_to_name[m.group(1)] = m.group(2)
    print(f"  Loaded {len(nid_to_name)} symbols from AeroLib")

    missing_nids = set()
    missing_list_path = Path(__file__).resolve().parent / 'data' / 'missing_154_resolved.txt'
    if missing_list_path.exists():
        with open(missing_list_path, 'r', encoding='utf-8') as f:
            for line in f:
                m = re.match(r"\s*([A-Za-z0-9+_-]+)\s*->\s*([A-Za-z0-9_]+)", line)
                if m:
                    nid = m.group(1)
                    if nid != "q1cHNfGycLI":  # natively bound to scePadRead in patched gravity-probe.exe
                        missing_nids.add(nid)

    stub_base = base
    stub_size = 65536
    if len(image) < stub_base + stub_size:
        image.extend(bytes(stub_base + stub_size - len(image)))
    linked_segments.append((stub_base, stub_size, 5))  # PROT_READ | PROT_EXEC

    # Specialized x86-64 machine code HLE routines
    # 0x00: ret_0 (xor eax, eax; ret) -> return ORBIS_OK (0)
    image[stub_base + 0x00 : stub_base + 0x03] = b"\x31\xc0\xc3"
    # 0x10: ret_3 (mov eax, 3; ret) -> common dialog finished status
    image[stub_base + 0x10 : stub_base + 0x16] = b"\xb8\x03\x00\x00\x00\xc3"
    # 0x20: get_thread_id (mov rax, gs:[0x30]; mov eax, [rax+0x48]; ret)
    image[stub_base + 0x20 : stub_base + 0x2D] = b"\x65\x48\x8b\x04\x25\x30\x00\x00\x00\x8b\x40\x48\xc3"
    # 0x30: rtc_current_tick (writes rdtsc to *rdi, returns 0)
    image[stub_base + 0x30 : stub_base + 0x44] = b"\x48\x85\xff\x74\x0e\x0f\x31\x48\xc1\xe2\x20\x48\x09\xd0\x48\x89\x07\x31\xc0\xc3"
    # 0x50: evf_create (writes *rdi = 1, returns 0)
    image[stub_base + 0x50 : stub_base + 0x5F] = b"\x48\x85\xff\x74\x07\x48\xc7\x07\x01\x00\x00\x00\x31\xc0\xc3"
    # 0x60: evf_wait (writes *rcx = rsi [pResultPat = bitPattern], returns 0)
    image[stub_base + 0x60 : stub_base + 0x6B] = b"\x48\x85\xc9\x74\x03\x48\x89\x31\x31\xc0\xc3"
    # 0x70: ngs2_buffer_size (writes hostBuffer=NULL, size=65536 to *rdx, returns 0)
    image[stub_base + 0x70 : stub_base + 0x87] = b"\x48\x85\xd2\x74\x13\x48\xc7\x02\x00\x00\x00\x00\x48\xc7\x42\x08\x00\x00\x01\x00\x31\xc0\xc3"
    # 0x90: out_rcx_handle (writes *rcx = 0x1001, returns 0) - sceFontCreateLibrary/Renderer
    image[stub_base + 0x90 : stub_base + 0x9F] = b"\x48\x85\xc9\x74\x07\x48\xc7\x01\x01\x10\x00\x00\x31\xc0\xc3"
    # 0xA0: out_r8_handle (writes *r8 = 0x2001, returns 0) - sceFontOpenFontMemory, sceNgs2RackCreate
    image[stub_base + 0xA0 : stub_base + 0xAF] = b"\x4d\x85\xc0\x74\x07\x49\xc7\x00\x01\x20\x00\x00\x31\xc0\xc3"
    # 0xB0: out_rdx_handle (writes *rdx = 0x3001, returns 0) - sceNgs2SystemCreate, sceAudio3dPortOpen
    image[stub_base + 0xB0 : stub_base + 0xBF] = b"\x48\x85\xd2\x74\x07\x48\xc7\x02\x01\x30\x00\x00\x31\xc0\xc3"
    # 0xC0: out_rsi_handle (writes *rsi = 0x4001, returns 0) - sceAudio3dObjectReserve
    image[stub_base + 0xC0 : stub_base + 0xCF] = b"\x48\x85\xf6\x74\x07\x48\xc7\x06\x01\x40\x00\x00\x31\xc0\xc3"
    # 0xD0: font_scale (writes *rsi = 16.0f, *rdx = 16.0f, returns 0)
    image[stub_base + 0xD0 : stub_base + 0xE9] = b"\x48\x85\xf6\x74\x07\xc7\x06\x00\x00\x80\x41\x48\x85\xd2\x74\x07\xc7\x02\x00\x00\x80\x41\x31\xc0\xc3"
    # 0xF0: font_glyph_metrics (writes 16x16 metrics to *rdx, returns 0)
    image[stub_base + 0xF0 : stub_base + 0x107] = b"\x48\x85\xd2\x74\x13\x48\xc7\x02\x10\x00\x00\x00\x48\xc7\x42\x08\x10\x00\x00\x00\x31\xc0\xc3"
    # 0x110: ret_1 (mov eax, 1; ret) - handle in eax for simple creators
    image[stub_base + 0x110 : stub_base + 0x116] = b"\xb8\x01\x00\x00\x00\xc3"

    bound_indices = {b[0] for b in bindings}
    stub_count = 0
    for ident, idx in identities.items():
        if idx in bound_indices:
            continue
        nid = ident[0]
        if nid in missing_nids:
            sym_name = nid_to_name.get(nid, "")
            if sym_name == "sceKernelCreateEventFlag":
                stub_off = 0x50
            elif sym_name == "sceKernelWaitEventFlag":
                stub_off = 0x60
            elif sym_name == "sceNgs2RackQueryBufferSize":
                stub_off = 0x70
            elif sym_name in ("sceFontCreateLibraryWithEdition", "sceFontCreateRendererWithEdition"):
                stub_off = 0x90
            elif sym_name in ("sceFontOpenFontMemory", "sceNgs2RackCreate", "sceNgs2RackCreateWithAllocator"):
                stub_off = 0xA0
            elif sym_name in ("sceNgs2SystemCreateWithAllocator", "sceNgs2RackGetVoiceHandle", "sceAudio3dPortOpen"):
                stub_off = 0xB0
            elif sym_name == "sceAudio3dObjectReserve":
                stub_off = 0xC0
            elif sym_name == "sceFontGetScalePixel":
                stub_off = 0xD0
            elif sym_name == "sceFontGetCharGlyphMetrics":
                stub_off = 0xF0
            elif "UpdateStatus" in sym_name:
                stub_off = 0x10
            elif sym_name == "scePthreadGetthreadid":
                stub_off = 0x20
            elif sym_name == "sceRtcGetCurrentTick":
                stub_off = 0x30
            elif any(k in sym_name for k in ("Create", "Request")) and "EventFlag" not in sym_name:
                stub_off = 0x110
            else:
                stub_off = 0x00

            bindings.append((idx, stub_base + stub_off, 1, 'gravity_stubs.prx'))
            bound_indices.add(idx)
            stub_count += 1

    module_table.append({
        'file': 'gravity_stubs.prx',
        'base': hex(stub_base),
        'size': stub_size,
        'init': stub_base,
        'tls_address': 0,
        'tls_memsz': 0,
        'tls_filesz': 0,
        'exports': stub_count,
        'tls_module': 0
    })
    base = (stub_base + stub_size + 65535) & ~65535
    print(f"  Synthetic stubs generated: {stub_count} imports bound to gravity_stubs.prx!")
    print(f"  Total satisfied imports now: {len(bindings)} of {len(identities)}")

    # Step 4: Write G3PROBE5 format boot.bin
    print(f"\nStep 3: Writing output boot image to {out / 'boot_gravity.bin'}...")
    capabilities = 1  # native libc
    flags = capabilities

    main_tls = next((p for p in ph if p['type'] == 7), None)
    main_tls_values = (main_tls['vaddr'], main_tls['filesz'], main_tls['memsz'], main_tls['align']) if main_tls else (0, 0, 0, 0)
    procparam = next((p for p in ph if p['type'] == 0x61000001), None)
    procparam_vaddr = procparam['vaddr'] if procparam else 0

    with (out / 'boot_gravity.bin').open('wb') as f:
        # Header (56 bytes)
        f.write(struct.pack('<8s6Q', b'G3PROBE5', len(image), header[4],
                            len(linked_segments), len(relocs), len(names), flags))
        # Procparam (8 bytes)
        f.write(struct.pack('<Q', procparam_vaddr))
        # Main TLS values (4 * 8 = 32 bytes)
        f.write(struct.pack('<4Q', *main_tls_values))
        # Module table count (8 bytes)
        f.write(struct.pack('<Q', len(module_table)))
        # Module table entries (len * 7 * 8 bytes)
        for t in module_table:
            f.write(struct.pack('<7Q', int(t['base'], 16), t['size'], t['init'], t['tls_address'], t['tls_memsz'],
                                t['tls_filesz'], t['tls_module']))
        # Bindings count (8 bytes)
        f.write(struct.pack('<Q', len(bindings)))
        # Bindings entries (len * 3 * 8 bytes)
        for b in bindings:
            f.write(struct.pack('<3Q', b[0], b[1], b[2]))
        # Segments (len * 3 * 8 bytes)
        for addr, msz, flg in linked_segments:
            f.write(struct.pack('<QQQ', addr, msz, flg))
        # Names (len * 128 bytes)
        for name in names:
            encoded = name.encode('ascii')[:127]
            f.write(encoded.ljust(128, b'\0'))
        # Relocs (len * 32 bytes)
        for r in relocs:
            f.write(struct.pack('<QQqq', *r))
        # Image bytes
        f.write(image)


    # Asset summary
    arc_files = []
    total_arc_bytes = 0
    for p in (game / 'arc').glob('*.psarc'):
        arc_files.append({'name': p.name, 'size': p.stat().st_size})
        total_arc_bytes += p.stat().st_size

    report = {
        'title': sfo((game / 'sce_sys/param.sfo').read_bytes()).get('TITLE', 'Gravity Rush Remastered'),
        'entry_point': hex(header[4]),
        'total_image_size': len(image),
        'segments_count': len(linked_segments),
        'relocations_count': len(relocs),
        'total_imported_symbols': len(names),
        'satisfied_by_linked_prxs': bound_count,
        'remaining_runtime_stubs': len(names) - bound_count,
        'linked_modules': module_table,
        'arc_archives': arc_files,
        'total_arc_bytes': total_arc_bytes
    }

    report_path = out / 'gravity_analysis.json'
    with report_path.open('w', encoding='utf-8') as f:
        json.dump(report, f, indent=2)

    boot_size = (out / 'boot_gravity.bin').stat().st_size
    print(f"\n[DONE] Milestone M1 Complete!")
    print(f"  boot_gravity.bin Size: {boot_size:,} bytes ({boot_size / (1024*1024):.2f} MiB)")
    print(f"  Total Satisfied by Linked PRXs: {bound_count}")
    print(f"  Remaining to Stub in C Runtime: {len(names) - bound_count}")
    print(f"  Analysis Report: {report_path}")


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description="Prepare Gravity Rush Remastered boot image")
    parser.add_argument('--game', default=str(Path(__file__).resolve().parent.parent / 'game' / 'CUSA01130'))
    parser.add_argument('--out', default=str(Path(__file__).resolve().parent / 'out'))
    args = parser.parse_args()
    prepare_and_link(args.game, args.out)
