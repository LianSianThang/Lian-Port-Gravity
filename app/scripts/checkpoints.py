"""Catalog local episode saves or make a separate replay profile; never edit source saves."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import shutil
from runtime_files import sha256

APP = Path(__file__).resolve().parents[1]
USER = APP / 'user'


def catalog():
    records = []
    for path in sorted(USER.rglob('SAVEAUTO.sce_sys/param.bin')):
        savedata = next((p for p in path.parents if p.name == 'savedata'), None)
        if savedata is None:
            continue
        profile = savedata.parent
        strings = [s.decode('ascii') for s in re.findall(rb'[ -~]{7,}', path.read_bytes())]
        records.append({'profile': str(profile.relative_to(USER)),
                        'episode': next((s for s in strings if s.startswith('Episode ')), 'Unknown'),
                        'play_time': next((s for s in strings if s.startswith('Play Time:')), 'Unknown'),
                        'data_sha256': sha256(path.parent.parent / 'SAVEAUTO/data.bin')})
    return records


def create_replay(source):
    source = Path(source).resolve()
    if not source.is_relative_to(USER.resolve()) or not (source / 'savedata').is_dir():
        raise ValueError('Choose a profile with savedata inside app/user.')
    stamp = datetime.now(timezone.utc).strftime('%Y%m%d_%H%M%S_%f')
    destination = USER / f'replay_{stamp}'
    before = {p.relative_to(source / 'savedata').as_posix(): sha256(p)
              for p in (source / 'savedata').rglob('*') if p.is_file()}
    if not before:
        raise ValueError('Source profile contains no save files.')
    shutil.copytree(source / 'savedata', destination / 'savedata')
    for relative, digest in before.items():
        if sha256(destination / 'savedata' / relative) != digest or sha256(source / 'savedata' / relative) != digest:
            raise ValueError('Save changed during copy; replay was not validated.')
    (destination / 'pad_probe.txt').write_text('')
    (destination / 'replay_manifest.json').write_text(json.dumps(
        {'source_profile': str(source), 'source_hashes': before, 'created_utc': stamp}, indent=2) + '\n')
    return destination


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--copy-profile', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = {'replay_profile': str(create_replay(args.copy_profile))} if args.copy_profile else catalog()
    encoded = json.dumps(result, indent=2) + '\n'
    if args.output:
        output = args.output.resolve()
        if not output.is_relative_to((APP.parent / 'local-data').resolve()):
            parser.error('Catalog output must stay inside ignored local-data.')
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(encoded)
    print(encoded)
