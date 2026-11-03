"""Find candidate direct calls to NGS2 import trampolines in the linked boot image."""
import argparse
import json
from pathlib import Path
import re
import struct


def inspect(boot, symbols):
    data = boot.read_bytes()
    offset = 8
    if data[:8] != b'G3PROBE5':
        raise ValueError('expected G3PROBE5')

    def words(count):
        nonlocal offset
        result = struct.unpack_from('<' + 'Q' * count, data, offset)
        offset += count * 8
        return result

    image_size, entry, segment_count, reloc_count, import_count, capabilities = words(6)
    words(5)  # process parameter and main TLS
    module_count, = words(1)
    words(module_count * 7)
    binding_count, = words(1)
    words(binding_count * 3)
    segments = [words(3) for _ in range(segment_count)]
    names = []
    for _ in range(import_count):
        names.append(data[offset:offset + 128].split(b'\0')[0].decode())
        offset += 128
    symbol_names = dict(re.findall(r'\{"([^"]+)",\s*"([^"]+)"\}', symbols.read_text()))
    slots = {}
    for _ in range(reloc_count):
        target, kind, value, addend = struct.unpack_from('<QQqq', data, offset)
        offset += 32
        if kind == 1 and 0 <= value < len(names) and 'Ngs2' in names[value]:
            nid = names[value].split('#')[0]
            slots[target] = symbol_names.get(nid, names[value])
    image = data[offset:]
    if len(image) != image_size:
        raise ValueError('image size mismatch')
    jumps = {}
    candidates = []
    for base, size, flags in segments:
        if not flags & 1:
            continue
        end = min(base + size, len(image))
        at = base
        while (at := image.find(b'\xff\x25', at, end)) >= 0:
            if at + 6 <= end:
                destination = at + 6 + struct.unpack_from('<i', image, at + 2)[0]
                if destination in slots:
                    jumps[at] = slots[destination]
            at += 1
    for base, size, flags in segments:
        if not flags & 1:
            continue
        end = min(base + size, len(image))
        at = base
        while (at := image.find(b'\xe8', at, end)) >= 0:
            if at + 5 <= end:
                destination = at + 5 + struct.unpack_from('<i', image, at + 1)[0]
                if destination in jumps:
                    candidates.append({'address': hex(at), 'trampoline': hex(destination),
                                       'symbol': jumps[destination]})
            at += 1
    return {'method': 'byte-pattern candidates; confirm instruction boundaries with a disassembler',
            'slots': {hex(k): v for k, v in slots.items()},
            'trampolines': {hex(k): v for k, v in jumps.items()},
            'calls': candidates}


if __name__ == '__main__':
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--boot', type=Path, default=root / 'out/boot_gravity.bin')
    parser.add_argument('--out', type=Path, default=root / 'out/ngs2_calls.json')
    args = parser.parse_args()
    report = inspect(args.boot, root.parent / 'runtime/src/missing_154.inc')
    args.out.write_text(json.dumps(report, indent=2) + '\n')
    print(f"NGS2: {len(report['slots'])} import slots, {len(report['trampolines'])} trampolines, "
          f"{len(report['calls'])} candidate call sites; {args.out}")
