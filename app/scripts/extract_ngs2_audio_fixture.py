"""Extract only bgm05's sound bank/stream for local NGS2 codec verification.

Reads the user's existing dump; does not modify it. Extracted game data must
stay local and must not be committed or distributed with the port sources.
"""
import argparse
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'pt-pc-main' / 'tools'))
import psarc


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('archive', type=Path)
    ap.add_argument('output', type=Path)
    args = ap.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    with args.archive.open('rb') as stream:
        info, entries, blocks = psarc.read_toc(stream)
        manifest = psarc.read_entry(stream, entries[0], blocks, info['block_size'])
        names = ['<manifest>'] + manifest.decode().replace('\r', '').split('\n')
        for name in ('sxd/bgm05.sxd1', 'sxd/bgm05.sxd2'):
            data = psarc.read_entry(stream, entries[names.index(name)], blocks, info['block_size'])
            target = args.output / Path(name).name
            if target.exists() and target.read_bytes() != data:
                raise ValueError(f'refusing to replace different data at {target}')
            target.write_bytes(data)
            print(f'{target}: {len(data)} bytes')


if __name__ == '__main__':
    main()
