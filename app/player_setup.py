"""Validate and prepare a player's extracted game without changing the dump."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import tempfile

from prepare_gravity import prepare_and_link, sfo

APP = Path(__file__).resolve().parent
REPOSITORY = APP.parent
INPUTS = ('eboot.bin', 'sce_sys/param.sfo', 'sce_module/libc.prx', 'sce_module/libSceFios2.prx')


def game_fingerprint(game):
    game = Path(game).resolve()
    if not game.is_dir():
        raise ValueError(f'Extract the game into {game}; a PKG file cannot be launched.')
    for relative in INPUTS:
        if not (game / relative).is_file():
            raise ValueError(f'Extracted game is missing {relative}: {game}')
    if not (game / 'arc').is_dir() or not any((game / 'arc').iterdir()):
        raise ValueError('The extracted game must contain a nonempty arc directory.')
    metadata = sfo((game / 'sce_sys/param.sfo').read_bytes())
    title_id = metadata.get('TITLE_ID')
    if title_id != 'CUSA01130':
        raise ValueError(f'This runtime targets CUSA01130; selected game reports {title_id!r}.')
    hashes = {}
    for relative in INPUTS:
        with (game / relative).open('rb') as stream:
            hashes[relative] = hashlib.file_digest(stream, 'sha256').hexdigest()
    return {'title_id': title_id, 'inputs': hashes}


def needs_preparation(game, out):
    fingerprint = game_fingerprint(game)
    out = Path(out)
    try:
        recorded = json.loads((out / 'prepared_game.json').read_text(encoding='utf-8'))
        return not (out / 'boot_gravity.bin').is_file() or recorded != fingerprint
    except (OSError, ValueError):
        return True


def prepare_game(game, out=APP / 'out', force=False):
    out = Path(out)
    fingerprint = game_fingerprint(game)
    if not force and not needs_preparation(game, out):
        return False
    out.mkdir(parents=True, exist_ok=True)
    # Preparation failure leaves the previous boot image and its identity untouched.
    with tempfile.TemporaryDirectory(prefix='prepare-', dir=out) as temp:
        staging = Path(temp)
        prepare_and_link(str(Path(game).resolve()), str(staging))
        if not (staging / 'boot_gravity.bin').is_file():
            raise ValueError('Preparation did not produce a boot image.')
        if game_fingerprint(game) != fingerprint:
            raise ValueError('Game input changed during preparation. Retry when extraction has finished.')
        for path in staging.iterdir():
            if path.is_file() and path.name != 'boot_gravity.bin':
                os.replace(path, out / path.name)
        os.replace(staging / 'boot_gravity.bin', out / 'boot_gravity.bin')
        marker = out / 'prepared_game.json.tmp'
        marker.write_text(json.dumps(fingerprint, indent=2) + '\n', encoding='utf-8')
        os.replace(marker, out / 'prepared_game.json')
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, default=REPOSITORY / 'game/CUSA01130')
    parser.add_argument('--out', type=Path, default=APP / 'out')
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--force', action='store_true')
    args = parser.parse_args()
    try:
        if args.check:
            pending = needs_preparation(args.game, args.out)
            print('Game validated. ' + ('Preparation required.' if pending else 'Boot image is current.'))
        else:
            changed = prepare_game(args.game, args.out, args.force)
            print('Game prepared.' if changed else 'Boot image already matches this game.')
    except (OSError, ValueError, KeyError) as exc:
        parser.exit(1, f'Setup failed: {exc}\n')


if __name__ == '__main__':
    main()
