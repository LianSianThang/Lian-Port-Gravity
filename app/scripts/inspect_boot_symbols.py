import struct
import json
import re
from pathlib import Path

APP_DIR = Path(__file__).resolve().parents[1]

with open(APP_DIR / 'out' / 'boot_gravity.bin', 'rb') as f:
    def r64(): return struct.unpack('<Q', f.read(8))[0]
    magic = f.read(8)
    size, entry, ns, nr = r64(), r64(), r64(), r64()
    import_count = r64()
    caps = r64()
    procparam = r64()
    tls = [r64() for _ in range(4)]
    module_count = r64()
    modules = []
    for _ in range(module_count):
        modules.append([r64() for _ in range(7)])
    nb = r64()
    bindings = {}
    for _ in range(nb):
        idx, addr, kind = r64(), r64(), r64()
        bindings[idx] = (addr, kind)
    segs = []
    for _ in range(ns):
        segs.append((r64(), r64(), r64()))
    names = []
    for _ in range(import_count):
        names.append(f.read(128).split(b'\0')[0].decode('utf-8', errors='replace'))

# Parse AeroLib / nid_to_name
aerolib_map = {}
with open(APP_DIR / 'prepare_gravity.py', 'r', encoding='utf-8') as f:
    text = f.read()

# find dictionary entries: "NID": "Symbol"
matches = re.findall(r'"([a-zA-Z0-9_\+\-]+)":\s*"([^"]+)"', text)
for k, v in matches:
    aerolib_map[k] = v

print(f"Loaded {len(aerolib_map)} symbol mappings from prepare_gravity.py")

print("\n=== LIB_SCE_FONT IMPORTS IN EBOOT ===")
for i, name in enumerate(names):
    if 'Font' in name:
        nid = name.split('#')[0]
        sym = aerolib_map.get(nid, 'UNKNOWN')
        b_info = bindings.get(i, ('NOT_IN_BINDINGS', 'N/A'))
        addr_str = f"0x{b_info[0]:x}" if isinstance(b_info[0], int) else str(b_info[0])
        print(f"[{i:3d}] {name:<30} -> {sym:<35} | binding: addr={addr_str} kind={b_info[1]}")

print("\n=== LIB_SCE_NGS2 IMPORTS IN EBOOT ===")
for i, name in enumerate(names):
    if 'Ngs' in name:
        nid = name.split('#')[0]
        sym = aerolib_map.get(nid, 'UNKNOWN')
        b_info = bindings.get(i, ('NOT_IN_BINDINGS', 'N/A'))
        addr_str = f"0x{b_info[0]:x}" if isinstance(b_info[0], int) else str(b_info[0])
        print(f"[{i:3d}] {name:<30} -> {sym:<35} | binding: addr={addr_str} kind={b_info[1]}")
